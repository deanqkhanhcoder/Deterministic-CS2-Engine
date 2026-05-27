// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — State Engine Implementation             ║
// ║  Redesigned for Always-Track Physical Layer & Focus Reconciliation  ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "state_engine.h"
#include "engine_internal.h"
#include "runtime_state.h"
#include "runtime_config.h"
#include "bhop.h"
#include "movement_reconstruction.h"
#include "telemetry.h"
#include "build_config.h"
#include "timing.h"
#include "injection.h"
#include "config.h"
#include "debug_logger.h"
#include "input_capture.h"
#include "target_platform.h"
#include "topology.h"
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <thread>
#include <mutex>

namespace engine {


static HWND  s_hwnd = nullptr;
static int64_t s_initTimeMs = 0;
static std::atomic<bool> s_stateDirty{false};
static std::atomic<bool> s_watchdogRunning{false};
static std::thread s_watchdogThread;

// --- Lock-Free Publication ---
struct EngineStatePublication {
    bool suspended;
    AxisState axisState[2];
    bool phys[4];
    bool logical[4];
    bool bundleActive;
    int64_t lastCounterMs;
};
constexpr size_t PUB_WORDS = (sizeof(EngineStatePublication) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
alignas(64) static std::atomic<uint64_t> s_pubBuffer[PUB_WORDS];
alignas(64) static std::atomic<uint32_t> s_pubSeq{0};


State s_state;
std::mutex s_stateMutex;
std::atomic<bool> s_suspendedAtomic{false};
bool s_hookInstalled = false;

void PublishEngineState() {
    EngineStatePublication pub;
    pub.suspended = s_state.suspended;
    pub.axisState[0] = s_state.axisState[0];
    pub.axisState[1] = s_state.axisState[1];
    for (int i = 0; i < 4; ++i) {
        pub.phys[i] = s_state.phys[i];
        pub.logical[i] = s_state.logical[i];
        pub.bundleActive = false;
    }
    pub.lastCounterMs = s_state.lastCounterMs;

    uint32_t seq = s_pubSeq.load(std::memory_order_relaxed);
    s_pubSeq.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    uint64_t buffer[PUB_WORDS] = {0};
    std::memcpy(buffer, &pub, sizeof(pub));
    for (size_t i = 0; i < PUB_WORDS; ++i) {
        s_pubBuffer[i].store(buffer[i], std::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order_release);
    s_pubSeq.store(seq + 2, std::memory_order_release);
}

std::atomic<int64_t> dbgLastHookUs{0};
std::atomic<int64_t> dbgLastNotifyUs{0};
std::atomic<int64_t> dbgLastRefreshUs{0};
std::atomic<int64_t> dbgLastRenderUs{0};
std::atomic<uint32_t> dbgEventSeq{0};
std::atomic<uint32_t> dbgPublishCount{0};
std::atomic<uint32_t> dbgRefreshCount{0};
std::atomic<uint32_t> dbgRenderCount{0};

void Init(HWND hwnd) {
    s_hwnd = hwnd;
    std::lock_guard<std::mutex> lock(s_stateMutex);
    s_state.Reset();
    s_initTimeMs = timing::NowMs();
    PublishEngineState();
}

State GetState() {
    std::lock_guard<std::mutex> lock(s_stateMutex);
    return s_state;
}
bool IsSuspended() { return s_suspendedAtomic.load(std::memory_order_acquire); }  // Thread-safe read

void NotifyUI() {
    dbgLastNotifyUs.store(timing::NowUs(), std::memory_order_relaxed);
    dbgPublishCount.fetch_add(1, std::memory_order_relaxed);
    HWND target = s_hwnd;
    if (target && IsWindow(target)) {
        if (!s_stateDirty.exchange(true)) {
            PostMessage(target, WM_STATE_DIRTY, 0, 0);
        }
    } else {
        s_stateDirty.store(true, std::memory_order_relaxed);
    }
}

void ClearStateDirty() {
    s_stateDirty.store(false, std::memory_order_relaxed);
}

// ── Forward declarations ──
void RunWatchdog() {
    int64_t nowMs = timing::NowMs();
    (void)nowMs;
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        // Watchdog relies on standard Reconcile logic if needed
        PublishEngineState();
    }
    batch.flush();
}

// ── SUSPEND / RESUME / FOCUS ──
void StartWatchdog() {
    s_watchdogRunning.store(true, std::memory_order_relaxed);
    int64_t now = timing::NowMs();
    telemetry::g_heartbeatTiming.store(now, std::memory_order_relaxed);
    telemetry::g_heartbeatHook.store(now, std::memory_order_relaxed);
    telemetry::g_heartbeatScanner.store(now, std::memory_order_relaxed);
    telemetry::g_heartbeatTelemetry.store(now, std::memory_order_relaxed);
    s_watchdogThread = std::thread([]() {
        topology::PinBackgroundThread();
        
        while (s_watchdogRunning.load(std::memory_order_relaxed)) {
            Sleep(100);
            
            telemetry::g_heartbeatTelemetry.store(timing::NowMs(), std::memory_order_relaxed);
            
            int64_t now = timing::NowMs();
            
            bool timingStalled = false;
            if (!telemetry::g_blockedTiming.load(std::memory_order_relaxed)) {
                int64_t diff = now - telemetry::g_heartbeatTiming.load(std::memory_order_relaxed);
                if (diff > 1000) timingStalled = true;
            }
            
            bool hookStalled = false;
            if (!telemetry::g_blockedHook.load(std::memory_order_relaxed)) {
                int64_t diff = now - telemetry::g_heartbeatHook.load(std::memory_order_relaxed);
                if (diff > 1000) hookStalled = true;
            }
            
            bool scannerStalled = false;
            if (!telemetry::g_blockedScanner.load(std::memory_order_relaxed)) {
                int64_t diff = now - telemetry::g_heartbeatScanner.load(std::memory_order_relaxed);
                if (diff > 5000) scannerStalled = true;
            }
            
            bool corruption = false;
            static int corruptionCounter[4] = {0};
            {
                std::lock_guard<std::mutex> lock(s_stateMutex);
                for (int i = 0; i < 4; i++) {
                    bool logicalVal = s_state.logical[i];
                    bool physVal = s_state.phys[i];
                    // In offline playback, we don't have timers running.
                    if (logicalVal && !physVal) {
                        corruptionCounter[i]++;
                        if (corruptionCounter[i] >= 10) {
                            corruption = true;
                        }
                    } else {
                        corruptionCounter[i] = 0;
                    }
                }
            }
            
        uint32_t triggers = 0;
            if (timingStalled) triggers |= 1;
            if (hookStalled)   triggers |= 2;
            if (scannerStalled) triggers |= 4;
            if (corruption)    triggers |= 16;
            
            telemetry::g_failSafeTriggers.store(triggers, std::memory_order_relaxed);
            
            if (triggers != 0) {
                // Exchange to only trigger emergency flush on state transition, mitigating loop storm
                uint32_t prevState = telemetry::g_watchdogState.exchange(2, std::memory_order_relaxed);
                if (prevState != 2) {
                    TriggerEmergencyFlush();
                }
            } else {
                telemetry::g_watchdogState.store(0, std::memory_order_relaxed);
            }
        }
    });
}

void StopWatchdog() {
    s_watchdogRunning.store(false, std::memory_order_relaxed);
    if (s_watchdogThread.joinable()) {
        s_watchdogThread.join();
    }
}

void TriggerEmergencyFlush() {
    static std::atomic<bool> s_inFlush{false};
    if (s_inFlush.exchange(true)) return;
    
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        for (int i = 0; i < 4; i++) {
            Key k = static_cast<Key>(i);
            batch.push(k, false);
            s_state.logical[i] = false;
            timing::CancelTimer(k);
        s_state.expectedTimerId[ki(k)] = 0;
        }
        PublishEngineState();
    }
    batch.flush();
    INPUT inpSpace = {};
    inpSpace.type = INPUT_KEYBOARD;
    inpSpace.ki.wVk = VK_SPACE;
    inpSpace.ki.wScan = 0x39;
    inpSpace.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &inpSpace, sizeof(INPUT));
    bhop::OnSpaceUp();
    
