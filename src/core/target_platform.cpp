#include "target_platform.h"
#include "debug_logger.h"
#include "topology.h"
#include "telemetry.h"
#include "timing.h"
#include <atomic>
#include <mutex>
#include <cwctype>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <tlhelp32.h>
#include "input_capture.h"

namespace target_platform {

static const TargetProfile CS2_PROFILE = {
    L"CS2",
    {L"SDL_app", L"Valve001"},
    L"cs2.exe",
    CAP_BHOP | CAP_CSTRAFE | CAP_SCROLL
};

static const TargetProfile VALORANT_PROFILE = {
    L"Valorant",
    {},
    L"VALORANT-Win64-Shipping.exe",
    CAP_BHOP | CAP_CSTRAFE | CAP_SCROLL
};

static const TargetProfile ROBLOX_PROFILE = {
    L"Roblox",
    {L"WINDOWSCLIENT"},
    L"RobloxPlayerBeta.exe",
    CAP_BHOP | CAP_CSTRAFE | CAP_SCROLL
};

static const TargetProfile* s_registeredProfiles[] = {
    &CS2_PROFILE,
    &VALORANT_PROFILE,
    &ROBLOX_PROFILE
};

// --- [BUG #1] Consistent Seqlock-Protected Target Publication ---
struct TargetPublication {
    const TargetProfile* profile = nullptr;
    HWND hwnd = nullptr;
    DWORD pid = 0;
    uint64_t identity = 0;
};

struct AtomicTargetPublication {
    std::atomic<const TargetProfile*> profile{nullptr};
    std::atomic<HWND>                 hwnd{nullptr};
    std::atomic<DWORD>                pid{0};
    std::atomic<uint64_t>             identity{0};
};

static AtomicTargetPublication s_activePub;
static std::atomic<uint32_t> s_pubSeq{0};

static TargetPublication GetActivePublication() {
    TargetPublication pub;
    uint32_t seq0, seq1 = 0;
    do {
        seq0 = s_pubSeq.load(std::memory_order_acquire);
        if (seq0 & 1) {
            _mm_pause();
            seq1 = seq0 - 1; // force repeat
            continue;
        }

        pub.profile  = s_activePub.profile.load(std::memory_order_relaxed);
        pub.hwnd     = s_activePub.hwnd.load(std::memory_order_relaxed);
        pub.pid      = s_activePub.pid.load(std::memory_order_relaxed);
        pub.identity = s_activePub.identity.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acq_rel);
        seq1 = s_pubSeq.load(std::memory_order_relaxed);
    } while (seq0 != seq1);
    return pub;
}

// --- [BUG #4] Bounded Resolver Queue State ---
static constexpr size_t RESOLVER_QUEUE_SIZE = 16;
static TargetIdentity s_resolverQueue[RESOLVER_QUEUE_SIZE];
static size_t s_queueTail = 0; // Oldest index (read/pop here)
static size_t s_queueCount = 0; // Number of items in queue

static std::thread s_resolverThread;
static std::mutex s_resolverMutex;
static std::condition_variable s_resolverCv;
static std::atomic<bool> s_resolverRunning{false};

static void ResolverWorker();

static std::atomic<uint32_t> s_runningGamesMask{0};
static std::thread s_scannerThread;
static void ProcessScannerWorker();

static std::atomic<bool> s_initialized{false};

void Init() {
    if (s_initialized.exchange(true)) return;
    s_activePub.profile.store(nullptr, std::memory_order_relaxed);
    s_activePub.hwnd.store(nullptr, std::memory_order_relaxed);
    s_activePub.pid.store(0, std::memory_order_relaxed);
    s_activePub.identity.store(0, std::memory_order_relaxed);
    s_pubSeq.store(0, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(s_resolverMutex);
        s_queueTail = 0;
        s_queueCount = 0;
    }

    s_resolverRunning.store(true, std::memory_order_relaxed);
    s_resolverThread = std::thread(ResolverWorker);
    s_scannerThread = std::thread(ProcessScannerWorker);
}

