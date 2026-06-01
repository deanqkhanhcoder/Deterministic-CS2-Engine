#include "build_config.h"
#include "debug_logger.h"
#include "workspace.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>

#include <thread>
#include <vector>
#include <string>
#include <condition_variable>
#include <atomic>

namespace dlog {

static std::mutex s_logMutex;
static std::condition_variable s_logCv;
static std::vector<std::string> s_logQueue;
static bool s_running = false;
static std::thread s_logThread;
static FILE* s_logFile = nullptr;

static void LogWorker() {
    std::vector<std::string> localQueue;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(s_logMutex);
            s_logCv.wait(lock, [] { return !s_running || !s_logQueue.empty(); });
            if (!s_running && s_logQueue.empty()) break;
            localQueue.swap(s_logQueue);
        }
        if (s_logFile && !localQueue.empty()) {
            for (const auto& msg : localQueue) {
                fprintf(s_logFile, "%s\n", msg.c_str());
            }
            fflush(s_logFile);
        }
        localQueue.clear();
    }
}

void Init() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (!s_logFile) {
        workspace::EnsureLogDirectoryExists();
        std::string path = workspace::GetLogRootA() + "marco_debug.log";
        s_logFile = fopen(path.c_str(), "w");
    }
    if (!s_running) {
        s_running = true;
        s_logThread = std::thread(LogWorker);
    }
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_running = false;
    }
    s_logCv.notify_all();
    if (s_logThread.joinable()) s_logThread.join();

    std::lock_guard<std::mutex> lock(s_logMutex);
    if (s_logFile) {
        fclose(s_logFile);
        s_logFile = nullptr;
    }
}

void Write(Subsystem sys, Level lvl, const char* file, int line, const char* fmt, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), fmt, arg1, arg2, arg3, arg4);

    char fullMsg[1200];
    snprintf(fullMsg, sizeof(fullMsg), "[%d][%d] %s:%d - %s", (int)sys, (int)lvl, file, line, buffer);

    {
        std::lock_guard<std::mutex> lock(s_logMutex);
        s_logQueue.emplace_back(fullMsg);
    }
    s_logCv.notify_one();
    
    char dbgBuf[1200];
    snprintf(dbgBuf, sizeof(dbgBuf), "[MARCO] %s\n", buffer);
    OutputDebugStringA(dbgBuf);
}

void Flush() {
    // Signal worker thread and wait for queue to drain (max 100ms)
    s_logCv.notify_one();
    for (int i = 0; i < 100; ++i) {
        {
            std::lock_guard<std::mutex> lock(s_logMutex);
            if (s_logQueue.empty()) return;
        }
        Sleep(1);
    }
}

} // namespace dlog
