#include "build_config.h"
#include "debug_logger.h"
#include "workspace.h"

#include <windows.h>
#include <cstdio>
#include <mutex>
#include <thread>
#include <deque>
#include <string>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdarg>

namespace dlog {
namespace {
constexpr size_t kMaxQueuedMessages = 4096;
constexpr auto kFlushTimeout = std::chrono::milliseconds(1000);

std::mutex s_logMutex;
std::condition_variable s_logCv;
std::condition_variable s_drainedCv;
std::deque<std::string> s_logQueue;
bool s_running = false;
bool s_accepting = false;
size_t s_inFlight = 0;
std::thread s_logThread;
FILE* s_logFile = nullptr;
std::atomic<uint64_t> s_accepted{0};
std::atomic<uint64_t> s_completed{0};
std::atomic<uint64_t> s_dropped{0};

void Complete(size_t count) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    s_inFlight -= count;
    s_completed.fetch_add(count, std::memory_order_relaxed);
    if (s_logQueue.empty() && s_inFlight == 0) s_drainedCv.notify_all();
}

void LogWorker() {
    std::deque<std::string> localQueue;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(s_logMutex);
            s_logCv.wait(lock, [] { return !s_running || !s_logQueue.empty(); });
            if (!s_running && s_logQueue.empty()) break;
            localQueue.swap(s_logQueue);
            s_inFlight += localQueue.size();
        }

        const size_t processed = localQueue.size();
        for (const auto& msg : localQueue) {
            if (s_logFile) std::fprintf(s_logFile, "%s\n", msg.c_str());
            char dbgBuf[1200];
            std::snprintf(dbgBuf, sizeof(dbgBuf), "[MARCO] %s\n", msg.c_str());
            OutputDebugStringA(dbgBuf);
        }
        if (s_logFile) std::fflush(s_logFile);
        localQueue.clear();
        Complete(processed);
    }
}
} // namespace

void Init() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_running) {
        s_accepting = true;
        return;
    }
    if (!s_logFile) {
        workspace::EnsureLogDirectoryExists();
        const std::string path = workspace::GetLogRootA() + "marco_debug.log";
        s_logFile = std::fopen(path.c_str(), "w");
    }
    s_running = true;
    s_accepting = true;
    s_logThread = std::thread(LogWorker);
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_accepting = false;
        s_running = false;
    }
    s_logCv.notify_all();
    if (s_logThread.joinable()) s_logThread.join();

    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logFile) {
        std::fclose(s_logFile);
        s_logFile = nullptr;
    }
}

void Write(Subsystem sys, Level lvl, const char* file, int line,
           const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    char fullMsg[1200];
    std::snprintf(fullMsg, sizeof(fullMsg), "[%d][%d] %s:%d - %s",
                  static_cast<int>(sys), static_cast<int>(lvl), file, line, buffer);

    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        if (!s_accepting || s_logQueue.size() >= kMaxQueuedMessages) {
            s_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        s_logQueue.emplace_back(fullMsg);
        s_accepted.fetch_add(1, std::memory_order_relaxed);
    }
    s_logCv.notify_one();
}

bool Flush() {
    std::unique_lock<std::mutex> lock(s_logMutex);
    const uint64_t target = s_accepted.load(std::memory_order_acquire);
    s_logCv.notify_one();
    return s_drainedCv.wait_for(lock, kFlushTimeout, [target] {
        return s_completed.load(std::memory_order_acquire) >= target &&
               s_logQueue.empty() && s_inFlight == 0;
    });
}

uint64_t AcceptedCount() { return s_accepted.load(std::memory_order_relaxed); }
uint64_t CompletedCount() { return s_completed.load(std::memory_order_relaxed); }
uint64_t DroppedCount() { return s_dropped.load(std::memory_order_relaxed); }
size_t PendingCount() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    return s_logQueue.size() + s_inFlight;
}

} // namespace dlog
