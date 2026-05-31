#include "telemetry.h"
#include "analysis_toolkit.h"
#include "workspace.h"
#include <windows.h>
#include <thread>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <vector>

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
#if MARCO_ENABLE_FORENSIC
EventRingBuffer g_eventBuffer;
ForensicRingBuffer g_forensicBuffer;

std::atomic<uint64_t> g_timersCreated{0};
std::atomic<uint64_t> g_timersExecuted{0};
std::atomic<uint64_t> g_timersCancelled{0};

#include <stdio.h>
#include <time.h>
#include <string>

static const char* ForensicTrapName(ForensicTrapType type) {
    switch (type) {
        case ForensicTrapType::FOCUS_LOST: return "FOCUS_LOST";
        case ForensicTrapType::FOCUS_GAINED: return "FOCUS_GAINED";
        case ForensicTrapType::PROFILE_CHANGED: return "PROFILE_CHANGED";
        case ForensicTrapType::COUNTERSTRAFE_CANCELLED: return "COUNTERSTRAFE_CANCELLED";
        case ForensicTrapType::COUNTERSTRAFE_CONFLICT: return "COUNTERSTRAFE_CONFLICT";
        case ForensicTrapType::BHOP_ABORTED: return "BHOP_ABORTED";
        case ForensicTrapType::BHOP_STALL: return "BHOP_STALL";
        case ForensicTrapType::TIMER_REJECTED: return "TIMER_REJECTED";
        case ForensicTrapType::LOGICAL_PHYSICAL_DIVERGENCE: return "LOGICAL_PHYSICAL_DIVERGENCE";
        default: return "UNKNOWN";
    }
}

static const char* BuildTypeName() {
#if defined(MARCO_RELEASE)
    return "Release";
#elif defined(MARCO_PROFILE)
    return "Profile";
#else
    return "Debug";
#endif
}

static std::string MakeDefaultForensicLogPath() {
    workspace::EnsureLogDirectoryExists();

    SYSTEMTIME st;
    GetLocalTime(&st);
    char name[96];
    snprintf(name, sizeof(name), "marco_%04u-%02u-%02u_%02u-%02u-%02u.log",
             (unsigned)st.wYear,
             (unsigned)st.wMonth,
             (unsigned)st.wDay,
             (unsigned)st.wHour,
             (unsigned)st.wMinute,
             (unsigned)st.wSecond);

    return workspace::GetLogRootA() + name;
}

void ForensicRingBuffer::FlushToFile(const char* filepath) {
    if (!filepath || filepath[0] == '\0') return;

    FILE* f = fopen(filepath, "a");
    if (!f) return;

    std::vector<ForensicEvent> snapshot(SIZE);
    size_t eventCount = 0;
    uint64_t droppedBefore = 0;

    if (lock.test_and_set(std::memory_order_acquire)) {
        fclose(f);
        return;
    }
    size_t currHead = head;
    size_t start = flushed;
    size_t prevFlushed = flushed;
    if (currHead - start > SIZE) {
        start = currHead - SIZE;
    }

    eventCount = currHead - start;
    for (size_t i = 0; i < eventCount; ++i) {
        snapshot[i] = buffer[(start + i) & MASK];
    }
    flushed = currHead;
    droppedBefore = dropped.exchange(0, std::memory_order_relaxed) + (start - prevFlushed);
    lock.clear(std::memory_order_release);

    if (eventCount == 0 && droppedBefore == 0) {
        fclose(f);
        return;
    }

    fprintf(f, "--- FORENSIC FLUSH tick_ms=%llu events=%llu dropped_before=%llu ---\n",
            (unsigned long long)GetTickCount64(),
            (unsigned long long)eventCount,
            (unsigned long long)droppedBefore);

    uint64_t created = g_timersCreated.load();
    uint64_t executed = g_timersExecuted.load();
    uint64_t cancelled = g_timersCancelled.load();
    fprintf(f, "TIMER_COUNTERS created=%llu executed=%llu cancelled=%llu derived_active=%llu\n",
            (unsigned long long)created, (unsigned long long)executed, (unsigned long long)cancelled,
            (unsigned long long)(created - executed - cancelled));

    for (size_t i = 0; i < eventCount; i++) {
        const auto& ev = snapshot[i];
        fprintf(f, "event=%s type=%u tid=%lu time_us=%lld reason=%d data1=%lu data2=%lu focus=%d\n",
            ForensicTrapName(ev.type),
            (unsigned int)ev.type,
            (unsigned long)ev.threadId,
            (long long)ev.timestampUs,
            ev.reasonCode,
            (unsigned long)ev.extraData1,
            (unsigned long)ev.extraData2,
            ev.focus ? 1 : 0);
    }

    fprintf(f, "--- END FLUSH ---\n\n");
    fclose(f);
}

