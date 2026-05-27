#include "telemetry.h"
#include "analysis_toolkit.h"
#include <windows.h>
#include <thread>
#include <algorithm>

#ifdef _MSC_VER
namespace telemetry {

// Define the provider instance
TRACELOGGING_DEFINE_PROVIDER(
    g_hProvider,
    "AntiGravity-Runtime",
    // {6a94f6c1-a20c-4bc3-a2bc-86e584f23e1b}
    (0x6a94f6c1, 0xa20c, 0x4bc3, 0xa2, 0xbc, 0x86, 0xe5, 0x84, 0xf2, 0x3e, 0x1b)
);

} // namespace telemetry
#endif

#include "build_config.h"

namespace telemetry {
#if MARCO_ENABLE_TELEMETRY
EventRingBuffer g_eventBuffer;

static std::thread s_telemetryThread;
static std::atomic<bool> s_telemetryRunning{false};

void StartTelemetryThread() {
    if (s_telemetryRunning.exchange(true)) return;
    s_telemetryThread = std::thread([]() {
        while (s_telemetryRunning.load(std::memory_order_relaxed)) {
            TraceEvent ev;
            int count = 0;
            while (g_eventBuffer.Pop(ev) && count < 100) {
                if (ev.type == 5) {
                    analysis::RecordEvent((analysis::EventType)ev.v1, ev.core, ev.v2);
                } else if (IsEnabled()) {
                    if (ev.type == 1) {
                        TraceLoggingWrite(g_hProvider, "Latency_Hook_Keyboard", TraceLoggingInt64(ev.v1, "DurationUs"), TraceLoggingUInt32(ev.v2, "VKCode"), TraceLoggingUInt64(ev.v3, "Action"), TraceLoggingUInt32(ev.core, "CoreId"));
                    } else if (ev.type == 2) {
                        TraceLoggingWrite(g_hProvider, "Latency_Hook_Mouse", TraceLoggingInt64(ev.v1, "DurationUs"), TraceLoggingUInt64(ev.v2, "Action"), TraceLoggingUInt32(ev.core, "CoreId"));
                    } else if (ev.type == 3) {
                        TraceLoggingWrite(g_hProvider, "Latency_TimerWake", TraceLoggingInt64(ev.v1, "JitterUs"), TraceLoggingInt64(ev.v2, "SpinDurationUs"), TraceLoggingInt32(ev.v3, "Key"));
                    } else if (ev.type == 4) {
                        TraceLoggingWrite(g_hProvider, "Latency_StateMutation", TraceLoggingInt64(ev.v1, "DurationUs"), TraceLoggingInt32(ev.v2, "Key"), TraceLoggingInt32(ev.v3, "Op"));
                    }
                }
                count++;
            }
            Sleep(1);
        }
    });
}

void StopTelemetryThread() {
    if (!s_telemetryRunning.exchange(false)) return;
    if (s_telemetryThread.joinable()) s_telemetryThread.join();
}

alignas(64) MetricBuffer g_hookLatency;
alignas(64) MetricBuffer g_timerJitter;
alignas(64) MetricBuffer g_oversleep;
alignas(64) MetricBuffer g_spinDuration;
alignas(64) MetricBuffer g_stateMutation;

alignas(64) std::atomic<uint32_t> g_schedulerSpikes{0};
alignas(64) std::atomic<uint32_t> g_coreMigrations{0};
alignas(64) std::atomic<int64_t> g_timerOversleepPeak{0};
alignas(64) std::atomic<int64_t> g_wakeVarianceUs{0};

#endif // MARCO_ENABLE_TELEMETRY

alignas(64) std::atomic<uint32_t> g_activeTimingGroup{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_activeTimingCore{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_activeHookGroup{0xFFFFFFFF};
alignas(64) std::atomic<uint32_t> g_activeHookCore{0xFFFFFFFF};

#if MARCO_ENABLE_HEARTBEATS

alignas(64) std::atomic<int64_t> g_heartbeatTiming{0};
alignas(64) std::atomic<int64_t> g_heartbeatHook{0};
alignas(64) std::atomic<int64_t> g_heartbeatScanner{0};
alignas(64) std::atomic<int64_t> g_heartbeatTelemetry{0};

alignas(64) std::atomic<bool> g_blockedTiming{false};
alignas(64) std::atomic<bool> g_blockedHook{false};
alignas(64) std::atomic<bool> g_blockedBhop{false};
alignas(64) std::atomic<bool> g_blockedScanner{false};
alignas(64) std::atomic<bool> g_blockedTelemetry{false};

alignas(64) std::atomic<uint32_t> g_watchdogState{0};
alignas(64) std::atomic<uint32_t> g_failSafeTriggers{0};
alignas(64) std::atomic<uint32_t> g_recoveryCount{0};

#endif // MARCO_ENABLE_HEARTBEATS

alignas(64) std::atomic<uint32_t> g_affinityMode{1}; // Default to Balanced (1). 0 = Competitive, 2 = Low CPU

} // namespace telemetry
