#pragma once

#include "build_config.h"
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <map>

namespace etw {

struct DriverLatencyStats {
    wchar_t driverName[64];
    uint64_t totalDpcTimeUs;
    uint64_t maxDpcUs;
    uint64_t totalIsrTimeUs;
    uint32_t dpcCount;
    uint32_t isrCount;
    double spikeCorrelation;
};

struct TraceAnalysis {
    uint32_t dpcCount{0};
    uint32_t isrCount{0};
    uint32_t cswitchCount{0};
    std::wstring rogueDriverName; // Name of the driver causing the most DPCs
    std::vector<DriverLatencyStats> offenders;
};

#if MARCO_ENABLE_ETW

struct TraceSession {
    ULONG64 handle{0};
    std::wstring sessionName;
    std::wstring logFilePath;
    bool isRunning{false};
};

// Starts a real NT Kernel Logger ETW session capturing Context Switches and DPCs
bool StartGlobalTrace();

// Stops the running ETW session
bool StopGlobalTrace();

// Queries if the trace is currently running
bool IsGlobalTraceRunning();

struct ModuleMapEntry {
    uint64_t loadBase;
    uint32_t imageSize;
    std::wstring fileName;
};

struct TraceContext {
    TraceAnalysis* analysis = nullptr;
    std::vector<ModuleMapEntry> moduleMap;
    std::map<std::wstring, DriverLatencyStats> driverStats;
};

bool AnalyzeTrace(const std::wstring& logFile, TraceAnalysis& outAnalysis);

#else

// Stubs for zero overhead
inline bool StartGlobalTrace() { return false; }
inline bool StopGlobalTrace() { return false; }
inline bool IsGlobalTraceRunning() { return false; }
inline bool AnalyzeTrace(const std::wstring&, TraceAnalysis&) { return false; }

#endif

} // namespace etw
