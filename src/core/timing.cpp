// â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
// â•‘  Counter-Strafe v25.3 C++ â€” Timing Engine Implementation            â•‘
// â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

#include "timing.h"
#include "debug_logger.h"
#include <thread>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <immintrin.h>
#include <windows.h>
#include "build_config.h"
#include "telemetry.h"
#include "topology.h"
#include "topology.h"
#include "analysis_toolkit.h"
#include <mutex>
#include <deque>
#include "state_engine.h"

namespace timing {

// â”€â”€ QPC state â”€â”€
static int64_t s_qpcFreq = 0;
static std::once_flag s_qpcInit;

static int64_t QpcFrequency() {
    std::call_once(s_qpcInit, [] {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_qpcFreq = freq.QuadPart;
    });
    return s_qpcFreq;
}

void Init() {
    DLOG_INFO(Timing, "QPC frequency = %lld Hz", QpcFrequency());
}

int64_t NowUs() {
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    const int64_t freq = QpcFrequency();
    // Use split arithmetic to avoid overflow for systems with >10.6 days uptime
    return (cnt.QuadPart / freq) * 1000000LL +
           ((cnt.QuadPart % freq) * 1000000LL) / freq;
}

int64_t NowMs() {
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    const int64_t freq = QpcFrequency();
    // Use split arithmetic to avoid overflow
    return (cnt.QuadPart / freq) * 1000LL +
           ((cnt.QuadPart % freq) * 1000LL) / freq;
}

// â”€â”€ Timer Thread Implementation â”€â”€

struct alignas(64) TimerSlot {
    bool        active    = false;
    int64_t     expireUs  = 0;       // QPC microsecond target
    uint64_t    id        = 0;
    Key         key       = Key::W;
};

static constexpr int NUM_SLOTS = 5;  // One per WASD key + Mouse1

static std::thread      s_timerThread;
alignas(64) static std::mutex s_spinlock;
static std::mutex       s_lifecycleMutex;
static std::condition_variable s_lifecycleCv;
static bool             s_stopping = false;
static std::atomic<HANDLE> s_cv_event{nullptr};
static std::atomic<HWND> s_expiryWindow{nullptr};

struct ExpiredTimer {
    Key key;
    uint64_t id;
};
static std::mutex s_expiryQueueMutex;
static std::deque<ExpiredTimer> s_expiryQueue;

alignas(64) static std::atomic<bool>     s_running{false};
alignas(64) static std::atomic<uint64_t> s_nextTimerId{1};
alignas(64) static std::atomic<bool>     s_dirty{false};
alignas(64) static std::atomic<uint8_t>  s_activeMask{0};
#ifdef MARCO_TIMING_TESTING
static std::atomic<bool> s_forceWaitableTimerFailure{false};

void SetForceWaitableTimerFailureForTesting(bool force) {
    s_forceWaitableTimerFailure.store(force, std::memory_order_release);
}

void SetNextTimerIdForTesting(uint64_t nextId) {
    s_nextTimerId.store(nextId, std::memory_order_release);
}
#endif

static TimerSlot        s_slots[NUM_SLOTS];  // indexed by Key enum

static std::atomic<int64_t> s_adaptiveWakeMarginUs{1200}; // start at balanced
static std::atomic<int64_t> s_adaptiveSpinTailUs{800};

static double s_avgOversleepUs = 0.0;
static double s_varOversleepUs = 0.0;

static void UpdateAdaptiveController(int64_t oversleepUs) {
    s_avgOversleepUs = s_avgOversleepUs * 0.9 + oversleepUs * 0.1;
    double diff = oversleepUs - s_avgOversleepUs;
    s_varOversleepUs = s_varOversleepUs * 0.9 + (diff * diff) * 0.1;

    uint32_t mode = telemetry::g_affinityMode.load(std::memory_order_relaxed);
#if MARCO_ENABLE_FORENSIC
    static uint32_t lastMode = 0xFFFFFFFF;
    if (mode != lastMode) {
        lastMode = mode;
        telemetry::g_eventBuffer.Push(5, 0, 4, (int32_t)mode); // EVENT_MODE_CHANGE = 4
    }
#endif
    int64_t spinTail = 100;
    int64_t wakeMargin = 200;

    if (mode == 0) { // Competitive
        spinTail = 100;
        wakeMargin = 200;
    } else if (mode == 1) { // Balanced
        spinTail = 100;
        wakeMargin = 200;
    } else { // Low CPU
        spinTail = 50;
        wakeMargin = 100;
    }

    if (s_avgOversleepUs > 100.0) {
        wakeMargin += 100;
        if (wakeMargin > 500) wakeMargin = 500;
    } else if (s_avgOversleepUs < 10.0 && wakeMargin > 200) {
        wakeMargin -= 50;
    }

    s_adaptiveSpinTailUs.store(spinTail, std::memory_order_relaxed);
    s_adaptiveWakeMarginUs.store(wakeMargin, std::memory_order_relaxed);
    telemetry::g_wakeVarianceUs.store((int64_t)s_varOversleepUs, std::memory_order_relaxed);
}

static void TimerThreadFunc(HANDLE wakeEvent) {
    // Set this thread to high priority for timing precision and pin to P-Core
    topology::PinCriticalThread(L"Pro Audio");

    HANDLE hTimer = nullptr;
#ifdef MARCO_TIMING_TESTING
    if (!s_forceWaitableTimerFailure.load(std::memory_order_acquire))
#endif
    hTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer) {
#ifdef MARCO_TIMING_TESTING
        if (!s_forceWaitableTimerFailure.load(std::memory_order_acquire))
#endif
        hTimer = CreateWaitableTimerW(NULL, FALSE, NULL);
    }
    HANDLE events[] = { hTimer, wakeEvent };

