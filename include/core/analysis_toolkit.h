#pragma once
#include <cstdint>
#include <atomic>
#include <string>
#include <mutex>
#include "build_config.h"

namespace analysis {

enum EventType : uint8_t {
    EVENT_HOOK_KEYBOARD = 0,
    EVENT_HOOK_MOUSE = 1,
    EVENT_TIMER_JITTER = 2,
    EVENT_TIMER_OVERSLEEP = 3,
    EVENT_CORE_MIGRATION = 4,
    EVENT_MODE_CHANGE = 5,
    EVENT_SPIKE = 6
};

struct TraceEvent {
    uint64_t timestampUs;
    uint8_t  eventType;
    uint8_t  coreIndex;
    uint16_t reserved;
    int32_t  value;
};

struct LatencyStats {
    int32_t p50 = 0;
    int32_t p95 = 0;
    int32_t p99 = 0;
    int32_t p999 = 0;
    int32_t maxVal = 0;
};

struct CoreStats {
    uint32_t residencySamples = 0;
    uint32_t migrations = 0;
    int32_t maxJitter = 0;
};

struct SpikeCluster {
    uint32_t totalSpikes = 0;
    uint32_t burstCount = 0;
    double frequencyHz = 0.0;
    bool sustainedDegradation = false;
};

constexpr size_t TRACE_BUFFER_SIZE = 32768;

struct TraceBuffer {
    std::atomic<uint32_t> head{0};
    TraceEvent events[TRACE_BUFFER_SIZE];
    mutable std::mutex mutex;
};

#if !defined(MARCO_RELEASE)
extern TraceBuffer g_traceBuffer;

void Init();
void RecordEvent(EventType type, uint8_t core, int32_t value);
void ExportCSV(const wchar_t* filePath);
void ExportJSON(const wchar_t* filePath);
void SaveBaseline(const wchar_t* filePath);
bool CompareSession(const wchar_t* baselinePath, wchar_t* outResult, size_t maxLen);

void AnalyzeSession(LatencyStats& hook, LatencyStats& jitter, LatencyStats& oversleep,
                    SpikeCluster& spikes, CoreStats coreStats[64], uint32_t& activeCoresCount,
                    uint32_t& totalMigrations, uint32_t& smtCollisions);
#else
inline void Init() {}
inline void RecordEvent(EventType, uint8_t, int32_t) {}
inline void ExportCSV(const wchar_t*) {}
inline void ExportJSON(const wchar_t*) {}
inline void SaveBaseline(const wchar_t*) {}
inline bool CompareSession(const wchar_t*, wchar_t*, size_t) { return false; }

inline void AnalyzeSession(LatencyStats&, LatencyStats&, LatencyStats&,
                    SpikeCluster&, CoreStats[64], uint32_t&,
                    uint32_t&, uint32_t&) {}
#endif

} // namespace analysis
