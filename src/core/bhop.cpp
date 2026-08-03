// â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
// â•‘  CS2 Bhop Engine â€” Native C++ Port of cs2_bhop_v2.ahk              â•‘
// â•‘  Worker thread architecture: hook signals â†’ cv wakeup â†’ bhop loop   â•‘
// â•‘  Exact behavioral match to AHK logic (minus F4 reload)              â•‘
// â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

#include "bhop.h"
#include "runtime_config.h"
#include "target_platform.h"
#include "debug_logger.h"
#include "state_engine.h"
#include "config_io.h"
#include "input_capture.h"
#include "injection.h"
#include "bhop_injection_gate.h"
#include "bhop_injection_queue.h"
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <immintrin.h>
#include "topology.h"
#include "telemetry.h"
#include "timing.h"

namespace bhop {

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  CONFIGURATION â€” reads from RuntimeConfig at runtime
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// [FIX #23] No more compile-time presets. All values come from rcfg::Get().

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  MODULE STATE
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
// Shared between hook thread (signal) and worker thread (execution)
static std::atomic<bool> s_spaceHeld{false};   // Physical Space key state
static std::atomic<bool> s_running{false};      // Thread lifecycle flag
static std::atomic<bool> s_waitingForSpaceRepress{false}; // Safe resume explicit state
static std::atomic<HWND> s_injectionWindow{nullptr};
static std::atomic<DWORD> s_injectionOwnerThreadId{0};
static BhopInjectionQueue s_injectionQueue;

// [FIX R-6] Atomic state for weak-memory correctness (ARM/other platforms)
static std::atomic<State>   s_state{State::Idle};      // Written by worker, read by UI


static int64_t s_qpcFreq     = 0;
static double  s_jitterAccum = 0.0;
static double  s_qpcToMs     = 0.0;

// Worker thread synchronization
static std::thread             s_workerThread;
static std::mutex              s_mutex;
static std::condition_variable s_cv;

// â”€â”€ Name tables â”€â”€
static const char* s_modeNames[] = {
    "", "LEGIT", "AGGRESSIVE", "HUMANIZED", "SCROLL_EMU"
};
static const char* s_stateNames[] = {
    "IDLE", "JUMP_START", "AIRBORNE", "LANDING"
};

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  NtDelayExecution (loaded dynamically)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
typedef LONG (NTAPI *NtDelayExecutionFn)(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
static NtDelayExecutionFn s_ntDelay = nullptr;

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  MATH UTILITIES
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static constexpr double PI = 3.14159265358979323846;

static double RandF(double lo, double hi) {
    if (lo >= hi) return lo;
    return lo + (hi - lo) * ((double)(rand() % 1000001) / 1000000.0);
}

static int RandI(int lo, int hi) {
    if (lo >= hi) return lo;
    return lo + (rand() % (hi - lo + 1));
}

static double Clamp(double val, double lo, double hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}

// Box-Muller Gaussian RNG
static double GaussianRandom(double mean, double stddev) {
    double u1;
    do { u1 = RandF(0.0, 1.0); } while (u1 < 1e-15);
    double u2 = RandF(0.0, 1.0);
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
    return Clamp(mean + stddev * z, mean - 3.0 * stddev, mean + 3.0 * stddev);
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  QPC HELPERS
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static int64_t QpcNow() {
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    return cnt.QuadPart;
}

#if MARCO_ENABLE_FORENSIC
static int64_t StallThresholdTicks(const RuntimeConfig& cfg) {
    int modeIdx = cfg.bhopMode;
    if (modeIdx < 1 || modeIdx > 4) modeIdx = 4;
    const auto& mt = cfg.modeCfg[modeIdx];
    int expectedMs = cfg.airborneLockMs + cfg.landingScanMs + mt.hMax + mt.dMax + cfg.spamIntervalMs + 250;
    int thresholdMs = std::max(2000, expectedMs);
    return (int64_t)((double)thresholdMs / s_qpcToMs);
}
#endif

// ══════════════════════════════════════════════════════════════════════
//  PRECISION WAIT (3-phase: NtDelay + QPC spin + jitter comp)
//  Runs on worker thread — safe to block for any duration.
// ══════════════════════════════════════════════════════════════════════
static void PrecisionWait(double ms) {
    // [CHAOS PROTECTION] NaN detection and timing parameter bounds protection
    if (std::isnan(ms)) ms = 0.0;
    if (ms < 0.0) ms = 0.0;
    if (ms > 5000.0) ms = 5000.0; // Clamped to 5 seconds max single delay limit to prevent infinite freezes

    // [FIX Bug #4] Apply accumulated jitter compensation to target
    double adjustedMs = ms - s_jitterAccum;
    if (adjustedMs < 0.0) adjustedMs = 0.0;

    int64_t startTick = QpcNow();
    int64_t targetTick = startTick + (int64_t)(adjustedMs / s_qpcToMs);

    // Phase 1: NtDelayExecution (sub-ms precision, yields CPU)
    int sleepMs = (int)floor(adjustedMs) - 1;
    if (sleepMs > 0 && s_ntDelay) {
        LARGE_INTEGER delay;
        delay.QuadPart = -(int64_t)sleepMs * 10000LL;
        s_ntDelay(FALSE, &delay);
    }

    // Phase 2: QPC busy-spin
    while (QpcNow() < targetTick) {
        if (!s_spaceHeld.load(std::memory_order_relaxed) ||
            !s_running.load(std::memory_order_relaxed)) {
            break;
        }
        _mm_pause();
    }

    // Phase 3: Jitter compensation (EMA drift accumulator)
    int64_t endTick = QpcNow();
    double actualMs = (double)(endTick - startTick) * s_qpcToMs;
    // Jitter is the difference between what we got and what we asked for
    double jitter = actualMs - adjustedMs;

    // Sanity check for float anomalies (NaN or Inf)
    if (std::isnan(jitter) || std::isinf(jitter)) {
        jitter = 0.0;
    }

    // [FIX Bug #4] Outlier rejection: clamp jitter spikes
    if (jitter > 5.0) jitter = 5.0;
    if (jitter < -5.0) jitter = -5.0;

    s_jitterAccum = s_jitterAccum * 0.6 + jitter * 0.4;
}

static bool s_injectedSpaceState = false;
static target_platform::TargetIdentity s_injectedSpaceTarget;

static bool ValidateSequenceTarget(const void* context) noexcept {
    const auto& sequenceTarget =
        *static_cast<const target_platform::TargetIdentity*>(context);
    return detail::IsExpectedTargetActive(
        sequenceTarget,
        [] { return target_platform::SampleStableForegroundIdentity(); },
        [] { return target_platform::GetCurrentIdentity(); });
}

static bool WakeInjectionOwner(void*) {
    const HWND hwnd = s_injectionWindow.load(std::memory_order_acquire);
    return hwnd != nullptr &&
           PostMessageW(hwnd, WM_BHOP_INJECTION_READY, 0, 0) != FALSE;
}

static uint32_t ExecuteInjectionOnOwnerThread(
    BhopInjectionKind kind,
    const target_platform::TargetIdentity& sequenceTarget,
    void*) {
    switch (kind) {
        case BhopInjectionKind::SpaceDown:
            return injection::SpaceDownForTarget(sequenceTarget);
        case BhopInjectionKind::SpaceUp:
            return injection::SpaceUpForTarget(sequenceTarget);
        case BhopInjectionKind::WheelDown:
            return injection::MouseWheelIf(-WHEEL_DELTA,
                                           ValidateSequenceTarget,
                                           &sequenceTarget);
    }
    return 0;
}

static bool InjectSpaceDown(const target_platform::TargetIdentity& sequenceTarget) {
    if (s_injectedSpaceState) return true;
    const BhopInjectionResult result =
        s_injectionQueue.Submit(BhopInjectionKind::SpaceDown, sequenceTarget);
    if (!result.executed || result.value != 1) return false;
    s_injectedSpaceState = true;
    s_injectedSpaceTarget = sequenceTarget;
    return true;
}

static bool InjectSpaceUp(const target_platform::TargetIdentity& sequenceTarget) {
    if (!s_injectedSpaceState) return true;
    const BhopInjectionResult result =
        s_injectionQueue.Submit(BhopInjectionKind::SpaceUp, sequenceTarget);
    if (!result.executed) return false;
    // An executed but rejected release is owned by injection's pending-release
    // queue. A cancelled request was never executed and retains local ownership.
    s_injectedSpaceState = false;
    s_injectedSpaceTarget = {};
    return result.value == 1;
}

static bool InjectWheelDown(const target_platform::TargetIdentity& sequenceTarget) {
    const BhopInjectionResult result =
        s_injectionQueue.Submit(BhopInjectionKind::WheelDown, sequenceTarget);
    return result.executed && result.value == 1;
}

static bool IsSequenceTargetActive(const target_platform::TargetIdentity& sequenceTarget) {
    return detail::IsExpectedTargetActive(
        sequenceTarget,
        [] { return target_platform::SampleStableForegroundIdentity(); },
        [] { return target_platform::GetCurrentIdentity(); });
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  SCROLL BURST (Mode 4)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static void ScrollBurst(const RuntimeConfig& cfg,
                        const target_platform::TargetIdentity& sequenceTarget) {
    if (!InjectWheelDown(sequenceTarget)) return;
    PrecisionWait(cfg.spamIntervalMs);
    (void)InjectWheelDown(sequenceTarget);
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  DISPATCH JUMP
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static void DispatchJump(const RuntimeConfig& cfg,
                         const target_platform::TargetIdentity& sequenceTarget,
                         int& seqCount) {
    // [BUG #4] Receive snapshot of RuntimeConfig to guarantee timing/generation consistency
    int modeIdx = cfg.bhopMode;
    if (modeIdx < 1 || modeIdx > 4) modeIdx = 4;
    const auto& mt = cfg.modeCfg[modeIdx];

    double holdT = 0.0, delayT = 0.0;

    if (modeIdx == (int)Mode::Humanized) {
        double hMid = (mt.hMin + mt.hMax) / 2.0;
        double dMid = (mt.dMin + mt.dMax) / 2.0;
        double hStd = (mt.hMax - mt.hMin) / 4.0;
        double dStd = (mt.dMax - mt.dMin) / 4.0;

        holdT  = Clamp(GaussianRandom(hMid, hStd), mt.hMin, mt.hMax);
        delayT = Clamp(GaussianRandom(dMid, dStd), mt.dMin, mt.dMax);

        double drift = (seqCount > 4) ? (seqCount * 0.35) : 0;
        delayT = Clamp(delayT + drift + RandF(-1.5, 1.5),
                       mt.dMin, mt.dMax + 8.0);
    } else {
        holdT  = RandI(mt.hMin, mt.hMax);
        delayT = RandI(mt.dMin, mt.dMax);
    }

    double compDelay = std::max(1.0, delayT - s_jitterAccum);

    // [BUG #5] Enforce active foreground target verification right before input dispatching
    if (!s_spaceHeld.load() || !IsSequenceTargetActive(sequenceTarget)) return;

    if (modeIdx == (int)Mode::ScrollEmu) {
        ScrollBurst(cfg, sequenceTarget);
        PrecisionWait(compDelay);
    } else {
        if (!InjectSpaceDown(sequenceTarget)) return;
        PrecisionWait(holdT);
        
        if (!IsSequenceTargetActive(sequenceTarget)) {
            (void)InjectSpaceUp(sequenceTarget);
            return;
        }
        if (!InjectSpaceUp(sequenceTarget)) return;
        
        if (!IsSequenceTargetActive(sequenceTarget)) return;
        PrecisionWait(1);
        PrecisionWait(compDelay);
    }

    seqCount++;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  AIRBORNE WAIT â€” checks s_spaceHeld periodically
//  Returns false if Space was released (should exit loop).
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static bool AirborneWait(const RuntimeConfig& cfg,
                         const target_platform::TargetIdentity& sequenceTarget) {
    int remaining = cfg.airborneLockMs;
    while (remaining > 0 && s_running.load()) {
        int chunk = std::min(remaining, 10);  // 10ms chunks
        PrecisionWait(chunk);
        remaining -= chunk;

        // [BUG #5] Check active target focus to break out instantly if focus is lost
        if (!s_spaceHeld.load() || s_waitingForSpaceRepress.load(std::memory_order_relaxed) ||
            !IsSequenceTargetActive(sequenceTarget))
            return false;
    }
    return s_spaceHeld.load() && !s_waitingForSpaceRepress.load(std::memory_order_relaxed) && 
           IsSequenceTargetActive(sequenceTarget);
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  WORKER THREAD â€” bhop state machine loop
//  Sleeps on CV when idle, wakes on Space press, runs bhop loop,
//  then goes back to sleep. Never touches the hook thread.
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static void BhopThreadFunc() {
    // [FIX ID 14] Seed RNG on this thread â€” rand() uses TLS on MSVC
    srand((unsigned)QpcNow());

    topology::PinCriticalThread(L"Pro Audio");

    while (true) {
        // â”€â”€ Sleep until Space is pressed â”€â”€
        {
            std::unique_lock<std::mutex> lock(s_mutex);
            s_cv.wait(lock, [] {
                return (s_spaceHeld.load() && rcfg::Get().bhopEnabled && !s_waitingForSpaceRepress.load()) || !s_running.load();
            });
        }

        if (!s_running.load(std::memory_order_relaxed)) break;
        
        // Snapshot the full identity so HWND reuse cannot retarget an active sequence.
        const auto sequenceTarget = target_platform::GetCurrentIdentity();
        RuntimeConfig jumpCfg = rcfg::Get();
        if (!sequenceTarget.IsValid() || !IsSequenceTargetActive(sequenceTarget) ||
            !jumpCfg.bhopEnabled || !s_spaceHeld.load(std::memory_order_relaxed) ||
            s_waitingForSpaceRepress.load(std::memory_order_relaxed)) continue;

        DLOG_INFO(Runtime, "Bhop: sequence start");

        // â”€â”€ Bhop state machine loop â”€â”€
        int seqCount = 0;
        int64_t landingScanStartTick = 0;
#if MARCO_ENABLE_FORENSIC
        int64_t lastProgressTick = QpcNow();
#endif
        s_state.store(State::JumpStart, std::memory_order_relaxed);
        s_jitterAccum = 0.0;


        while (s_spaceHeld.load() && s_running.load() && !s_waitingForSpaceRepress.load(std::memory_order_relaxed)) {
            // [BUG #BP-1] Passive target focus check inside loop
            if (!IsSequenceTargetActive(sequenceTarget)) {
                DLOG_WARN(Runtime, "Bhop: Focus lost/changed during sequence. Aborting jump thread injection.");
                s_spaceHeld.store(false, std::memory_order_relaxed);
#if MARCO_ENABLE_FORENSIC
                telemetry::ForensicEvent ev;
                ev.type = telemetry::ForensicTrapType::BHOP_ABORTED;
                ev.threadId = GetCurrentThreadId();
                ev.timestampUs = timing::NowUs();
                ev.reasonCode = 1;
                ev.extraData1 = s_spaceHeld.load();
                ev.extraData2 = s_injectedSpaceState;
                ev.focus = false;
                telemetry::g_forensicBuffer.Push(ev);
#endif
                break;
            }

#if MARCO_ENABLE_FORENSIC
            int64_t nowTick = QpcNow();
            if (nowTick - lastProgressTick > StallThresholdTicks(jumpCfg)) {
                telemetry::ForensicEvent ev;
                ev.type = telemetry::ForensicTrapType::BHOP_STALL;
                ev.threadId = GetCurrentThreadId();
                ev.timestampUs = timing::NowUs();
                ev.reasonCode = (int32_t)s_state.load(std::memory_order_relaxed);
                ev.extraData1 = s_spaceHeld.load();
                ev.extraData2 = s_injectedSpaceState;
                ev.focus = true;
                telemetry::g_forensicBuffer.Push(ev);
                lastProgressTick = nowTick;
            }
#endif

            switch (s_state) {
                case State::JumpStart:
                {
#if MARCO_ENABLE_FORENSIC
                    int beforeSeq = seqCount;
#endif
                    DispatchJump(jumpCfg, sequenceTarget, seqCount);
#if MARCO_ENABLE_FORENSIC
                    if (seqCount != beforeSeq) {
                        lastProgressTick = QpcNow();
                    }
#endif
                    s_state.store(State::AirborneLock, std::memory_order_relaxed);
                    break;
                }

                case State::AirborneLock:
                    if (!AirborneWait(jumpCfg, sequenceTarget)) goto done;
#if MARCO_ENABLE_FORENSIC
                    lastProgressTick = QpcNow();
#endif
                    s_state.store(State::LandingScan, std::memory_order_relaxed);
                    landingScanStartTick = QpcNow();
                    break;

                case State::LandingScan: {
#if MARCO_ENABLE_FORENSIC
                    int beforeSeq = seqCount;
#endif
                    DispatchJump(jumpCfg, sequenceTarget, seqCount);
#if MARCO_ENABLE_FORENSIC
                    if (seqCount != beforeSeq) {
                        lastProgressTick = QpcNow();
                    }
#endif

                    double landingDuration = (double)jumpCfg.landingScanMs;
                    int64_t nowTick = QpcNow();
                    double elapsedMs = (double)(nowTick - landingScanStartTick) * 1000.0 / (double)s_qpcFreq;
                    if (elapsedMs >= landingDuration) {
                        s_state.store(State::AirborneLock, std::memory_order_relaxed);
#if MARCO_ENABLE_FORENSIC
                        lastProgressTick = nowTick;
#endif
                        seqCount = 0; // Reset seq for the new jump rhythm
                    }
                    break;
                }

                default:
                    goto done;
            }
        }

    done:
        (void)InjectSpaceUp(sequenceTarget);
        s_state.store(State::Idle, std::memory_order_relaxed);
        DLOG_INFO(Runtime, "Bhop: sequence end [%d jumps]", seqCount);
    }
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  PUBLIC API
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static std::atomic<bool> s_initialized{false};

void Init(HWND injectionWindow) {
    const DWORD currentThreadId = GetCurrentThreadId();
    if (injectionWindow == nullptr ||
        GetWindowThreadProcessId(injectionWindow, nullptr) != currentThreadId ||
        s_initialized.exchange(true)) {
        return;
    }
    s_injectionOwnerThreadId.store(currentThreadId, std::memory_order_release);
    s_injectionWindow.store(injectionWindow, std::memory_order_release);
    s_injectionQueue.Start(WakeInjectionOwner, nullptr);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    s_qpcFreq = freq.QuadPart;
    s_qpcToMs = 1000.0 / (double)s_qpcFreq;

    // Load NtDelayExecution
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        s_ntDelay = reinterpret_cast<NtDelayExecutionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "NtDelayExecution")));
    }

    // Start worker thread
    s_running.store(true);
    s_workerThread = std::thread(BhopThreadFunc);

    DLOG_INFO(Runtime, "Bhop engine initialized [QPCFreq=%lld, NtDelay=%s]",
              s_qpcFreq, (s_ntDelay ? "OK" : "FALLBACK"));
}

void Shutdown() {
    if (!s_initialized.load(std::memory_order_acquire)) return;
    if (GetCurrentThreadId() !=
        s_injectionOwnerThreadId.load(std::memory_order_acquire)) {
        DLOG_ERR(Runtime, "Bhop shutdown rejected: caller is not injection owner");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_running.store(false);
    }
    s_injectionQueue.Stop();
    s_cv.notify_all();
    if (s_workerThread.joinable())
        s_workerThread.join();
    if (s_injectedSpaceState) {
        (void)injection::SpaceUpForTarget(s_injectedSpaceTarget);
        s_injectedSpaceState = false;
        s_injectedSpaceTarget = {};
    }
    s_injectionWindow.store(nullptr, std::memory_order_release);
    s_injectionOwnerThreadId.store(0, std::memory_order_release);
    s_initialized.store(false, std::memory_order_release);
    DLOG_INFO(Runtime, "Bhop engine shutdown");
}

void DrainInjectionRequests() {
    if (GetCurrentThreadId() !=
        s_injectionOwnerThreadId.load(std::memory_order_acquire)) {
        return;
    }
    s_injectionQueue.Drain(ExecuteInjectionOnOwnerThread, nullptr);
}

Mode        GetMode()      { return (Mode)rcfg::Get().bhopMode; }
State       GetState()     { return s_state.load(std::memory_order_relaxed); } // [FIX R-6] Atomic load
bool        IsWaitingForSpaceRepress() { return s_waitingForSpaceRepress.load(std::memory_order_relaxed); }
const char* GetModeName()  { 
    int m = rcfg::Get().bhopMode;
    return s_modeNames[(m >= 1 && m <= 4) ? m : 4]; 
}
const char* GetStateName() { return s_stateNames[(int)s_state.load(std::memory_order_relaxed)]; }

void ToggleEnabled() {
    RuntimeConfig cfg = rcfg::GetMutable();
    cfg.bhopEnabled = !cfg.bhopEnabled;
    bool isNowEnabled = cfg.bhopEnabled;
    rcfg::Apply(cfg);

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!isNowEnabled) {
            // Turning off: flag that we are waiting for repress to safely break the loop
            s_waitingForSpaceRepress.store(true, std::memory_order_relaxed);
        } else {
            // If turning on while space is held, require a repress
            if (s_spaceHeld.load(std::memory_order_relaxed)) {
                s_waitingForSpaceRepress.store(true, std::memory_order_relaxed);
            }
        }
    }
    s_cv.notify_all();  // Wake worker to re-check predicate
    DLOG_WARN(Runtime, "Bhop %s", (isNowEnabled ? "ENABLED" : "DISABLED"));
}

void CycleMode() {
    RuntimeConfig cfg = rcfg::GetMutable();
    int m = cfg.bhopMode;
    m = (m >= (int)Mode::COUNT) ? 1 : m + 1;
    cfg.bhopMode = m;
    rcfg::Apply(cfg);
    config_io::Save(cfg);
    DLOG_INFO(Runtime, "Bhop mode: %s", s_modeNames[m]);
}

void OnSuspendChanged() {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_waitingForSpaceRepress.store(engine::GetState().spacePhys, std::memory_order_relaxed);
    }
    s_cv.notify_all(); // Wake worker to check engine::IsSuspended()
}

// â”€â”€ Input signals (called from hook thread â€” returns instantly) â”€â”€

void OnSpaceDown() {
    s_waitingForSpaceRepress.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_spaceHeld.store(true);
    }
    s_cv.notify_one();
}

void OnSpaceUp() {
    s_waitingForSpaceRepress.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_spaceHeld.store(false);
    }
    // Worker will see s_spaceHeld == false on next iteration
}

void ForceSpaceSync(bool isPhysicallyHeld) {
    if (isPhysicallyHeld) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_spaceHeld.store(true);
    } else {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_spaceHeld.store(false);
        s_waitingForSpaceRepress.store(false, std::memory_order_relaxed);
    }
    s_cv.notify_all();
}

} // namespace bhop
