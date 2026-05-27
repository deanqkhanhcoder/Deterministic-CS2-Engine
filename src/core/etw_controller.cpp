#include "etw_controller.h"
#include <initguid.h>
#include "debug_logger.h"
#include <vector>
#include <map>
#include <algorithm>
#include <tdh.h>
#include "workspace.h"

// DEFINE_GUID(SystemTraceControlGuid, 0x9e814aad, 0x3204, 0x11d2, 0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39);
// DEFINE_GUID(EventTraceGuid, 0x68fdd900, 0x4a3e, 0x11d1, 0x84, 0xf4, 0x00, 0x00, 0xf8, 0x04, 0x64, 0xe3);
DEFINE_GUID(ImageLoadGuid, 0x2cb15d1d, 0x5fc1, 0x11d2, 0xab, 0xe1, 0x00, 0xa0, 0xc9, 0x11, 0xf5, 0x18);
DEFINE_GUID(PerfInfoGuid, 0xce1dbfb4, 0x137e, 0x4da6, 0x87, 0xb0, 0x3f, 0x59, 0xaa, 0x10, 0x2c, 0xbc);
DEFINE_GUID(ThreadGuid, 0x3d6fa8d1, 0xfe05, 0x11d0, 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c);



namespace etw {

struct TraceProperties {
    EVENT_TRACE_PROPERTIES properties;
    wchar_t sessionName[256];
    wchar_t logFileName[MAX_PATH];
};

static TraceSession s_globalEtwSession;
static const std::wstring KERNEL_TRACE_FILE = L"performance_trace.etl";

bool StartGlobalTrace() {
    TraceSession& session = s_globalEtwSession;
    if (session.isRunning) return true;

    // NT Kernel Logger is the ONLY session that can capture CSwitch and DPCs
    session.sessionName = KERNEL_LOGGER_NAMEW;
    workspace::EnsureLogDirectoryExists();
    session.logFilePath = workspace::GetLogRootW() + KERNEL_TRACE_FILE;

    ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 
                       (session.sessionName.length() + 1) * sizeof(wchar_t) + 
                       (session.logFilePath.length() + 1) * sizeof(wchar_t);
                       
    std::vector<uint8_t> buffer(bufferSize, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());

    props->Wnode.BufferSize = bufferSize;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // QPC clock
    props->Wnode.Guid = SystemTraceControlGuid;
    
    // Enable DPC and Interrupt tracing only
    props->EnableFlags = EVENT_TRACE_FLAG_DPC | EVENT_TRACE_FLAG_INTERRUPT;
    
    props->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    props->MaximumFileSize = 100; // 100 MB max
    props->MinimumBuffers = 16;
    props->MaximumBuffers = 64;
    props->BufferSize = 1024; // 1 MB buffers
    
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    props->LogFileNameOffset = 0; // Must be 0 for STOP command
    
    // Copy logger name into buffer for STOP command
    wcscpy_s(reinterpret_cast<wchar_t*>(buffer.data() + props->LoggerNameOffset), session.sessionName.length() + 1, session.sessionName.c_str());

    // First try to stop any existing dangling session
    ControlTraceW(0, KERNEL_LOGGER_NAMEW, props, EVENT_TRACE_CONTROL_STOP);

    // Now set up paths for StartTraceW
    props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + (session.sessionName.length() + 1) * sizeof(wchar_t);
    wcscpy_s(reinterpret_cast<wchar_t*>(buffer.data() + props->LogFileNameOffset), session.logFilePath.length() + 1, session.logFilePath.c_str());

    ULONG status = StartTraceW(&session.handle, KERNEL_LOGGER_NAMEW, props);
    if (status != ERROR_SUCCESS) {
        DLOG_ERR(ETW, "Failed to start NT Kernel Logger ETW session. Error: %lu (Are you running as Administrator?)", status);
        return false;
    }

    session.isRunning = true;
    DLOG_INFO(ETW, "Started ETW Kernel Trace: %ls", reinterpret_cast<int64_t>(session.logFilePath.c_str()));
    return true;
}

bool StopGlobalTrace() {
    TraceSession& session = s_globalEtwSession;
    if (!session.isRunning && session.handle == 0) return true;

    ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
    std::vector<uint8_t> buffer(bufferSize, 0);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    
    props->Wnode.BufferSize = bufferSize;
    props->Wnode.Guid = SystemTraceControlGuid;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    props->LogFileNameOffset = 0;

    ULONG status = ControlTraceW(session.handle, session.sessionName.c_str(), props, EVENT_TRACE_CONTROL_STOP);
    if (status != ERROR_SUCCESS && status != ERROR_WMI_INSTANCE_NOT_FOUND) {
        DLOG_ERR(ETW, "Failed to stop ETW session. Error: %lu", status);
        return false;
    }

    session.isRunning = false;
    session.handle = 0;
    DLOG_INFO(ETW, "Stopped ETW Trace: %ls", reinterpret_cast<int64_t>(session.sessionName.c_str()));
    return true;
}

bool IsGlobalTraceRunning() {
    return s_globalEtwSession.isRunning;
}

static bool GetTdhPointer(PEVENT_RECORD record, const wchar_t* propName, uint64_t& out) {
    PROPERTY_DATA_DESCRIPTOR desc = {};
    desc.PropertyName = (ULONGLONG)propName;
    desc.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    if (TdhGetPropertySize(record, 0, nullptr, 1, &desc, &size) != ERROR_SUCCESS) return false;
    if (size == 4) {
        uint32_t temp = 0;
        if (TdhGetProperty(record, 0, nullptr, 1, &desc, size, (PBYTE)&temp) == ERROR_SUCCESS) {
            out = temp; return true;
        }
    } else if (size == 8) {
        if (TdhGetProperty(record, 0, nullptr, 1, &desc, size, (PBYTE)&out) == ERROR_SUCCESS) return true;
    }
    return false;
}

static bool GetTdhStringW(PEVENT_RECORD record, const wchar_t* propName, std::wstring& out) {
    PROPERTY_DATA_DESCRIPTOR desc = {};
    desc.PropertyName = (ULONGLONG)propName;
    desc.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    if (TdhGetPropertySize(record, 0, nullptr, 1, &desc, &size) != ERROR_SUCCESS || size == 0) return false;
    std::vector<uint8_t> buf(size);
    if (TdhGetProperty(record, 0, nullptr, 1, &desc, size, buf.data()) == ERROR_SUCCESS) {
        out = (wchar_t*)buf.data();
        
        // Strip full path to just basename
        size_t pos = out.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            out = out.substr(pos + 1);
        }
        return true;
    }
    return false;
}

