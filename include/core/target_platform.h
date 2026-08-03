#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Target Platform Redesign                ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

namespace target_platform {

enum CapabilityFlags : uint32_t {
    CAP_NONE    = 0,
    CAP_BHOP    = 1 << 0,
    CAP_CSTRAFE = 1 << 1,
    CAP_SCROLL  = 1 << 2,
};

enum GameMask : uint32_t {
    MASK_NONE      = 0,
    MASK_CS2       = 1 << 0,
    MASK_ROBLOX    = 1 << 1,
    MASK_VALORANT  = 1 << 2
};

struct TargetProfile {
    std::wstring name;
    std::vector<std::wstring> windowClasses;
    std::wstring executableName;
    uint32_t capabilities;
};

// [FIX R-2] TargetIdentity structure to prevent HWND reuse hazards.
struct TargetIdentity {
    HWND     hwnd     = nullptr;
    DWORD    pid      = 0;
    DWORD    tid      = 0;
    uint64_t identity = 0; // Hash of HWND + PID + TID + process lifetime
    uint64_t processStartTime = 0;

    bool IsValid() const {
        return hwnd != nullptr && pid != 0 && tid != 0 && processStartTime != 0;
    }
    bool operator==(const TargetIdentity& other) const {
        return hwnd == other.hwnd && pid == other.pid && tid == other.tid &&
               identity == other.identity &&
               processStartTime == other.processStartTime;
    }
    bool operator!=(const TargetIdentity& other) const { return !(*this == other); }

    static TargetIdentity FromWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return {};
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || tid == 0) return {};

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        FILETIME creation{}, exit{}, kernel{}, user{};
        const BOOL queried = GetProcessTimes(process, &creation, &exit, &kernel, &user);
        CloseHandle(process);
        if (!queried) return {};

        const uint64_t processStartTime =
            (static_cast<uint64_t>(creation.dwHighDateTime) << 32) |
            creation.dwLowDateTime;
        if (processStartTime == 0) return {};
        const uint64_t identity =
            static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd)) ^
            (static_cast<uint64_t>(pid) << 32) ^
            (static_cast<uint64_t>(tid) << 16) ^ processStartTime;
        return {hwnd, pid, tid, identity, processStartTime};
    }
};

struct TargetPublication {
    const TargetProfile* profile = nullptr;
    TargetIdentity target{};
};

inline bool IsForegroundTarget(const TargetIdentity& foreground,
                               const TargetIdentity& expected) noexcept {
    return foreground.IsValid() && expected.IsValid() && foreground == expected;
}

namespace detail {

class TargetPublicationStore {
public:
    TargetPublication Load() const noexcept {
        TargetPublication publication;
        uint32_t before = 0;
        uint32_t after = 0;
        do {
            before = sequence_.load(std::memory_order_acquire);
            if ((before & 1U) != 0) continue;

            publication.profile = profile_.load(std::memory_order_relaxed);
            publication.target.hwnd = hwnd_.load(std::memory_order_relaxed);
            publication.target.pid = pid_.load(std::memory_order_relaxed);
            publication.target.tid = tid_.load(std::memory_order_relaxed);
            publication.target.identity = identity_.load(std::memory_order_relaxed);
            publication.target.processStartTime =
                processStartTime_.load(std::memory_order_relaxed);

            std::atomic_thread_fence(std::memory_order_acquire);
            after = sequence_.load(std::memory_order_acquire);
        } while (before != after || (after & 1U) != 0);
        return publication;
    }

    void Store(const TargetProfile* profile, const TargetIdentity& target) noexcept {
        std::lock_guard<std::mutex> writerLock(writerMutex_);
        sequence_.fetch_add(1, std::memory_order_acq_rel);
        profile_.store(profile, std::memory_order_relaxed);
        hwnd_.store(target.hwnd, std::memory_order_relaxed);
        pid_.store(target.pid, std::memory_order_relaxed);
        tid_.store(target.tid, std::memory_order_relaxed);
        identity_.store(target.identity, std::memory_order_relaxed);
        processStartTime_.store(target.processStartTime, std::memory_order_relaxed);
        sequence_.fetch_add(1, std::memory_order_release);
    }

private:
    mutable std::atomic<uint32_t> sequence_{0};
    std::atomic<const TargetProfile*> profile_{nullptr};
    std::atomic<HWND> hwnd_{nullptr};
    std::atomic<DWORD> pid_{0};
    std::atomic<DWORD> tid_{0};
    std::atomic<uint64_t> identity_{0};
    std::atomic<uint64_t> processStartTime_{0};
    mutable std::mutex writerMutex_;
};

inline bool ShouldPublishResolution(const TargetIdentity& requested,
                                    const TargetIdentity& foreground) noexcept {
    return requested.IsValid() ? requested == foreground : !foreground.IsValid();
}

inline bool IsStableForegroundSample(HWND firstHwnd,
                                     const TargetIdentity& firstIdentity,
                                     HWND secondHwnd,
                                     const TargetIdentity& secondIdentity) noexcept {
    return firstHwnd != nullptr && firstHwnd == secondHwnd &&
           firstIdentity.IsValid() && firstIdentity == secondIdentity;
}

} // namespace detail

// Initializes the target platform subsystem
void Init();

// Message-only window notified after resolver publication.
void SetNotifyWindow(HWND notifyHwnd);

// Shuts down the target platform subsystem
void Shutdown();

// Asynchronously resolves the given identity and updates the active target cache if matched
void ResolveTargetAsync(TargetIdentity identity);

// Gets the current active profile pointer (returns nullptr if none active)
const TargetProfile* GetActiveProfile();

// Gets the current target identity
TargetIdentity GetCurrentIdentity();

// Samples the live foreground HWND and full ownership identity twice.  An
// empty result means focus or HWND ownership changed while it was sampled.
TargetIdentity SampleStableForegroundIdentity();

// Revalidates a route-time target immediately before an input backend call.
bool IsExpectedTargetActive(const TargetIdentity& expected) noexcept;

// Gets the currently recognized target PID
DWORD GetCurrentTargetPid();

// Exposes capabilities safely
uint32_t GetActiveCapabilities();

// Name of the active target for UI
void GetActiveTargetName(wchar_t* outBuf, size_t maxLen);

uint32_t GetRunningGamesMask();

} // namespace target_platform