    while (true) {
        // Heartbeat & Core tracking
#if MARCO_ENABLE_HEARTBEATS

#endif
        uint32_t procNumber = GetCurrentProcessorNumber();
        (void)procNumber;
        uint32_t prevCore = telemetry::g_activeTimingCore.exchange(procNumber, std::memory_order_relaxed);
        telemetry::g_activeTimingGroup.store(0, std::memory_order_relaxed);
        if (prevCore != 0xFFFFFFFF && prevCore != procNumber) {
            telemetry::g_coreMigrations.fetch_add(1, std::memory_order_relaxed);
#if MARCO_ENABLE_FORENSIC
            telemetry::g_eventBuffer.Push(5, procNumber, 3, (int32_t)prevCore); // EVENT_CORE_MIGRATION = 3
#endif
        }

        std::unique_lock<std::mutex> lock(s_spinlock);
        s_dirty.store(false, std::memory_order_relaxed);

        // Find nearest expiration
        int64_t nearestUs = INT64_MAX;
        int     nearestIdx = -1;

        for (int i = 0; i < NUM_SLOTS; ++i) {
            if (s_slots[i].active && s_slots[i].expireUs < nearestUs) {
                nearestUs = s_slots[i].expireUs;
                nearestIdx = i;
            }
        }

        if (!s_running) break;

        if (nearestIdx < 0) {
            // No active timers â€” wait for signal with timeout (250ms) to maintain core telemetry
            // Only mark blocked if we are actually waiting
            lock.unlock();
            DWORD waitRes = WaitForSingleObject(wakeEvent, 250);
            (void)waitRes;
#if MARCO_ENABLE_HEARTBEATS
            telemetry::g_blockedTiming.store(false, std::memory_order_relaxed);

#endif

            lock.lock();
            if (!s_running) break;
            continue;
        }

        // Wait until ~1ms before expiration using high-resolution waitable timer
        int64_t nowUs = NowUs();
        int64_t remainUs = nearestUs - nowUs;

        int64_t wakeMargin = s_adaptiveWakeMarginUs.load(std::memory_order_relaxed);
        int64_t spinTail = s_adaptiveSpinTailUs.load(std::memory_order_relaxed);



        if (remainUs > wakeMargin) {
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = -(int64_t)((remainUs - spinTail) * 10); // spinTail early spin, 100ns intervals
            const bool timerArmed = hTimer != nullptr &&
                SetWaitableTimer(hTimer, &dueTime, 0, NULL, NULL, FALSE) != FALSE;

            lock.unlock();
            if (timerArmed) {
                WaitForMultipleObjects(2, events, FALSE, INFINITE);
            } else {
                // Native waitable timers can be unavailable under restricted
                // policies. Wait on the wake event with a bounded timeout;
                // never pass a null handle or busy-spin for the full duration.
                const int64_t coarseWaitUs = std::max<int64_t>(
                    1000, remainUs - wakeMargin);
                const DWORD coarseWaitMs = static_cast<DWORD>(std::min<int64_t>(
                    coarseWaitUs / 1000, MAXDWORD - 1));
                WaitForSingleObject(wakeEvent, coarseWaitMs);
            }

            lock.lock();
            continue; // Re-evaluate nearest
        }
        // [BYPASS SCHEDULER] If remainUs <= wakeMargin, we fall straight into spin. 
        // We DO NOT call SetWaitableTimer or WaitForMultipleObjects to avoid context yielding.

        // Release lock for spin-wait phase
        TimerSlot slot = s_slots[nearestIdx];
        lock.unlock();

        // Spin-wait for precise timing (last ~1ms)
        int64_t spinStartUs = NowUs();
        (void)spinStartUs;
        while (NowUs() < slot.expireUs) {
            if (s_dirty.load(std::memory_order_relaxed)) {
                break; // Break out to re-evaluate if new timer was scheduled
            }
            // If in recovery fallback, sleep instead of heavy spin
            _mm_pause();
        }

        // Check if still active (may have been cancelled during spin)
        lock.lock();
        if (!s_running) break;

        int64_t actualWakeUs = NowUs();
        if (s_slots[nearestIdx].active &&
            s_slots[nearestIdx].id == slot.id &&
            actualWakeUs >= slot.expireUs) { // Ensure it actually expired and wasn't just a dirty break
            s_slots[nearestIdx].active = false;
            
            // Recompute active mask
            uint8_t newMask = 0;
            for (int i = 0; i < NUM_SLOTS; i++) {
                if (s_slots[i].active) newMask |= (1 << i);
            }
            s_activeMask.store(newMask, std::memory_order_relaxed);
            
            lock.unlock();

            // Record metrics
            int64_t jitter = std::abs(actualWakeUs - slot.expireUs);
            (void)jitter;
            int64_t oversleep = std::max(0LL, actualWakeUs - slot.expireUs);

            if (oversleep > 1000) {
                telemetry::g_schedulerSpikes.fetch_add(1, std::memory_order_relaxed);
            }
            int64_t peak = telemetry::g_timerOversleepPeak.load(std::memory_order_relaxed);
            while (oversleep > peak && !telemetry::g_timerOversleepPeak.compare_exchange_weak(peak, oversleep, std::memory_order_relaxed));

#if MARCO_ENABLE_FORENSIC
            telemetry::g_timerJitter.Add(jitter);
            telemetry::g_oversleep.Add(oversleep);
            telemetry::g_spinDuration.Add(actualWakeUs - spinStartUs);
            
            uint8_t currentCore = (uint8_t)telemetry::g_activeTimingCore.load(std::memory_order_relaxed);
            telemetry::g_eventBuffer.Push(5, currentCore, 10, (int32_t)jitter); // EVENT_TIMER_JITTER = 10
            telemetry::g_eventBuffer.Push(5, currentCore, 11, (int32_t)oversleep); // EVENT_TIMER_OVERSLEEP = 11

            if (oversleep > 1000) {
                telemetry::g_eventBuffer.Push(5, currentCore, 12, (int32_t)oversleep); // EVENT_SPIKE = 12
            }
#endif

            // Update adaptive precision controller
            UpdateAdaptiveController(oversleep);

#if MARCO_ENABLE_FORENSIC
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(3, currentCore, (int32_t)jitter, (int32_t)(actualWakeUs - spinStartUs), (int32_t)slot.key);
            }
#endif

            const HWND expiryWindow =
                s_expiryWindow.load(std::memory_order_acquire);
            if (expiryWindow == nullptr) {
                // Isolated timing tests have no hook-owner window.
                engine::OnTimerExpired(slot.key, slot.id);
            } else {
                {
                    std::lock_guard<std::mutex> queueLock(s_expiryQueueMutex);
                    s_expiryQueue.push_back({slot.key, slot.id});
                }
                bool posted = false;
                while (s_running.load(std::memory_order_acquire)) {
                    if (PostMessageW(expiryWindow, WM_TIMER_EXPIRED, 0, 0)) {
                        posted = true;
                        break;
                    }
                    Sleep(1);
                }
                if (!posted) {
                    DLOG_WARN(Timing,
                              "Timer expiry post aborted during shutdown for %s",
                              keymap::KeyName[ki(slot.key)]);
                }
            }
        } else {
            lock.unlock();
        }
    }

    if (hTimer) {
        CloseHandle(hTimer);
    }
}