static std::string s_forensicLogPath;
static std::thread s_forensicThread;
static std::atomic<bool> s_forensicRunning{false};
static std::atomic<bool> s_forensicFlushRequested{false};
static std::mutex s_forensicWakeMutex;
static std::condition_variable s_forensicWakeCv;

static LONG WINAPI ForensicExceptionFilter(EXCEPTION_POINTERS* ep) {
    (void)ep;
    FlushForensicLog();
    return EXCEPTION_CONTINUE_SEARCH;
}

void InitForensics(std::string path) {
    if (path.empty()) {
        path = MakeDefaultForensicLogPath();
    }
    s_forensicLogPath = path;
    workspace::EnsureLogDirectoryExists();

    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);

        SYSTEMTIME st;
        GetLocalTime(&st);

        fprintf(f, "=== FORENSIC SESSION HEADER ===\n");
        fprintf(f, "Version: v27.4.0-stable\n");
        fprintf(f, "Build: %s\n", BuildTypeName());
        fprintf(f, "BuildDate: %s %s\n", __DATE__, __TIME__);
        fprintf(f, "SessionStartLocal: %04u-%02u-%02u %02u:%02u:%02u\n",
                (unsigned)st.wYear,
                (unsigned)st.wMonth,
                (unsigned)st.wDay,
                (unsigned)st.wHour,
                (unsigned)st.wMinute,
                (unsigned)st.wSecond);
        fprintf(f, "TickMs: %llu\n", (unsigned long long)GetTickCount64());
        fprintf(f, "PID: %lu\n", GetCurrentProcessId());
        fprintf(f, "CWD: %s\n", cwd);
        fprintf(f, "EXE path: %s\n", exePath);
        fprintf(f, "LogPath: %s\n", path.c_str());
        fprintf(f, "Scope: anomaly events only; raw input is not logged\n");
        fprintf(f, "AutoFlush: 1000 ms plus async focus/profile requests and shutdown/crash flush\n");
        fprintf(f, "===============================\n\n");
        fclose(f);
    }

    SetUnhandledExceptionFilter(ForensicExceptionFilter);

    s_forensicFlushRequested.store(false, std::memory_order_relaxed);
    s_forensicRunning.store(true);
    s_forensicThread = std::thread([]() {
        while (s_forensicRunning.load(std::memory_order_relaxed)) {
            {
                std::unique_lock<std::mutex> lock(s_forensicWakeMutex);
                s_forensicWakeCv.wait_for(lock, std::chrono::milliseconds(1000), [] {
                    return !s_forensicRunning.load(std::memory_order_relaxed) ||
                           s_forensicFlushRequested.load(std::memory_order_relaxed);
                });
                s_forensicFlushRequested.store(false, std::memory_order_relaxed);
            }
            if (!s_forensicRunning.load(std::memory_order_relaxed)) break;
            FlushForensicLog();
        }
    });
}

void ShutdownForensics() {
    s_forensicRunning.store(false);
    s_forensicWakeCv.notify_all();
    if (s_forensicThread.joinable()) {
        s_forensicThread.join();
    }
    FlushForensicLog();
}

void FlushForensicLog() {
    if (s_forensicLogPath.empty()) {
        s_forensicLogPath = MakeDefaultForensicLogPath();
    }
    g_forensicBuffer.FlushToFile(s_forensicLogPath.c_str());
}

void RequestForensicFlush() {
    s_forensicFlushRequested.store(true, std::memory_order_release);
    s_forensicWakeCv.notify_one();
}

const std::string& GetForensicLogPath() {
    if (s_forensicLogPath.empty()) {
        s_forensicLogPath = MakeDefaultForensicLogPath();
    }
    return s_forensicLogPath;
}

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

#endif // MARCO_ENABLE_FORENSIC

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