    // Defer Uninstall to main thread to avoid UnhookWindowsHookEx deadlock!
    if (s_hwnd) {
        PostMessage(s_hwnd, WM_EMERGENCY_UNHOOK, 0, 0);
    }
    
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        s_state.suspended = true;
        s_suspendedAtomic.store(true, std::memory_order_release);
        PublishEngineState();
    }
    
    telemetry::g_recoveryCount.fetch_add(1, std::memory_order_relaxed);
    
    s_inFlush.store(false);
}

bool IsTimingWorkloadActive() {
    if (bhop::GetState() != bhop::State::Idle) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        if (timing::AreTimersActive()) return true;
    }
    if (timing::AreTimersActive()) {
        return true;
    }
    return false;
}

void TakeSnapshot(RuntimeSnapshot& out) {
    EngineStatePublication pub;
    uint64_t buffer[PUB_WORDS];
    uint32_t seq0, seq1;
    int spin_count = 0;
    while (true) {
        seq0 = s_pubSeq.load(std::memory_order_acquire);
        if (seq0 & 1) {
            if (spin_count < 64) _mm_pause();
            else if (spin_count < 1024) std::this_thread::yield();
            else std::this_thread::sleep_for(std::chrono::microseconds(10));
            spin_count++;
            continue;
        }
        for (size_t i = 0; i < PUB_WORDS; ++i) {
            buffer[i] = s_pubBuffer[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acq_rel);
        seq1 = s_pubSeq.load(std::memory_order_relaxed);
        if (seq0 == seq1) break;
        spin_count++;
    }
    std::memcpy(&pub, buffer, sizeof(pub));

    out.suspended = pub.suspended;
    for (int i = 0; i < 2; i++) out.axisState[i] = pub.axisState[i];
    for (int i = 0; i < 4; i++) {
        out.phys[i] = pub.phys[i];
        out.logical[i] = pub.logical[i];
        out.bundleActive = pub.bundleActive;
    }
    out.lastCounterMs = pub.lastCounterMs;

    out.bhopEnabled = rcfg::Get().bhopEnabled;
    out.bhopMode = bhop::GetMode();
    out.bhopState = bhop::GetState();
    out.hookInstalled = s_hookInstalled;
    out.targetActive = capture::IsTargetActiveForUI();
    out.activeCapabilities = target_platform::GetActiveCapabilities();
    out.waitingForSpaceRepress = bhop::IsWaitingForSpaceRepress();
    target_platform::GetActiveTargetName(out.targetName, 32);
    out.uptimeMs = timing::NowMs() - s_initTimeMs;
    out.runningGamesMask = target_platform::GetRunningGamesMask();

    // Movement Telemetry
    const RuntimeConfig& cfg = rcfg::Get();
    out.subTickCompressionActive = (cfg.brakeProfiles[cfg.activeBrakeProfileIndex].overlap_duration_us == 0);
    out.overlapAccuracyUs = 0; // Filled later from timerJitterUs
    switch(cfg.activeBrakeProfileIndex) {
        case 1: wcscpy_s(out.activeBrakeProfileName, L"RIFLE"); break;
        case 2: wcscpy_s(out.activeBrakeProfileName, L"PISTOL"); break;
        case 3: wcscpy_s(out.activeBrakeProfileName, L"SNIPER"); break;
        case 4: wcscpy_s(out.activeBrakeProfileName, L"SMG"); break;
        default: wcscpy_s(out.activeBrakeProfileName, L"NONE"); break;
    }
    out.profileOverlapUs = cfg.brakeProfiles[cfg.activeBrakeProfileIndex].overlap_duration_us;
    out.profileBrakeBias = cfg.brakeProfiles[cfg.activeBrakeProfileIndex].brake_bias_multiplier;
    out.profileAuthorityBiasMs = cfg.brakeProfiles[cfg.activeBrakeProfileIndex].authority_bias_ms;
    out.profileAggrCurve = cfg.brakeProfiles[cfg.activeBrakeProfileIndex].aggressiveness_curve;

    // Instrumentation Metrics
    out.dbgLastHookUs = dbgLastHookUs.load(std::memory_order_relaxed);
    out.dbgLastNotifyUs = dbgLastNotifyUs.load(std::memory_order_relaxed);
    out.dbgLastRefreshUs = dbgLastRefreshUs.load(std::memory_order_relaxed);
    out.dbgLastRenderUs = dbgLastRenderUs.load(std::memory_order_relaxed);
    out.dbgEventSeq = dbgEventSeq.load(std::memory_order_relaxed);
    out.dbgPublishCount = dbgPublishCount.load(std::memory_order_relaxed);
    out.dbgRefreshCount = dbgRefreshCount.load(std::memory_order_relaxed);
    out.dbgRenderCount = dbgRenderCount.load(std::memory_order_relaxed);

    int64_t nowMs = timing::NowMs();

    // Workload-aware timing activity detection
    bool timingActive = IsTimingWorkloadActive();

    // Check if the timing engine has received new samples recently
    static int64_t lastActiveTimeMs = 0;
    if (timingActive) {
        lastActiveTimeMs = nowMs;
    }
    out.timingActive = timingActive;
    out.telemetryAgeMs = (lastActiveTimeMs == 0) ? -1 : (timingActive ? 0 : (nowMs - lastActiveTimeMs));

    // Determine state machine state
    RuntimeState rState = RuntimeState::Detached;
#if MARCO_ENABLE_WATCHDOG
    uint32_t fsTriggers = telemetry::g_failSafeTriggers.load(std::memory_order_relaxed);
    uint32_t wdState = telemetry::g_watchdogState.load(std::memory_order_relaxed);
    if (fsTriggers != 0 || wdState == 2) {
        rState = RuntimeState::FailSafe;
    } else if (timingActive) {
#else
    if (timingActive) {
#endif
        rState = RuntimeState::ActiveTiming;
        rState = RuntimeState::ActiveTiming;
    } else if (out.runningGamesMask != 0 && out.hookInstalled) {
        rState = RuntimeState::Attached;
    }
    out.runtimeState = rState;

    uint32_t tGroup = telemetry::g_activeTimingGroup.load(std::memory_order_relaxed);
    uint32_t tCore = telemetry::g_activeTimingCore.load(std::memory_order_relaxed);
    uint32_t hGroup = telemetry::g_activeHookGroup.load(std::memory_order_relaxed);
    uint32_t hCore = telemetry::g_activeHookCore.load(std::memory_order_relaxed);
    out.activeTimingCore = tCore;
    out.activeHookCore = hCore;
    out.smtCollision = (tGroup != 0xFFFFFFFF && hGroup != 0xFFFFFFFF) && topology::AreSmtSiblings(tGroup, tCore, hGroup, hCore);
    out.affinityMode = telemetry::g_affinityMode.load(std::memory_order_relaxed);

#if MARCO_ENABLE_TELEMETRY
    // Persistent static caches for timing telemetry
    static int64_t cachedTimerJitterUs = 0;
    static int64_t cachedWakeOversleepUs = 0;
    static int64_t cachedSpinDurationUs = 0;
    static uint32_t cachedHistJitter[6] = {0};
    static uint32_t cachedHistOversleep[6] = {0};

    static int64_t cachedTimelineJitter[RuntimeSnapshot::TIMELINE_SIZE] = {0};
    static int64_t cachedTimelineOversleep[RuntimeSnapshot::TIMELINE_SIZE] = {0};
    static bool cachedTimelineSpike[RuntimeSnapshot::TIMELINE_SIZE] = {false};
    static int cachedTimelineIndex = 0;

    // Latency Metrics
    int64_t hookP50 = 0, hookP99 = 0, hookAvg = 0;
    telemetry::g_hookLatency.GetStats(hookP50, hookP99, hookAvg);
    out.hookLatencyP50Us = hookP50;
    out.hookLatencyP99Us = hookP99;

    if (timingActive) {
        int64_t jitterP50 = 0, jitterP99 = 0, jitterAvg = 0;
        int64_t oversleepP50 = 0, oversleepP99 = 0, oversleepAvg = 0;
        int64_t spinP50 = 0, spinP99 = 0, spinAvg = 0;

        telemetry::g_timerJitter.GetStats(jitterP50, jitterP99, jitterAvg);
        telemetry::g_oversleep.GetStats(oversleepP50, oversleepP99, oversleepAvg);
        telemetry::g_spinDuration.GetStats(spinP50, spinP99, spinAvg);

        cachedTimerJitterUs = jitterP50;
        cachedWakeOversleepUs = oversleepP50;
        cachedSpinDurationUs = spinP50;
    }

    out.timerJitterUs = cachedTimerJitterUs;
    out.wakeOversleepUs = cachedWakeOversleepUs;
    out.spinDurationUs = cachedSpinDurationUs;
    out.overlapAccuracyUs = cachedTimerJitterUs; // overlap accuracy matches timer jitter

    int64_t stateMutationP50 = 0, stateMutationP99 = 0, stateMutationAvg = 0;
    telemetry::g_stateMutation.GetStats(stateMutationP50, stateMutationP99, stateMutationAvg);
    out.stateMutationLatencyUs = stateMutationP50;

    out.schedulerSpikeCount = telemetry::g_schedulerSpikes.load(std::memory_order_relaxed);
    out.coreMigrationCount = telemetry::g_coreMigrations.load(std::memory_order_relaxed);
    out.timerOversleepPeakUs = telemetry::g_timerOversleepPeak.load(std::memory_order_relaxed);
    out.wakeVarianceUs = telemetry::g_wakeVarianceUs.load(std::memory_order_relaxed);

    // Maintain rolling timeline in TakeSnapshot
    if (timingActive) {
        int64_t jitterAvgTenths = telemetry::g_timerJitter.GetAverageTenths();
        int64_t oversleepAvgTenths = telemetry::g_oversleep.GetAverageTenths();

        static uint32_t lastConsumedOversleepIdx = 0;
        uint32_t currOversleepIdx = telemetry::g_oversleep.index.load(std::memory_order_relaxed);
        bool spikeInWindow = false;

        static bool firstSampleCheck = true;
        if (firstSampleCheck) {
            lastConsumedOversleepIdx = currOversleepIdx;
            firstSampleCheck = false;
        }

        if (currOversleepIdx != lastConsumedOversleepIdx) {
            uint32_t start = lastConsumedOversleepIdx;
            uint32_t end = currOversleepIdx;
            lastConsumedOversleepIdx = currOversleepIdx;

            for (uint32_t idx = start; idx < end; idx++) {
                int64_t val = telemetry::g_oversleep.samples[idx % telemetry::MetricBuffer::SIZE];
                if (val > 50) { // >50us late is a scheduler spike
                    spikeInWindow = true;
                    break;
                }
            }
        }

        cachedTimelineJitter[cachedTimelineIndex] = jitterAvgTenths;
        cachedTimelineOversleep[cachedTimelineIndex] = oversleepAvgTenths;
        cachedTimelineSpike[cachedTimelineIndex] = spikeInWindow;

        cachedTimelineIndex = (cachedTimelineIndex + 1) % RuntimeSnapshot::TIMELINE_SIZE;
    }

    out.timelineIndex = cachedTimelineIndex;
    for (int i = 0; i < RuntimeSnapshot::TIMELINE_SIZE; i++) {
        out.timelineJitter[i] = cachedTimelineJitter[i];
        out.timelineOversleep[i] = cachedTimelineOversleep[i];
        out.timelineSpike[i] = cachedTimelineSpike[i];
    }

    // Histograms (Only iterate over the valid active samples in each ring buffer)
    memset(out.histHook, 0, sizeof(out.histHook));
    uint32_t hookIdx = telemetry::g_hookLatency.index.load(std::memory_order_relaxed);
    int hookLimit = hookIdx < telemetry::MetricBuffer::SIZE ? (int)hookIdx : telemetry::MetricBuffer::SIZE;
    for (int i = 0; i < hookLimit; i++) {
        int64_t val = telemetry::g_hookLatency.samples[i];
        if (val < 5) out.histHook[0]++;
        else if (val < 10) out.histHook[1]++;
        else if (val < 20) out.histHook[2]++;
        else if (val < 50) out.histHook[3]++;
        else if (val < 100) out.histHook[4]++;
        else out.histHook[5]++;
    }

    if (timingActive) {
        memset(cachedHistJitter, 0, sizeof(cachedHistJitter));
        uint32_t jitterIdx = telemetry::g_timerJitter.index.load(std::memory_order_relaxed);
        int jitterLimit = jitterIdx < telemetry::MetricBuffer::SIZE ? (int)jitterIdx : telemetry::MetricBuffer::SIZE;
        for (int i = 0; i < jitterLimit; i++) {
            int64_t val = telemetry::g_timerJitter.samples[i];
            if (val < 20) cachedHistJitter[0]++;
            else if (val < 50) cachedHistJitter[1]++;
            else if (val < 100) cachedHistJitter[2]++;
            else if (val < 250) cachedHistJitter[3]++;
            else if (val < 500) cachedHistJitter[4]++;
            else cachedHistJitter[5]++;
        }

        memset(cachedHistOversleep, 0, sizeof(cachedHistOversleep));
        uint32_t oversleepIdx = telemetry::g_oversleep.index.load(std::memory_order_relaxed);
        int oversleepLimit = oversleepIdx < telemetry::MetricBuffer::SIZE ? (int)oversleepIdx : telemetry::MetricBuffer::SIZE;
        for (int i = 0; i < oversleepLimit; i++) {
            int64_t val = telemetry::g_oversleep.samples[i];
            if (val < 50) cachedHistOversleep[0]++;
            else if (val < 100) cachedHistOversleep[1]++;
            else if (val < 250) cachedHistOversleep[2]++;
            else if (val < 500) cachedHistOversleep[3]++;
            else if (val < 1000) cachedHistOversleep[4]++;
            else cachedHistOversleep[5]++;
        }
    }

    memcpy(out.histJitter, cachedHistJitter, sizeof(out.histJitter));
    memcpy(out.histOversleep, cachedHistOversleep, sizeof(out.histOversleep));
#endif

    // Thread Health and Watchdog snapshots
#if MARCO_ENABLE_HEARTBEATS
    out.threadHealthTiming = (telemetry::g_blockedTiming.load(std::memory_order_relaxed) ||
                              (nowMs - telemetry::g_heartbeatTiming.load(std::memory_order_relaxed) < 1000));
    out.threadHealthHook = (telemetry::g_blockedHook.load(std::memory_order_relaxed) ||
                            (nowMs - telemetry::g_heartbeatHook.load(std::memory_order_relaxed) < 1000));
    out.threadHealthScanner = (telemetry::g_blockedScanner.load(std::memory_order_relaxed) ||
                               (nowMs - telemetry::g_heartbeatScanner.load(std::memory_order_relaxed) < 5000));
    out.threadHealthTelemetry = (telemetry::g_blockedTelemetry.load(std::memory_order_relaxed) ||
                                 (nowMs - telemetry::g_heartbeatTelemetry.load(std::memory_order_relaxed) < 2000));
#if MARCO_ENABLE_WATCHDOG
    out.watchdogState = telemetry::g_watchdogState.load(std::memory_order_relaxed);
    out.failSafeTriggers = telemetry::g_failSafeTriggers.load(std::memory_order_relaxed);
    out.recoveryCount = telemetry::g_recoveryCount.load(std::memory_order_relaxed);
#endif
#endif
}

void SetHookInstalled(bool v) { s_hookInstalled = v; NotifyUI(); }

} // namespace engine
