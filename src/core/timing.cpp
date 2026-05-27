// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Timing Engine Implementation            ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "timing.h"
#include "debug_logger.h"
#include <thread>
#include <atomic>
#include <algorithm>
#include <immintrin.h>
#include <windows.h>
#include "build_config.h"
#include "telemetry.h"
#include "topology.h"
#include "topology.h"
#include "analysis_toolkit.h"
#include <mutex>

namespace timing {

// ── QPC state ──
static int64_t s_qpcFreq = 0;

void Init() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    s_qpcFreq = freq.QuadPart;
    DLOG_INFO(Timing, "QPC frequency = %lld Hz", s_qpcFreq);
}

int64_t NowUs() {
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    int64_t freq = s_qpcFreq;
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
        s_qpcFreq = freq;
    }
    // Use split arithmetic to avoid overflow for systems with >10.6 days uptime
    return (cnt.QuadPart / freq) * 1000000LL + 
           ((cnt.QuadPart % freq) * 1000000LL) / freq;
}

int64_t NowMs() {
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    int64_t freq = s_qpcFreq;
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
        s_qpcFreq = freq;
    }
    // Use split arithmetic to avoid overflow
    return (cnt.QuadPart / freq) * 1000LL + 
           ((cnt.QuadPart % freq) * 1000LL) / freq;
}

// ── Timer Thread Implementation ──

struct alignas(64) TimerSlot {
    bool        active    = false;
    int64_t     expireUs  = 0;       // QPC microsecond target
    uint64_t    id        = 0;
    Key         key       = Key::W;
    std::function<void()> cb = nullptr;
};

static constexpr int NUM_SLOTS = 5;  // One per WASD key + Mouse1

static HWND             s_targetHwnd = nullptr;
static std::thread      s_timerThread;
alignas(64) static std::mutex s_spinlock;
static HANDLE           s_cv_event = nullptr;

alignas(64) static std::atomic<bool>     s_running{false};
alignas(64) static std::atomic<uint64_t> s_nextTimerId{1};
alignas(64) static std::atomic<bool>     s_dirty{false};
alignas(64) static std::atomic<uint8_t>  s_activeMask{0};

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
#if MARCO_ENABLE_TELEMETRY
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
#if MARCO_ENABLE_TELEMETRY
    telemetry::g_wakeVarianceUs.store((int64_t)s_varOversleepUs, std::memory_order_relaxed);
#endif
}

