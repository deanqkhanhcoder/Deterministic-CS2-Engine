#pragma once

#include "target_platform.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace bhop {

enum class BhopInjectionKind : uint8_t {
    SpaceDown,
    SpaceUp,
    WheelDown,
};

struct BhopInjectionResult {
    bool executed = false;
    uint32_t value = 0;
};

// Serializes bhop worker requests onto the hook-owner message thread.
class BhopInjectionQueue {
public:
    using WakeFn = bool (*)(void* context);
    using ExecuteFn = uint32_t (*)(BhopInjectionKind kind,
                                   const target_platform::TargetIdentity& target,
                                   void* context);

    BhopInjectionQueue() = default;
    BhopInjectionQueue(const BhopInjectionQueue&) = delete;
    BhopInjectionQueue& operator=(const BhopInjectionQueue&) = delete;

    ~BhopInjectionQueue() { Stop(); }

    void Start(WakeFn wake, void* wakeContext) {
        Stop();
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_wake = wake;
        m_wakeContext = wakeContext;
        m_running.store(true, std::memory_order_release);
    }

    void Stop() {
        // Stop and Drain share this gate: once Stop returns, no executor can
        // still perform synthetic input.
        std::lock_guard<std::mutex> drainLock(m_drainMutex);
        m_running.store(false, std::memory_order_release);

        std::deque<std::shared_ptr<Request>> cancelled;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            cancelled.swap(m_queue);
            m_wake = nullptr;
            m_wakeContext = nullptr;
        }
        for (const auto& request : cancelled) {
            Complete(request, false, 0);
        }
    }

    BhopInjectionResult Submit(
        BhopInjectionKind kind,
        const target_platform::TargetIdentity& target) {
        auto request = std::make_shared<Request>();
        request->kind = kind;
        request->target = target;

        WakeFn wake = nullptr;
        void* wakeContext = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_running.load(std::memory_order_acquire)) return {};
            if (m_queue.size() >= kMaxQueuedRequests) return {};
            m_queue.push_back(request);
            wake = m_wake;
            wakeContext = m_wakeContext;
        }

        while (m_running.load(std::memory_order_acquire) &&
               (!wake || !wake(wakeContext))) {
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                if (request->done)
                    return {request->executed, request->result};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::unique_lock<std::mutex> lock(request->mutex);
        request->completed.wait(lock, [&] { return request->done; });
        return {request->executed, request->result};
    }

    void Drain(ExecuteFn execute, void* executeContext) {
        std::lock_guard<std::mutex> drainLock(m_drainMutex);
        for (;;) {
            std::shared_ptr<Request> request;
            {
                std::lock_guard<std::mutex> queueLock(m_queueMutex);
                if (m_queue.empty()) return;
                request = std::move(m_queue.front());
                m_queue.pop_front();
            }

            const uint32_t result =
                execute ? execute(request->kind, request->target, executeContext)
                        : 0;
            Complete(request, true, result);
        }
    }

private:
    static constexpr size_t kMaxQueuedRequests = 1;

    struct Request {
        BhopInjectionKind kind = BhopInjectionKind::SpaceDown;
        target_platform::TargetIdentity target{};
        std::mutex mutex;
        std::condition_variable completed;
        bool done = false;
        bool executed = false;
        uint32_t result = 0;
    };

    static void Complete(const std::shared_ptr<Request>& request,
                         bool executed,
                         uint32_t result) {
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            if (request->done) return;
            request->executed = executed;
            request->result = result;
            request->done = true;
        }
        request->completed.notify_all();
    }

    std::mutex m_queueMutex;
    std::mutex m_drainMutex;
    std::deque<std::shared_ptr<Request>> m_queue;
    std::atomic<bool> m_running{false};
    WakeFn m_wake = nullptr;
    void* m_wakeContext = nullptr;
};

} // namespace bhop
