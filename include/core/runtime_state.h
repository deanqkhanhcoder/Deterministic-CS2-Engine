#pragma once
// Runtime state snapshot for UI consumption (lock-free read)
#include "types.h"
#include "bhop.h"
#include "target_platform.h"

#include <cstdint>

enum class RuntimeState {
    Detached,
    Attached,
    ActiveTiming,
    FailSafe
};

struct RuntimeSnapshot {
    RuntimeState runtimeState;
    // Counter-strafe
    bool      suspended;
    AxisState axisState[2];
    bool      phys[4];
    bool      logical[4];
    bool      bundleActive;


    // Bhop
    bool       bhopEnabled;
    bhop::Mode bhopMode;
    bhop::State bhopState;
    bool hookInstalled;
    bool targetActive;
    uint32_t activeCapabilities;
    bool waitingForSpaceRepress;
    wchar_t targetName[32];
    int64_t uptimeMs;
    uint32_t runningGamesMask;
    bool      timingActive;
    int64_t   telemetryAgeMs;

    // Instrumentation Metrics
    int64_t   dbgLastHookUs;
    int64_t   dbgLastNotifyUs;
    int64_t   dbgLastRefreshUs;
    int64_t   dbgLastRenderUs;
    uint32_t  dbgEventSeq;
    uint32_t  dbgPublishCount;
    uint32_t  dbgRefreshCount;
    uint32_t  dbgRenderCount;

    // Extended Platform Observability Metrics
    int64_t   hookLatencyP50Us;
    int64_t   hookLatencyP99Us;
    int64_t   timerJitterUs;
    int64_t   wakeOversleepUs;
    int64_t   spinDurationUs;
    int64_t   stateMutationLatencyUs;

    uint32_t  schedulerSpikeCount;
    uint32_t  coreMigrationCount;
    int64_t   wakeVarianceUs;
    int64_t   timerOversleepPeakUs;


    int64_t   overlapAccuracyUs;
    wchar_t   activeBrakeProfileName[32];
    int64_t   profileOverlapUs;
    double    profileBrakeBias;
    double    profileAuthorityBiasMs;
    double    profileAggrCurve;

    uint32_t  activeTimingCore;
    uint32_t  activeHookCore;
    bool      smtCollision;
    uint32_t  affinityMode;

    uint32_t  failSafeTriggers;
    uint32_t  recoveryCount;

    // Rolling timeline graphs
    static constexpr int TIMELINE_SIZE = 120;
    int64_t   timelineJitter[TIMELINE_SIZE];
    int64_t   timelineOversleep[TIMELINE_SIZE];
    bool      timelineSpike[TIMELINE_SIZE];
    int       timelineIndex;

    // Histogram bins
    uint32_t  histHook[6];
    uint32_t  histJitter[6];
    uint32_t  histOversleep[6];
};
