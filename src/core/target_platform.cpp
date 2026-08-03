#include "target_platform.h"
#include "debug_logger.h"
#include "types.h"
#include "topology.h"
#include <atomic>
#include <mutex>
#include <cwctype>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <tlhelp32.h>


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

static detail::TargetPublicationStore s_activePublication;

static TargetPublication GetActivePublication() {
    return s_activePublication.Load();
}

static void PublishTarget(const TargetProfile* profile,
                          const TargetIdentity& target = {}) {
    s_activePublication.Store(profile, target);
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
static std::atomic<HWND> s_notifyHwnd{nullptr};

static void ResolverWorker();

static std::atomic<uint32_t> s_runningGamesMask{0};
static std::thread s_scannerThread;
static void ProcessScannerWorker();

static std::atomic<bool> s_initialized{false};

void Init() {
    if (s_initialized.exchange(true)) return;
    PublishTarget(nullptr);

    {
        std::lock_guard<std::mutex> lock(s_resolverMutex);
        s_queueTail = 0;
        s_queueCount = 0;
    }

    s_resolverRunning.store(true, std::memory_order_relaxed);
    s_resolverThread = std::thread(ResolverWorker);
    s_scannerThread = std::thread(ProcessScannerWorker);
}

void SetNotifyWindow(HWND notifyHwnd) {
    s_notifyHwnd.store(notifyHwnd, std::memory_order_release);
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
    PublishTarget(nullptr);
    s_runningGamesMask.store(MASK_NONE, std::memory_order_release);
    s_notifyHwnd.store(nullptr, std::memory_order_release);
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
    {
        std::lock_guard<std::mutex> lock(s_resolverMutex);
        if (!s_resolverRunning.load(std::memory_order_acquire)) return;
        
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

        TargetIdentity foreground = TargetIdentity::FromWindow(GetForegroundWindow());
        if (!detail::ShouldPublishResolution(id, foreground)) continue;
        if (!id.IsValid()) {
            PublishTarget(nullptr);
            HWND notifyHwnd = s_notifyHwnd.load(std::memory_order_acquire);
            if (notifyHwnd) PostMessageW(notifyHwnd, WM_TARGET_REFRESH_REQUEST, 0, 0);
            continue;
        }

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

        // Do not let a slow lookup publish a window that is no longer the
        // foreground target. Newer queued focus events will resolve next.
        foreground = TargetIdentity::FromWindow(GetForegroundWindow());
        if (!detail::ShouldPublishResolution(id, foreground)) continue;

        // [BUG #6] Prevent late publication after shutdown has been initiated
        if (!s_resolverRunning.load(std::memory_order_relaxed)) continue;

        if (matchedProfile) {
            PublishTarget(matchedProfile, id);
        } else {
            PublishTarget(nullptr);
        }

        if (matchedProfile) {
            DLOG_INFO(Runtime, "TargetPlatform: Resolved active profile: %ls (PID: %lu)", matchedProfile->name.c_str(), id.pid);
        }

        HWND notifyHwnd = s_notifyHwnd.load(std::memory_order_acquire);
        if (notifyHwnd) PostMessageW(notifyHwnd, WM_TARGET_REFRESH_REQUEST, 0, 0);
    }
}

// --- [BUG #1] Reader functions are 100% Lock-Free and atomically consistent ---

const TargetProfile* GetActiveProfile() {
    return GetActivePublication().profile;
}

TargetIdentity GetCurrentIdentity() {
    return GetActivePublication().target;
}

TargetIdentity SampleStableForegroundIdentity() {
    const HWND firstHwnd = GetForegroundWindow();
    const TargetIdentity firstIdentity = TargetIdentity::FromWindow(firstHwnd);
    const HWND secondHwnd = GetForegroundWindow();
    const TargetIdentity secondIdentity = TargetIdentity::FromWindow(secondHwnd);
    return detail::IsStableForegroundSample(firstHwnd, firstIdentity,
                                            secondHwnd, secondIdentity)
        ? secondIdentity
        : TargetIdentity{};
}

bool IsExpectedTargetActive(const TargetIdentity& expected) noexcept {
    if (!expected.IsValid()) return false;
    if (SampleStableForegroundIdentity() != expected) return false;
    if (GetCurrentIdentity() != expected) return false;
    return SampleStableForegroundIdentity() == expected;
}

DWORD GetCurrentTargetPid() {
    return GetActivePublication().target.pid;
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
