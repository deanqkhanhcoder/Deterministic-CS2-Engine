// â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
// â•‘  Counter-Strafe v25.3 C++ â€” State Engine Implementation             â•‘
// â•‘  Redesigned for Always-Track Physical Layer & Focus Reconciliation  â•‘
// â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

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


// --- Lock-Free Publication ---
struct EngineStatePublication {
    bool suspended;
    AxisState axisState[2];
    bool phys[4];
    bool logical[4];
    bool bundleActive;

};
constexpr size_t PUB_WORDS = (sizeof(EngineStatePublication) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
struct alignas(64) PublishedState {
    std::atomic<uint32_t> seq{0};
    std::atomic<uint64_t> buffer[PUB_WORDS];
};
static PublishedState s_pubState;

static void ReadPublishedEngineState(EngineStatePublication& pub) {
    uint64_t buffer[PUB_WORDS];
    uint32_t seq0, seq1;
    int spinCount = 0;
    while (true) {
        seq0 = s_pubState.seq.load(std::memory_order_acquire);
        if (seq0 & 1) {
            if (spinCount < 64) _mm_pause();
            else std::this_thread::yield();
            spinCount++;
            continue;
        }
        for (size_t i = 0; i < PUB_WORDS; ++i) {
            buffer[i] = s_pubState.buffer[i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acq_rel);
        seq1 = s_pubState.seq.load(std::memory_order_relaxed);
        if (seq0 == seq1) break;
        spinCount++;
    }
    std::memcpy(&pub, buffer, sizeof(pub));
}


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

#if MARCO_ENABLE_FORENSIC
        if (s_state.phys[i] != s_state.logical[i]) {
            telemetry::ForensicEvent ev;
            ev.type = telemetry::ForensicTrapType::LOGICAL_PHYSICAL_DIVERGENCE;
            ev.threadId = GetCurrentThreadId();
            ev.timestampUs = timing::NowUs();
            ev.reasonCode = i; // key index
            ev.extraData1 = s_state.phys[i];
            ev.extraData2 = s_state.logical[i];
            ev.focus = s_state.spacePhys; // reusing spacePhys as dummy since we don't have focus here easily, or just targetActive
            telemetry::g_forensicBuffer.Push(ev);
        }
#endif
    }

#if MARCO_ENABLE_FORENSIC
    static int64_t conflictStartUs[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        if (s_state.axisState[i] == AxisState::Conflict) {
            if (conflictStartUs[i] == 0) conflictStartUs[i] = timing::NowUs();
            else if (timing::NowUs() - conflictStartUs[i] > 500000) { // 500 ms stall
                telemetry::ForensicEvent ev;
                ev.type = telemetry::ForensicTrapType::COUNTERSTRAFE_CONFLICT;
                ev.threadId = GetCurrentThreadId();
                ev.timestampUs = timing::NowUs();
                ev.reasonCode = i; // axis
                ev.extraData1 = (s_state.phys[i*2] | (s_state.phys[i*2+1] << 1));
                ev.extraData2 = (s_state.logical[i*2] | (s_state.logical[i*2+1] << 1));
                ev.focus = true;
                telemetry::g_forensicBuffer.Push(ev);
                conflictStartUs[i] = timing::NowUs(); // reset to avoid spam
            }
        } else {
            conflictStartUs[i] = 0;
        }
    }
#endif


    uint32_t seq = s_pubState.seq.load(std::memory_order_relaxed);
    s_pubState.seq.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    uint64_t buffer[PUB_WORDS] = {0};
    std::memcpy(buffer, &pub, sizeof(pub));
    for (size_t i = 0; i < PUB_WORDS; ++i) {
        s_pubState.buffer[i].store(buffer[i], std::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order_release);
    s_pubState.seq.store(seq + 2, std::memory_order_release);
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



bool IsTimingWorkloadActive() {
    if (bhop::GetState() != bhop::State::Idle) {
        return true;
    }
    return timing::AreTimersActive();
}

void TakeSnapshot(RuntimeSnapshot& out) {
    EngineStatePublication pub;
    ReadPublishedEngineState(pub);

    out.suspended = pub.suspended;
    for (int i = 0; i < 2; i++) out.axisState[i] = pub.axisState[i];
    for (int i = 0; i < 4; i++) {
        out.phys[i] = pub.phys[i];
        out.logical[i] = pub.logical[i];
        out.bundleActive = pub.bundleActive;
    }


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
    if (timingActive) {
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

    out.schedulerSpikeCount = telemetry::g_schedulerSpikes.load(std::memory_order_relaxed);
    out.coreMigrationCount = telemetry::g_coreMigrations.load(std::memory_order_relaxed);
    out.timerOversleepPeakUs = telemetry::g_timerOversleepPeak.load(std::memory_order_relaxed);
    out.wakeVarianceUs = telemetry::g_wakeVarianceUs.load(std::memory_order_relaxed);

#if MARCO_ENABLE_FORENSIC
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


}

void SetHookInstalled(bool v) { s_hookInstalled = v; NotifyUI(); }

} // namespace engine
