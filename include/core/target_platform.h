#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Target Platform Redesign                ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>
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
    uint64_t identity = 0; // Hash of HWND + PID + TID for fast identity check

    bool IsValid() const { return hwnd != nullptr && pid != 0; }
    bool operator==(const TargetIdentity& other) const {
        return identity == other.identity && hwnd == other.hwnd;
    }
    bool operator!=(const TargetIdentity& other) const { return !(*this == other); }

    static TargetIdentity FromWindow(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) return {};
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        return { hwnd, pid, tid, (uint64_t)hwnd ^ ((uint64_t)pid << 32) ^ ((uint64_t)tid << 16) };
    }
};

// Initializes the target platform subsystem
void Init();

// Shuts down the target platform subsystem
void Shutdown();

// Asynchronously resolves the given identity and updates the active target cache if matched
void ResolveTargetAsync(TargetIdentity identity);

// Gets the current active profile pointer (returns nullptr if none active)
const TargetProfile* GetActiveProfile();

// Gets the current target identity
TargetIdentity GetCurrentIdentity();

// Gets the currently recognized target PID
DWORD GetCurrentTargetPid();

// Exposes capabilities safely
uint32_t GetActiveCapabilities();

// Name of the active target for UI
void GetActiveTargetName(wchar_t* outBuf, size_t maxLen);

uint32_t GetRunningGamesMask();

} // namespace target_platform