void DrainExpiredTimers() {
    for (;;) {
        ExpiredTimer expired{};
        {
            std::lock_guard<std::mutex> queueLock(s_expiryQueueMutex);
            if (s_expiryQueue.empty()) return;
            expired = s_expiryQueue.front();
            s_expiryQueue.pop_front();
        }
        engine::OnTimerExpired(expired.key, expired.id);
    }
}

void StartTimerThread(HWND expiryWindow) {
    std::unique_lock<std::mutex> lifecycleLock(s_lifecycleMutex);
    s_lifecycleCv.wait(lifecycleLock, [] { return !s_stopping; });
    if (s_running.load(std::memory_order_acquire) || s_timerThread.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(s_spinlock);
        for (auto& slot : s_slots) slot = TimerSlot{};
        s_activeMask.store(0, std::memory_order_relaxed);
    }
    s_dirty.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> queueLock(s_expiryQueueMutex);
        s_expiryQueue.clear();
    }
    HANDLE wakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (wakeEvent == nullptr) {
        DLOG_ERR(Timing, "Failed to create timer wake event");
        return;
    }
    s_expiryWindow.store(expiryWindow, std::memory_order_release);
    s_cv_event.store(wakeEvent, std::memory_order_release);
    s_running.store(true, std::memory_order_release);
    try {
        s_timerThread = std::thread(TimerThreadFunc, wakeEvent);
    } catch (...) {
        s_running.store(false, std::memory_order_release);
        s_expiryWindow.store(nullptr, std::memory_order_release);
        s_cv_event.store(nullptr, std::memory_order_release);
        CloseHandle(wakeEvent);
        throw;
    }
}

