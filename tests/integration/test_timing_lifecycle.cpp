#include "timing.h"
#include "debug_logger.h"
#include "state_engine.h"
#include "telemetry.h"
#include "topology.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {
std::mutex g_callbackMutex;
std::condition_variable g_callbackCv;
std::atomic<int> g_callbacks{0};
std::atomic<DWORD> g_callbackThreadId{0};
std::atomic<uint64_t> g_callbackTimerId{0};
}

namespace dlog {
void Write(Subsystem, Level, const char*, int, const char*, ...) {}
}

namespace topology {
bool PinCriticalThread(const wchar_t*) { return true; }
}

namespace telemetry {
MetricBuffer g_hookLatency;
MetricBuffer g_timerJitter;
MetricBuffer g_oversleep;
MetricBuffer g_spinDuration;
MetricBuffer g_stateMutation;
#if MARCO_ENABLE_FORENSIC
EventRingBuffer g_eventBuffer;
std::atomic<uint64_t> g_timersCreated{0};
std::atomic<uint64_t> g_timersExecuted{0};
std::atomic<uint64_t> g_timersCancelled{0};
#endif
alignas(64) std::atomic<uint32_t> g_activeTimingGroup{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_activeTimingCore{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_activeHookGroup{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_activeHookCore{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_schedulerSpikes{0};
alignas(64) std::atomic<uint32_t> g_coreMigrations{0};
alignas(64) std::atomic<int64_t> g_timerOversleepPeak{0};
alignas(64) std::atomic<int64_t> g_wakeVarianceUs{0};
alignas(64) std::atomic<uint32_t> g_affinityMode{1};
}

namespace engine {
void OnTimerExpired(Key, uint64_t timerId) {
    g_callbackThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    g_callbackTimerId.store(timerId, std::memory_order_release);
    g_callbacks.fetch_add(1, std::memory_order_release);
    g_callbackCv.notify_all();
}
}

namespace {

LRESULT CALLBACK TimerDispatchWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
    if (msg == WM_TIMER_EXPIRED) {
        timing::DrainExpiredTimers();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void TestExpiryDispatchesOnWindowOwnerThread() {
    constexpr wchar_t kClassName[] = L"MarcoTimingLifecycleTest";
    WNDCLASSW wc{};
    wc.lpfnWndProc = TimerDispatchWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    assert(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    assert(hwnd != nullptr);

    const int before = g_callbacks.load(std::memory_order_acquire);
    const DWORD ownerThreadId = GetCurrentThreadId();
    constexpr uint64_t kWideTimerId = (uint64_t{1} << 32) + 7;
    timing::SetNextTimerIdForTesting(kWideTimerId);
    timing::StartTimerThread(hwnd);
    assert(timing::ScheduleTimerUs(Key::W, 1000) == kWideTimerId);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    MSG msg{};
    while (g_callbacks.load(std::memory_order_acquire) == before &&
           std::chrono::steady_clock::now() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
    assert(g_callbacks.load(std::memory_order_acquire) == before + 1);
    assert(g_callbackThreadId.load(std::memory_order_acquire) == ownerThreadId);
    assert(g_callbackTimerId.load(std::memory_order_acquire) == kWideTimerId);
    timing::StopTimerThread();
    DestroyWindow(hwnd);
}

void TestConcurrentClockInitialization() {
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    for (int threadIndex = 0; threadIndex < 16; ++threadIndex) {
        workers.emplace_back([&failed] {
            int64_t previousUs = timing::NowUs();
            for (int iteration = 0; iteration < 1000; ++iteration) {
                const int64_t nowUs = timing::NowUs();
                const int64_t nowMs = timing::NowMs();
                if (nowUs < previousUs || nowMs < 0) {
                    failed.store(true, std::memory_order_relaxed);
                }
                previousUs = nowUs;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    assert(!failed.load(std::memory_order_relaxed));
}

void TestScheduleIsRejectedOutsideLifecycle() {
    timing::StopTimerThread();
    assert(timing::ScheduleTimerUs(Key::W, 1000) == 0);
    timing::CancelTimer(Key::W);
    assert(!timing::AreTimersActive());
}

void TestStartStopAreIdempotentAndRestartable() {
    timing::StartTimerThread(nullptr);
    timing::StartTimerThread(nullptr);
    const uint64_t cancelled = timing::ScheduleTimerUs(Key::A, 1000000);
    assert(cancelled != 0);
    timing::CancelTimer(Key::A);
    timing::StopTimerThread();
    timing::StopTimerThread();
    assert(timing::ScheduleTimerUs(Key::A, 1000) == 0);

    timing::StartTimerThread(nullptr);
    const uint64_t delivered = timing::ScheduleTimerUs(Key::D, 1000);
    assert(delivered != 0);
    {
        std::unique_lock<std::mutex> lock(g_callbackMutex);
        assert(g_callbackCv.wait_for(lock, std::chrono::seconds(2), [] {
            return g_callbacks.load(std::memory_order_acquire) == 1;
        }));
    }
    timing::StopTimerThread();
}

void TestConcurrentStartStopDoesNotRaceHandleLifetime() {
    for (int round = 0; round < 20; ++round) {
        std::vector<std::thread> workers;
        workers.emplace_back([] { timing::StartTimerThread(nullptr); });
        workers.emplace_back([] { timing::StartTimerThread(nullptr); });
        for (auto& worker : workers) worker.join();

        workers.clear();
        workers.emplace_back([] { timing::StopTimerThread(); });
        workers.emplace_back([] { timing::StopTimerThread(); });
        for (auto& worker : workers) worker.join();

        assert(timing::ScheduleTimerUs(Key::S, 1000) == 0);
    }
}

void TestNullWaitableTimerUsesBoundedWakeFallback() {
    timing::StopTimerThread();
    const int before = g_callbacks.load(std::memory_order_acquire);
    timing::SetForceWaitableTimerFailureForTesting(true);
    timing::StartTimerThread(nullptr);
    assert(timing::ScheduleTimerUs(Key::W, 2000) != 0);
    {
        std::unique_lock<std::mutex> lock(g_callbackMutex);
        assert(g_callbackCv.wait_for(lock, std::chrono::seconds(2), [before] {
            return g_callbacks.load(std::memory_order_acquire) == before + 1;
        }));
    }
    timing::StopTimerThread();
    timing::SetForceWaitableTimerFailureForTesting(false);
}

} // namespace

int main() {
    TestConcurrentClockInitialization();
    timing::Init();
    TestScheduleIsRejectedOutsideLifecycle();
    TestStartStopAreIdempotentAndRestartable();
    TestConcurrentStartStopDoesNotRaceHandleLifetime();
    TestNullWaitableTimerUsesBoundedWakeFallback();
    TestExpiryDispatchesOnWindowOwnerThread();
    return 0;
}