void Shutdown() {
    if (!s_initialized.exchange(false)) return;
    s_resolverRunning.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(s_resolverMutex);
        s_queueTail = 0;
        s_queueCount = 0;
    }
    s_resolverCv.notify_all();
    if (s_resolverThread.joinable()) {
        s_resolverThread.join();
    }
    if (s_scannerThread.joinable()) {
        s_scannerThread.join();
    }
}

static std::wstring ToLower(const std::wstring& str) {
    std::wstring out = str;
    for (auto& c : out) {
        c = std::towlower(c);
    }
    return out;
}

// --- [BUG #4] Overwrite-Safe Bounded Queue Implementation ---
void ResolveTargetAsync(TargetIdentity identity) {
    if (!identity.IsValid()) return;
    {
        std::lock_guard<std::mutex> lock(s_resolverMutex);
        
        // [BUG #2] Prevent consecutive duplicate spam in the queue to avoid task poisoning
        bool duplicate = false;
        if (s_queueCount > 0) {
            size_t lastIdx = (s_queueTail + s_queueCount - 1) % RESOLVER_QUEUE_SIZE;
            if (s_resolverQueue[lastIdx] == identity) {
                duplicate = true;
            }
        }

        if (!duplicate) {
            if (s_queueCount == RESOLVER_QUEUE_SIZE) {
                // Queue full: discard the oldest to make room for the latest target
                s_queueTail = (s_queueTail + 1) % RESOLVER_QUEUE_SIZE;
                s_queueCount--;
            }
            size_t writeIdx = (s_queueTail + s_queueCount) % RESOLVER_QUEUE_SIZE;
            s_resolverQueue[writeIdx] = identity;
            s_queueCount++;
        }
    }
    s_resolverCv.notify_all();
}

static void ResolverWorker() {
    topology::PinBackgroundThread();
    while (s_resolverRunning.load(std::memory_order_relaxed)) {
        TargetIdentity id;
        {
            std::unique_lock<std::mutex> lock(s_resolverMutex);
            s_resolverCv.wait(lock, [] {
                return s_queueCount > 0 || !s_resolverRunning.load(std::memory_order_relaxed);
            });
            if (!s_resolverRunning.load(std::memory_order_relaxed)) break;
            
            // Pop oldest (FIFO)
            id = s_resolverQueue[s_queueTail];
            s_queueTail = (s_queueTail + 1) % RESOLVER_QUEUE_SIZE;
            s_queueCount--;
        }

        // [BUG #TP-2] Stale-task discard semantics: bypass ONLY if foreground window is valid and different
        HWND fg = capture::GetActiveWindowFast();
        if (!id.IsValid() || (fg != nullptr && id.hwnd != fg)) continue;

        wchar_t className[256];
        if (!GetClassNameW(id.hwnd, className, 256)) continue;
        std::wstring wClassName = className;

        // [BUG #3] Re-verify PID/TID ownership immediately after GetClassNameW to catch rapid focus/HWND reuse
        DWORD checkPid1 = 0;
        DWORD checkTid1 = GetWindowThreadProcessId(id.hwnd, &checkPid1);
        if (checkPid1 != id.pid || checkTid1 != id.tid) continue;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id.pid);
        std::wstring exeName = L"";
        if (hProcess) {
            wchar_t exePath[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, exePath, &size)) {
                std::wstring fullPath(exePath);
                size_t slashPos = fullPath.find_last_of(L"\\/");
                if (slashPos != std::wstring::npos) {
                    exeName = ToLower(fullPath.substr(slashPos + 1));
                } else {
                    exeName = ToLower(fullPath);
                }
            }
            CloseHandle(hProcess);
        }

        // [BUG #3] Re-verify PID/TID ownership after heavy process queries
        DWORD checkPid2 = 0;
        DWORD checkTid2 = GetWindowThreadProcessId(id.hwnd, &checkPid2);
        if (checkPid2 != id.pid || checkTid2 != id.tid) continue;

        const TargetProfile* matchedProfile = nullptr;
        for (const TargetProfile* profile : s_registeredProfiles) {
            bool match = false;
            if (!exeName.empty() && ToLower(profile->executableName) == exeName) {
                match = true;
            }
            if (!match) {
                for (const auto& cls : profile->windowClasses) {
                    if (cls == wClassName) { match = true; break; }
                }
            }
            if (match) { matchedProfile = profile; break; }
        }

        // [BUG #6] Prevent late publication after shutdown has been initiated
        if (!s_resolverRunning.load(std::memory_order_relaxed)) continue;

        // [BUG #1] Publish to Seqlock-protected structure for atomic, multi-variable consistency
        uint32_t seq = s_pubSeq.load(std::memory_order_relaxed);
        s_pubSeq.store(seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        if (matchedProfile) {
            s_activePub.profile.store(matchedProfile, std::memory_order_relaxed);
            s_activePub.hwnd.store(id.hwnd, std::memory_order_relaxed);
            s_activePub.pid.store(id.pid, std::memory_order_relaxed);
            s_activePub.identity.store(id.identity, std::memory_order_relaxed);
        } else {
            s_activePub.profile.store(nullptr, std::memory_order_relaxed);
            s_activePub.hwnd.store(nullptr, std::memory_order_relaxed);
            s_activePub.pid.store(0, std::memory_order_relaxed);
            s_activePub.identity.store(0, std::memory_order_relaxed);
        }

        std::atomic_thread_fence(std::memory_order_release);
        s_pubSeq.store(seq + 2, std::memory_order_release);

        if (matchedProfile) {
            DLOG_INFO(Runtime, "TargetPlatform: Resolved active profile: %ls (PID: %lu)", reinterpret_cast<int64_t>(matchedProfile->name.c_str()), static_cast<int64_t>(id.pid));
        }
    }
}