void StopTimerThread() {
    std::thread threadToJoin;
    HANDLE wakeEvent = nullptr;
    {
        std::unique_lock<std::mutex> lifecycleLock(s_lifecycleMutex);
        s_lifecycleCv.wait(lifecycleLock, [] { return !s_stopping; });
        if (!s_running.load(std::memory_order_acquire) && !s_timerThread.joinable()) return;
        s_stopping = true;
        s_running.store(false, std::memory_order_release);
        wakeEvent = s_cv_event.load(std::memory_order_acquire);
        if (wakeEvent) SetEvent(wakeEvent);
        threadToJoin = std::move(s_timerThread);
    }

    if (threadToJoin.joinable()) threadToJoin.join();

    {
        std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
        HANDLE ownedEvent = s_cv_event.exchange(nullptr, std::memory_order_acq_rel);
        s_expiryWindow.store(nullptr, std::memory_order_release);
        {
            std::lock_guard<std::mutex> queueLock(s_expiryQueueMutex);
            s_expiryQueue.clear();
        }
        if (ownedEvent) CloseHandle(ownedEvent);
        s_stopping = false;
    }
    s_lifecycleCv.notify_all();
}

uint64_t ScheduleTimerUs(Key key, int64_t durationUs) {
    std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
    if (!s_running.load(std::memory_order_acquire) || s_stopping) return 0;
    int64_t expireUs = NowUs() + durationUs;
    uint64_t id = s_nextTimerId.fetch_add(1, std::memory_order_relaxed);
    bool wakeRequired = false;

    DLOG_INFO(Timing, "ScheduleTimerUs: %s for %lld us (id=%llu)", keymap::KeyName[ki(key)], durationUs, id);

    {
        std::unique_lock<std::mutex> lock(s_spinlock);
        
        int64_t currentNearestUs = INT64_MAX;
        for (int i = 0; i < NUM_SLOTS; ++i) {
            if (s_slots[i].active && s_slots[i].expireUs < currentNearestUs) {
                currentNearestUs = s_slots[i].expireUs;
            }
        }
        
        if (expireUs < currentNearestUs) {
            wakeRequired = true;
        }

        auto& slot = s_slots[ki(key)];
        slot.active     = true;
        slot.expireUs   = expireUs;
        slot.id         = id;
        slot.key        = key;
        
        uint8_t mask = s_activeMask.load(std::memory_order_relaxed);
        mask |= (1 << ki(key));
        s_activeMask.store(mask, std::memory_order_relaxed);
    }
    s_dirty.store(true, std::memory_order_relaxed);
    
#if MARCO_ENABLE_FORENSIC
    telemetry::g_timersCreated.fetch_add(1, std::memory_order_relaxed);
#endif

    HANDLE wakeEvent = s_cv_event.load(std::memory_order_relaxed);
    if (wakeRequired && wakeEvent) {
        SetEvent(wakeEvent);
    }
    
    return id;
}

uint64_t ScheduleTimer(Key key, int durationMs) {
    return ScheduleTimerUs(key, (int64_t)durationMs * 1000LL);
}

void CancelTimer(Key key) {
    std::lock_guard<std::mutex> lifecycleLock(s_lifecycleMutex);
    if (!s_running.load(std::memory_order_acquire) || s_stopping) return;
    std::unique_lock<std::mutex> lock(s_spinlock);
    if (s_slots[ki(key)].active) {
        s_slots[ki(key)].active = false;
#if MARCO_ENABLE_FORENSIC
        telemetry::g_timersCancelled.fetch_add(1, std::memory_order_relaxed);
#endif
    }
    uint8_t mask = s_activeMask.load(std::memory_order_relaxed);
    mask &= ~(1 << ki(key));
    s_activeMask.store(mask, std::memory_order_relaxed);
    s_dirty.store(true, std::memory_order_relaxed);
}


bool AreTimersActive() {
    return s_activeMask.load(std::memory_order_relaxed) != 0;
}

} // namespace timing
