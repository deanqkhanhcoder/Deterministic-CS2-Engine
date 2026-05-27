#include "build_config.h"
#include "debug_logger.h"
#if MARCO_DEBUG_FORENSIC

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include "workspace.h"

namespace dlog {

static std::mutex s_logMutex;
static FILE* s_logFile = nullptr;

void Init() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (!s_logFile) {
        fopen_s(&s_logFile, "marco_debug.log", "w");
    }
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logFile) {
        fclose(s_logFile);
        s_logFile = nullptr;
    }
}

void Write(Subsystem sys, Level lvl, const char* file, int line, const char* fmt, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), fmt, arg1, arg2, arg3, arg4);
    
    if (s_logFile) {
        fprintf(s_logFile, "[%d][%d] %s:%d - %s\n", (int)sys, (int)lvl, file, line, buffer);
        fflush(s_logFile);
    }
    
    char dbgBuf[1200];
    snprintf(dbgBuf, sizeof(dbgBuf), "[MARCO] %s\n", buffer);
    OutputDebugStringA(dbgBuf);
}

void Flush() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logFile) fflush(s_logFile);
}

} // namespace dlog

#endif // MARCO_DEBUG_FORENSIC