// --- [BUG #1] Reader functions are 100% Lock-Free and atomically consistent ---

const TargetProfile* GetActiveProfile() {
    return GetActivePublication().profile;
}

TargetIdentity GetCurrentIdentity() {
    auto pub = GetActivePublication();
    TargetIdentity id;
    id.identity = pub.identity;
    id.hwnd     = pub.hwnd;
    id.pid      = pub.pid;
    return id;
}

DWORD GetCurrentTargetPid() {
    return GetActivePublication().pid;
}

uint32_t GetActiveCapabilities() {
const TargetProfile* prof = GetActivePublication().profile;
    return prof ? prof->capabilities : CAP_NONE;
}

void GetActiveTargetName(wchar_t* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    const TargetProfile* prof = GetActivePublication().profile;
    if (prof) {
        wcsncpy_s(outBuf, maxLen, prof->name.c_str(), _TRUNCATE);
    } else {
        outBuf[0] = L'\0';
    }
}

void ProcessScannerWorker() {
    topology::PinBackgroundThread();
    while (s_resolverRunning.load(std::memory_order_relaxed)) {

        uint32_t mask = MASK_NONE;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
                        mask |= MASK_CS2;
                    }
                    else if (_wcsicmp(pe.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0) {
                        mask |= MASK_VALORANT;
                    }
                    else if (_wcsicmp(pe.szExeFile, L"robloxplayerbeta.exe") == 0) {
                        mask |= MASK_ROBLOX;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        s_runningGamesMask.store(mask, std::memory_order_relaxed);
        

        std::unique_lock<std::mutex> lock(s_resolverMutex);
        s_resolverCv.wait_for(lock, std::chrono::milliseconds(2000), [] {
            return !s_resolverRunning.load(std::memory_order_relaxed);
        });
        if (!s_resolverRunning.load(std::memory_order_relaxed)) break;

    }
}

uint32_t GetRunningGamesMask() {
    return s_runningGamesMask.load(std::memory_order_relaxed);
}

} // namespace target_platform
