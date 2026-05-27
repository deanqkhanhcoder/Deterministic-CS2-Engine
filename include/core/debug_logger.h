#pragma once

#include "build_config.h"

#if MARCO_DEBUG_FORENSIC

#include <cstdint>

namespace dlog {

enum class Level { Trace, Info, Warn, Error, Fatal };

enum class Subsystem {
    Runtime,
    Scheduler,
    Hook,
    Telemetry,
    UI,
    Watchdog,
    Injection,
    Timing,
    ThreadHealth,
    StressTest,
    Startup,
    Shutdown,
    ETW,
    Deadlock,
    Latency,
    Config,
    Errors,
    Warnings,
    Crashes,
    FireTrace,
    COUNT
};

void Init();
void Shutdown();
void Write(Subsystem sys, Level lvl, const char* file, int line, const char* fmt, int64_t arg1 = 0, int64_t arg2 = 0, int64_t arg3 = 0, int64_t arg4 = 0);
void Flush(); // Force flush

} // namespace dlog

#define DLOG_TRACE(sys, fmt, ...) dlog::Write(dlog::Subsystem::sys, dlog::Level::Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_INFO(sys, fmt, ...)  dlog::Write(dlog::Subsystem::sys, dlog::Level::Info,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_WARN(sys, fmt, ...)  dlog::Write(dlog::Subsystem::sys, dlog::Level::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_ERR(sys, fmt, ...)   dlog::Write(dlog::Subsystem::sys, dlog::Level::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_FATAL(sys, fmt, ...) dlog::Write(dlog::Subsystem::sys, dlog::Level::Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#else

#define DLOG_TRACE(sys, ...) do {} while(0)
#define DLOG_INFO(sys, ...)  do {} while(0)
#define DLOG_WARN(sys, ...)  do {} while(0)
#define DLOG_ERR(sys, ...)   do {} while(0)
#define DLOG_FATAL(sys, ...) do {} while(0)

#define LOG_FIRE_TRACE(phase, gen) do {} while(0)

namespace dlog {
inline void Init() {}
inline void Shutdown() {}
inline void Flush() {}
}

#endif
