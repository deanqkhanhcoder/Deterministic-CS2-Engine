#pragma once

#include <windows.h>
#include <stdint.h>
#include <atomic>
#include <algorithm>
#include <string>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

#ifdef _MSC_VER
#include <TraceLoggingProvider.h>

namespace telemetry {
TRACELOGGING_DECLARE_PROVIDER(g_hProvider);

inline void Init() { TraceLoggingRegister(g_hProvider); }
inline void Shutdown() { TraceLoggingUnregister(g_hProvider); }
inline bool IsEnabled() { return TraceLoggingProviderEnabled(g_hProvider, 0, 0); }
} // namespace telemetry

#else

namespace telemetry {
inline void Init() {}
inline void Shutdown() {}
inline bool IsEnabled() { return false; }
}

// Stub out TraceLoggingWrite for non-MSVC compilers
#define TraceLoggingWrite(...) do {} while(0)
#define TraceLoggingInt64(...) 0
#define TraceLoggingUInt64(...) 0
#define TraceLoggingUInt32(...) 0
#define TraceLoggingInt32(...) 0
#define TraceLoggingString(...) 0

#endif

namespace telemetry {

struct MetricBuffer {
    static constexpr int SIZE = 128;
    std::atomic<uint32_t> index{0};
    int64_t samples[SIZE] = {0};

    void Add(int64_t val) {
        uint32_t idx = index.fetch_add(1, std::memory_order_relaxed) % SIZE;
        samples[idx] = val;
    }

    void GetStats(int64_t& p50, int64_t& p99, int64_t& avg) const {
        int64_t temp[SIZE];
        uint32_t currIndex = index.load(std::memory_order_relaxed);
        int limit = currIndex < SIZE ? (int)currIndex : SIZE;
        for (int i = 0; i < limit; i++) {
            temp[i] = samples[i];
        }
        if (limit == 0) {
            p50 = p99 = avg = 0;
            return;
        }
        std::sort(temp, temp + limit);
        p50 = temp[limit / 2];
        p99 = temp[(limit * 99) / 100];
        int64_t sum = 0;
        for (int i = 0; i < limit; i++) {
            sum += temp[i];
        }
        avg = sum / limit;
    }

    int64_t GetAverageTenths() const {
        uint32_t currIndex = index.load(std::memory_order_relaxed);
        int limit = currIndex < SIZE ? (int)currIndex : SIZE;
        if (limit == 0) return 0;
        int64_t sum = 0;
        for (int i = 0; i < limit; i++) {
            sum += samples[i];
        }
        return (sum * 10LL) / limit;
    }
};

struct TraceEvent {
    uint8_t type;
    uint8_t core;
    int32_t v1;
    int32_t v2;
    int32_t v3;
};

class EventRingBuffer {
    static constexpr size_t SIZE = 1024;
    static constexpr size_t MASK = SIZE - 1;
    TraceEvent buffer[SIZE];
    alignas(64) size_t head = 0;
    alignas(64) size_t tail = 0;
    alignas(64) std::atomic_flag lock = ATOMIC_FLAG_INIT;
public:
    void Push(uint8_t type, uint8_t core, int32_t v1, int32_t v2 = 0, int32_t v3 = 0) {
        if (!lock.test_and_set(std::memory_order_acquire)) {
            if (head - tail < SIZE) {
                buffer[head & MASK] = {type, core, v1, v2, v3};
                head++;
            }
            lock.clear(std::memory_order_release);
        }
    }
    bool Pop(TraceEvent& ev) {
        bool popped = false;
        if (!lock.test_and_set(std::memory_order_acquire)) {
            if (head > tail) {
                ev = buffer[tail & MASK];
                tail++;
                popped = true;
            }
            lock.clear(std::memory_order_release);
        }
        return popped;
    }
};

enum class ForensicTrapType : uint8_t {
    FOCUS_LOST = 1,
    FOCUS_GAINED = 2,
    PROFILE_CHANGED = 3,
    COUNTERSTRAFE_CANCELLED = 4,
    COUNTERSTRAFE_CONFLICT = 5,
    BHOP_ABORTED = 6,
    BHOP_STALL = 7,
    TIMER_REJECTED = 8,
    LOGICAL_PHYSICAL_DIVERGENCE = 9
};

struct ForensicEvent {
    ForensicTrapType type;
    uint32_t threadId;
    int64_t timestampUs;
    int32_t reasonCode; // generic
    uint32_t extraData1; // phys state bitmask or axis or whatever
    uint32_t extraData2; // logical state bitmask or oppositePhys
    bool focus;
};

class ForensicRingBuffer {
    static constexpr size_t SIZE = 65536;
    static constexpr size_t MASK = SIZE - 1;
    ForensicEvent buffer[SIZE];
    alignas(64) size_t head = 0;
    alignas(64) size_t flushed = 0;
    alignas(64) std::atomic_flag lock = ATOMIC_FLAG_INIT;
    alignas(64) std::atomic<uint64_t> dropped{0};
public:
    void Push(const ForensicEvent& ev) {
        if (lock.test_and_set(std::memory_order_acquire)) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        buffer[head & MASK] = ev;
        head++;
        if (head - flushed > SIZE) {
            dropped.fetch_add((head - SIZE) - flushed, std::memory_order_relaxed);
            flushed = head - SIZE;
        }
        lock.clear(std::memory_order_release);
    }
    void FlushToFile(const char* filepath);
};

#include "build_config.h"

// Core Affinity tracking is now unconditional for Dashboard UI
alignas(64) extern std::atomic<uint32_t> g_activeTimingGroup;
alignas(64) extern std::atomic<uint32_t> g_activeTimingCore;
alignas(64) extern std::atomic<uint32_t> g_activeHookGroup;
alignas(64) extern std::atomic<uint32_t> g_activeHookCore;

extern ForensicRingBuffer g_forensicBuffer;

void InitForensics(std::string path);
void ShutdownForensics();
void FlushForensicLog();
void RequestForensicFlush();
const std::string& GetForensicLogPath();

#if MARCO_ENABLE_FORENSIC
extern EventRingBuffer g_eventBuffer;

extern std::atomic<uint64_t> g_timersCreated;
extern std::atomic<uint64_t> g_timersExecuted;
extern std::atomic<uint64_t> g_timersCancelled;

// Global telemetry buffers & status flags
alignas(64) extern MetricBuffer g_hookLatency;
alignas(64) extern MetricBuffer g_timerJitter;
alignas(64) extern MetricBuffer g_oversleep;
alignas(64) extern MetricBuffer g_spinDuration;
alignas(64) extern MetricBuffer g_stateMutation;

#endif // MARCO_ENABLE_FORENSIC

alignas(64) extern std::atomic<uint32_t> g_schedulerSpikes;
alignas(64) extern std::atomic<uint32_t> g_coreMigrations;
alignas(64) extern std::atomic<int64_t> g_timerOversleepPeak;
alignas(64) extern std::atomic<int64_t> g_wakeVarianceUs;



alignas(64) extern std::atomic<uint32_t> g_affinityMode;

#if MARCO_ENABLE_FORENSIC
void StartTelemetryThread();
void StopTelemetryThread();
#else
inline void StartTelemetryThread() {}
inline void StopTelemetryThread() {}
#endif

} // namespace telemetry