static void TimerThreadFunc() {
    // Set this thread to high priority for timing precision and pin to P-Core
    topology::PinCriticalThread(L"Pro Audio");

    HANDLE hTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer) {
        hTimer = CreateWaitableTimerW(NULL, FALSE, NULL);
    }
    HANDLE events[] = { hTimer, s_cv_event };

    while (true) {
        // Heartbeat & Core tracking
#if MARCO_ENABLE_HEARTBEATS
        telemetry::g_heartbeatTiming.store(NowMs(), std::memory_order_relaxed);
#endif
        uint32_t procNumber = GetCurrentProcessorNumber();
        (void)procNumber;
        uint32_t prevCore = telemetry::g_activeTimingCore.exchange(procNumber, std::memory_order_relaxed);
        telemetry::g_activeTimingGroup.store(0, std::memory_order_relaxed);
#if MARCO_ENABLE_TELEMETRY
        if (prevCore != 0xFFFFFFFF && prevCore != procNumber) {
            telemetry::g_coreMigrations.fetch_add(1, std::memory_order_relaxed);
            telemetry::g_eventBuffer.Push(5, procNumber, 3, (int32_t)prevCore); // EVENT_CORE_MIGRATION = 3
        }
#endif

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
            // No active timers — wait for signal with timeout (250ms) to maintain heartbeat/core telemetry
            // Only mark blocked if we are actually waiting
#if MARCO_ENABLE_HEARTBEATS
            telemetry::g_blockedTiming.store(true, std::memory_order_relaxed);
#endif
            lock.unlock();
            DWORD waitRes = WaitForSingleObject(s_cv_event, 250);
            (void)waitRes;
#if MARCO_ENABLE_HEARTBEATS
            telemetry::g_blockedTiming.store(false, std::memory_order_relaxed);
            telemetry::g_heartbeatTiming.store(NowMs(), std::memory_order_relaxed);
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

        // Fallback safety: if watchdog trips/fatal, downgrade immediately
#if MARCO_ENABLE_WATCHDOG
        if (telemetry::g_watchdogState.load(std::memory_order_relaxed) == 2) {
            wakeMargin = 200; // Low CPU mode
            spinTail = 100;
        }
#endif

        if (remainUs > wakeMargin) {
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = -(int64_t)((remainUs - spinTail) * 10); // spinTail early spin, 100ns intervals
            if (hTimer) {
                SetWaitableTimer(hTimer, &dueTime, 0, NULL, NULL, FALSE);
            }
            
            lock.unlock();
#if MARCO_ENABLE_WATCHDOG
            telemetry::g_blockedTiming.store(true, std::memory_order_relaxed);
#endif
            WaitForMultipleObjects(2, events, FALSE, INFINITE);
#if MARCO_ENABLE_WATCHDOG
            telemetry::g_blockedTiming.store(false, std::memory_order_relaxed);
            telemetry::g_heartbeatTiming.store(NowMs(), std::memory_order_relaxed);
#endif
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
#if MARCO_ENABLE_WATCHDOG
            if (telemetry::g_watchdogState.load(std::memory_order_relaxed) == 2) {
                Sleep(0);
            } else {
                _mm_pause();  // CPU hint: reduce power, yield to SMT sibling
            }
#else
            _mm_pause();
#endif
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

#if MARCO_ENABLE_TELEMETRY
            telemetry::g_timerJitter.Add(jitter);
            telemetry::g_oversleep.Add(oversleep);
            telemetry::g_spinDuration.Add(actualWakeUs - spinStartUs);
            
            uint8_t currentCore = (uint8_t)telemetry::g_activeTimingCore.load(std::memory_order_relaxed);
            telemetry::g_eventBuffer.Push(5, currentCore, 10, (int32_t)jitter); // EVENT_TIMER_JITTER = 10
            telemetry::g_eventBuffer.Push(5, currentCore, 11, (int32_t)oversleep); // EVENT_TIMER_OVERSLEEP = 11

            if (oversleep > 1000) {
                telemetry::g_schedulerSpikes.fetch_add(1, std::memory_order_relaxed);
                telemetry::g_eventBuffer.Push(5, currentCore, 12, (int32_t)oversleep); // EVENT_SPIKE = 12
            }
            int64_t peak = telemetry::g_timerOversleepPeak.load(std::memory_order_relaxed);
            while (oversleep > peak && !telemetry::g_timerOversleepPeak.compare_exchange_weak(peak, oversleep, std::memory_order_relaxed));
#endif

            // Update adaptive precision controller
            UpdateAdaptiveController(oversleep);

#if MARCO_ENABLE_TELEMETRY
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(3, currentCore, (int32_t)jitter, (int32_t)(actualWakeUs - spinStartUs), (int32_t)slot.key);
            }
#endif

            // ADD JITTER LOG FOR FIRE TRACE
            DLOG_INFO(Timing, "[FIRE_TRACE] TIMER_WAKE_JITTER id=%llu key=%s jitter=%lld", slot.id, reinterpret_cast<int64_t>(keymap::KeyName[ki(slot.key)]), jitter);

            // Post completion to main thread
            DLOG_INFO(Timing, "TimerThreadFunc: Posting WM_TIMER_EXPIRED for %s (id=%llu)", reinterpret_cast<int64_t>(keymap::KeyName[ki(slot.key)]), slot.id);
            if (slot.cb) {
                slot.cb();
            } else {
                PostMessage(s_targetHwnd, WM_TIMER_EXPIRED,
                            (WPARAM)slot.key, (LPARAM)slot.id);
            }

        } else {
            lock.unlock();
        }
    }

    if (hTimer) {
        CloseHandle(hTimer);
    }
}

void StartTimerThread(HWND hwnd) {
    if (s_running.exchange(true)) return;
    s_targetHwnd = hwnd;
    for (auto& slot : s_slots) slot.active = false;
    s_activeMask.store(0, std::memory_order_relaxed);
    if (s_cv_event == nullptr) {
        s_cv_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    }
    s_timerThread = std::thread(TimerThreadFunc);
}

void StopTimerThread() {
    if (!s_running.exchange(false)) return;
    if (s_cv_event) SetEvent(s_cv_event);
    if (s_timerThread.joinable())
        s_timerThread.join();
    if (s_cv_event) {
        CloseHandle(s_cv_event);
        s_cv_event = nullptr;
    }
}

uint64_t ScheduleTimerUs(Key key, int64_t durationUs) {
    return ScheduleTimerAtUs(key, NowUs() + durationUs);
}

uint64_t ScheduleTimerAtUs(Key key, int64_t expireUs, std::function<void()> cb) {
    uint64_t id = s_nextTimerId.fetch_add(1, std::memory_order_relaxed);
    bool wakeRequired = false;

    DLOG_INFO(Timing, "ScheduleTimerAtUs: %s for %lld us target (id=%llu)", reinterpret_cast<int64_t>(keymap::KeyName[ki(key)]), expireUs, id);

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
        slot.cb         = cb;
        
        uint8_t mask = s_activeMask.load(std::memory_order_relaxed);
        mask |= (1 << ki(key));
        s_activeMask.store(mask, std::memory_order_relaxed);
    }
    s_dirty.store(true, std::memory_order_relaxed);
    
    if (wakeRequired && s_cv_event) {
        SetEvent(s_cv_event);
    }
    
    return id;
}

uint64_t ScheduleTimer(Key key, int durationMs) {
    return ScheduleTimerUs(key, (int64_t)durationMs * 1000LL);
}

void CancelTimer(Key key) {
    std::unique_lock<std::mutex> lock(s_spinlock);
    s_slots[ki(key)].active = false;
    uint8_t mask = s_activeMask.load(std::memory_order_relaxed);
    mask &= ~(1 << ki(key));
    s_activeMask.store(mask, std::memory_order_relaxed);
    s_dirty.store(true, std::memory_order_relaxed);
}


bool AreTimersActive() {
    return s_activeMask.load(std::memory_order_relaxed) != 0;
}

} // namespace timing
