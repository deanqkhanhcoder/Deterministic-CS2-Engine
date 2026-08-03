#pragma once

#include "build_config.h"
#include <cstdint>
#include <cstddef>

namespace dlog {

enum class Level { Trace, Info, Warn, Error, Fatal };

enum class Subsystem {
    Runtime,
    Scheduler,
    Hook,
    Telemetry,
    UI,
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
    COUNT
};

void Init();
void Shutdown();
// Preserve printf argument types. The previous fixed int64_t slots made every
// pointer and floating-point format a variadic type mismatch (undefined
// behavior inside snprintf).
void Write(Subsystem sys, Level lvl, const char* file, int line,
           const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;
bool Flush(); // Wait for every accepted entry to be written and flushed.
uint64_t AcceptedCount();
uint64_t CompletedCount();
uint64_t DroppedCount();
size_t PendingCount();

} // namespace dlog

#if MARCO_ENABLE_FORENSIC

#define DLOG_TRACE(sys, fmt, ...) dlog::Write(dlog::Subsystem::sys, dlog::Level::Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_INFO(sys, fmt, ...)  dlog::Write(dlog::Subsystem::sys, dlog::Level::Info,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_WARN(sys, fmt, ...)  dlog::Write(dlog::Subsystem::sys, dlog::Level::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_ERR(sys, fmt, ...)   dlog::Write(dlog::Subsystem::sys, dlog::Level::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_FATAL(sys, fmt, ...) dlog::Write(dlog::Subsystem::sys, dlog::Level::Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#else

#define DLOG_TRACE(sys, ...) do {} while(0)
#define DLOG_INFO(sys, ...)  do {} while(0)
// Keep WARN, ERR, FATAL in production
#define DLOG_WARN(sys, fmt, ...)  dlog::Write(dlog::Subsystem::sys, dlog::Level::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_ERR(sys, fmt, ...)   dlog::Write(dlog::Subsystem::sys, dlog::Level::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define DLOG_FATAL(sys, fmt, ...) dlog::Write(dlog::Subsystem::sys, dlog::Level::Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
