#include "bhop_injection_queue.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace {

struct Harness {
    std::mutex mutex;
    std::condition_variable wakeReady;
    std::atomic<int> wakeAttempts{0};
    std::atomic<int> executed{0};
    int failWakeAttempts = 0;
    std::thread::id executeThread{};
    bhop::BhopInjectionKind executedKind = bhop::BhopInjectionKind::SpaceUp;
    target_platform::TargetIdentity executedTarget{};
};

struct BlockingHarness {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
};

bool Wake(void* context) {
    auto& harness = *static_cast<Harness*>(context);
    const int attempt = harness.wakeAttempts.fetch_add(1) + 1;
    if (attempt > harness.failWakeAttempts) harness.wakeReady.notify_all();
    return attempt > harness.failWakeAttempts;
}

uint32_t Execute(bhop::BhopInjectionKind kind,
                 const target_platform::TargetIdentity& target,
                 void* context) {
    auto& harness = *static_cast<Harness*>(context);
    harness.executeThread = std::this_thread::get_id();
    harness.executedKind = kind;
    harness.executedTarget = target;
    harness.executed.fetch_add(1, std::memory_order_release);
    return 7;
}

uint32_t ExecuteBlocking(bhop::BhopInjectionKind,
                         const target_platform::TargetIdentity&,
                         void* context) {
    auto& harness = *static_cast<BlockingHarness*>(context);
    std::unique_lock<std::mutex> lock(harness.mutex);
    harness.entered = true;
    harness.changed.notify_all();
    harness.changed.wait(lock, [&] { return harness.release; });
    return 1;
}

void TestOwnerThreadDispatchPreservesTarget() {
    bhop::BhopInjectionQueue queue;
    Harness harness{};
    harness.failWakeAttempts = 2;
    queue.Start(Wake, &harness);

    const target_platform::TargetIdentity target{
        reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234)),
        11,
        22,
        UINT64_C(0x1122334455667788),
        UINT64_C(0x8877665544332211),
    };
    std::atomic<uint32_t> result{0};
    std::atomic<bool> executed{false};
    std::thread worker([&] {
        const auto dispatch =
            queue.Submit(bhop::BhopInjectionKind::SpaceDown, target);
        executed.store(dispatch.executed, std::memory_order_release);
        result.store(dispatch.value, std::memory_order_release);
    });

    {
        std::unique_lock<std::mutex> lock(harness.mutex);
        assert(harness.wakeReady.wait_for(lock, std::chrono::seconds(2), [&] {
            return harness.wakeAttempts.load(std::memory_order_acquire) >= 3;
        }));
    }

    const std::thread::id ownerThread = std::this_thread::get_id();
    queue.Drain(Execute, &harness);
    queue.Drain(Execute, &harness); // Duplicate wake is harmless.
    worker.join();

    assert(result.load(std::memory_order_acquire) == 7);
    assert(executed.load(std::memory_order_acquire));
    assert(harness.executeThread == ownerThread);
    assert(harness.executedKind == bhop::BhopInjectionKind::SpaceDown);
    assert(harness.executedTarget == target);
    queue.Stop();
}

void TestStopCancelsQueuedRequest() {
    bhop::BhopInjectionQueue queue;
    Harness harness{};
    queue.Start(Wake, &harness);

    std::atomic<uint32_t> result{99};
    std::atomic<bool> executed{true};
    std::thread worker([&] {
        const auto dispatch =
            queue.Submit(bhop::BhopInjectionKind::WheelDown, {});
        executed.store(dispatch.executed, std::memory_order_release);
        result.store(dispatch.value, std::memory_order_release);
    });
    while (harness.wakeAttempts.load(std::memory_order_acquire) == 0) {
        std::this_thread::yield();
    }

    queue.Stop();
    worker.join();
    assert(result.load(std::memory_order_acquire) == 0);
    assert(!executed.load(std::memory_order_acquire));
}

void TestRepeatedWorkerDispatchStaysOnOwner() {
    constexpr int kIterations = 2000;
    bhop::BhopInjectionQueue queue;
    Harness harness{};
    queue.Start(Wake, &harness);

    std::atomic<bool> finished{false};
    std::thread worker([&] {
        const target_platform::TargetIdentity target{
            reinterpret_cast<HWND>(static_cast<uintptr_t>(0x5678)),
            33, 44, 55, 66};
        for (int i = 0; i < kIterations; ++i) {
            const auto kind = (i & 1) ? bhop::BhopInjectionKind::SpaceUp
                                      : bhop::BhopInjectionKind::SpaceDown;
            const auto dispatch = queue.Submit(kind, target);
            assert(dispatch.executed && dispatch.value == 7);
        }
        finished.store(true, std::memory_order_release);
    });

    while (!finished.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(harness.mutex);
        harness.wakeReady.wait_for(lock, std::chrono::milliseconds(10));
        lock.unlock();
        queue.Drain(Execute, &harness);
    }
    queue.Drain(Execute, &harness);
    worker.join();

    assert(harness.executed.load(std::memory_order_acquire) == kIterations);
    assert(harness.executeThread == std::this_thread::get_id());
    queue.Stop();
}

void TestStopWaitsForInFlightDrain() {
    bhop::BhopInjectionQueue queue;
    Harness wakeHarness{};
    BlockingHarness blocking{};
    queue.Start(Wake, &wakeHarness);

    bhop::BhopInjectionResult dispatch{};
    std::thread worker([&] {
        dispatch = queue.Submit(bhop::BhopInjectionKind::SpaceUp, {});
    });
    while (wakeHarness.wakeAttempts.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();

    std::thread drainer([&] { queue.Drain(ExecuteBlocking, &blocking); });
    {
        std::unique_lock<std::mutex> lock(blocking.mutex);
        assert(blocking.changed.wait_for(lock, std::chrono::seconds(2), [&] {
            return blocking.entered;
        }));
    }

    std::atomic<bool> stopStarted{false};
    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        stopStarted.store(true, std::memory_order_release);
        queue.Stop();
        stopReturned.store(true, std::memory_order_release);
    });
    while (!stopStarted.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!stopReturned.load(std::memory_order_acquire));

    {
        std::lock_guard<std::mutex> lock(blocking.mutex);
        blocking.release = true;
    }
    blocking.changed.notify_all();
    drainer.join();
    stopper.join();
    worker.join();

    assert(stopReturned.load(std::memory_order_acquire));
    assert(dispatch.executed && dispatch.value == 1);
}

} // namespace

int main() {
    TestOwnerThreadDispatchPreservesTarget();
    TestStopCancelsQueuedRequest();
    TestRepeatedWorkerDispatchStaysOnOwner();
    TestStopWaitsForInFlightDrain();
    return 0;
}