static void WINAPI EventRecordCallback(PEVENT_RECORD record) {
    TraceContext* ctx = reinterpret_cast<TraceContext*>(record->UserContext);
    if (!ctx || !ctx->analysis) return;
    
    auto& guid = record->EventHeader.ProviderId;
    uint8_t type = record->EventHeader.EventDescriptor.Opcode;

    if (IsEqualGUID(guid, ImageLoadGuid) && type == 10) {
        uint64_t base = 0;
        std::wstring name;
        if (GetTdhPointer(record, L"ImageBase", base) && GetTdhStringW(record, L"FileName", name)) {
            ctx->moduleMap.push_back({base, 0, name}); // We omit size for now, as instruction pointers usually fall within known bases
            // Sort by base for binary search
            std::sort(ctx->moduleMap.begin(), ctx->moduleMap.end(), [](const ModuleMapEntry& a, const ModuleMapEntry& b) {
                return a.loadBase < b.loadBase;
            });
        }
    } 
    else if (IsEqualGUID(guid, ThreadGuid) && type == 36) {
        ctx->analysis->cswitchCount++;
    } 
    else if (IsEqualGUID(guid, PerfInfoGuid)) {
        if (type == 66 || type == 68) { // DPC
            ctx->analysis->dpcCount++;
            uint64_t routine = 0;
            if (GetTdhPointer(record, L"Routine", routine)) {
                // Find driver in module map via upper_bound
                auto it = std::upper_bound(ctx->moduleMap.begin(), ctx->moduleMap.end(), routine, [](uint64_t val, const ModuleMapEntry& m) {
                    return val < m.loadBase;
                });
                std::wstring driverName = L"Unknown";
                if (it != ctx->moduleMap.begin()) {
                    --it; // The module with the highest base address that is <= routine
                    driverName = it->fileName;
                }
                
                auto& stats = ctx->driverStats[driverName];
                if (stats.driverName[0] == L'\0') {
                    wcscpy_s(stats.driverName, driverName.c_str());
                }
                stats.dpcCount++;
                // Without duration tracking enabled, we just count. 
                // Full WPA computes duration using Start/End opcode pairs (66=Entry, 68=Exit or 69).
                // For now, we estimate 10us per DPC if duration is unknown, or we can use time deltas if we track state.
            }
        } else if (type == 67 || type == 69) { // ISR
            ctx->analysis->isrCount++;
            uint64_t routine = 0;
            if (GetTdhPointer(record, L"Routine", routine)) {
                auto it = std::upper_bound(ctx->moduleMap.begin(), ctx->moduleMap.end(), routine, [](uint64_t val, const ModuleMapEntry& m) {
                    return val < m.loadBase;
                });
                std::wstring driverName = L"Unknown";
                if (it != ctx->moduleMap.begin()) {
                    --it;
                    driverName = it->fileName;
                }
                auto& stats = ctx->driverStats[driverName];
                if (stats.driverName[0] == L'\0') {
                    wcscpy_s(stats.driverName, driverName.c_str());
                }
                stats.isrCount++;
            }
        }
    }
}

bool AnalyzeTrace(const std::wstring& logFile, TraceAnalysis& outAnalysis) {
    outAnalysis = TraceAnalysis{};
    
    TraceContext ctx;
    ctx.analysis = &outAnalysis;

    EVENT_TRACE_LOGFILEW log = {};
    log.LogFileName = const_cast<LPWSTR>(logFile.c_str());
    log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = EventRecordCallback;
    log.Context = &ctx;
    
    TRACEHANDLE handle = OpenTraceW(&log);
    if (handle == INVALID_PROCESSTRACE_HANDLE) {
        DLOG_ERR(ETW, "Failed to open ETW trace for analysis: %ls", reinterpret_cast<int64_t>(logFile.c_str()));
        return false;
    }
    
    ULONG status = ProcessTrace(&handle, 1, nullptr, nullptr);
    
    // Populate offenders array
    for (const auto& pair : ctx.driverStats) {
        outAnalysis.offenders.push_back(pair.second);
    }
    
    // Sort by DPC count descending
    std::sort(outAnalysis.offenders.begin(), outAnalysis.offenders.end(), [](const DriverLatencyStats& a, const DriverLatencyStats& b) {
        return (a.dpcCount + a.isrCount) > (b.dpcCount + b.isrCount);
    });
    
    CloseTrace(handle);
    
    if (status != ERROR_SUCCESS) {
        DLOG_ERR(ETW, "ProcessTrace failed with error: %lu", status);
        return false;
    }
    
    DLOG_INFO(ETW, "ETW Trace Analysis Complete: %u CSwitches, %u DPCs, %u ISRs", 
             outAnalysis.cswitchCount, outAnalysis.dpcCount, outAnalysis.isrCount);
             
    // Heuristic string for now
    if (!outAnalysis.offenders.empty()) {
        outAnalysis.rogueDriverName = outAnalysis.offenders[0].driverName;
        outAnalysis.rogueDriverName += L" (Highest Interrupt Volume)";
    } else {
        outAnalysis.rogueDriverName = L"None (Clean execution)";
    }
    
    return true;
}

} // namespace etw
