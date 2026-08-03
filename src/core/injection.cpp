#include "injection.h"

#include <array>
#include <mutex>

namespace injection {
namespace {

constexpr std::size_t kMovementKeyCount = 4;
constexpr std::size_t kKeyboardReleaseKeyCount = kMovementKeyCount + 1; // WASD + Space
constexpr std::size_t kSpaceSlot = kMovementKeyCount;
constexpr std::size_t kMouseSlot = kKeyboardReleaseKeyCount;
constexpr std::size_t kReleaseSlotCount = kKeyboardReleaseKeyCount + 1;

struct InputTemplates {
    std::array<INPUT, kReleaseSlotCount> down{};
    std::array<INPUT, kReleaseSlotCount> up{};
};

INPUT MakeKeyboardInput(WORD vk, WORD scan, bool keyUp) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = scan;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
    input.ki.dwExtraInfo = kInjectedInputMarker;
    return input;
}

INPUT MakeMouseButtonInput(bool keyUp) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = keyUp ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
    input.mi.dwExtraInfo = kInjectedInputMarker;
    return input;
}

const InputTemplates& Templates() {
    // C++ guarantees thread-safe, one-time initialization of this local static.
    static const InputTemplates templates = [] {
        InputTemplates result{};
        for (std::size_t index = 0; index < kMovementKeyCount; ++index) {
            result.down[index] = MakeKeyboardInput(keymap::VkCode[index], keymap::ScanCode[index], false);
            result.up[index] = MakeKeyboardInput(keymap::VkCode[index], keymap::ScanCode[index], true);
        }
        result.down[kSpaceSlot] = MakeKeyboardInput(VK_SPACE, 0x39, false);
        result.up[kSpaceSlot] = MakeKeyboardInput(VK_SPACE, 0x39, true);
        result.down[kMouseSlot] = MakeMouseButtonInput(false);
        result.up[kMouseSlot] = MakeMouseButtonInput(true);
        return result;
    }();
    return templates;
}

#ifndef MARCO_INJECTION_TESTING
UINT DefaultSendInput(UINT count, INPUT* inputs, int inputSize) {
    return ::SendInput(count, inputs, inputSize);
}
#else
UINT DefaultSendInput(UINT, INPUT*, int) {
    // Unit-test binaries must not import or invoke the real SendInput API.
    return 0;
}
#endif

std::mutex g_mutex;
bool g_releaseOnly = false;

SendInputBackend g_backend = DefaultSendInput;
std::array<bool, kReleaseSlotCount> g_pendingRelease{};
std::array<bool, kReleaseSlotCount> g_held{};
std::array<target_platform::TargetIdentity, kReleaseSlotCount> g_ownerTarget{};
std::array<target_platform::TargetIdentity, kReleaseSlotCount> g_pendingTarget{};

UINT SendLocked(const INPUT* inputs, UINT count) {
    if (inputs == nullptr || count == 0) return 0;
    return g_backend(count, const_cast<INPUT*>(inputs), static_cast<int>(sizeof(INPUT)));
}

int ReleaseSlotFor(const INPUT& input) {
    const auto& templates = Templates();
    if (input.type == INPUT_MOUSE) {
        if (input.mi.dwExtraInfo != kInjectedInputMarker) return -1;
        const DWORD buttonFlags = input.mi.dwFlags & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP);
        return buttonFlags != 0 ? static_cast<int>(kMouseSlot) : -1;
    }
    if (input.type != INPUT_KEYBOARD || input.ki.dwExtraInfo != kInjectedInputMarker) return -1;
    for (std::size_t index = 0; index < kKeyboardReleaseKeyCount; ++index) {
        if (input.ki.wScan == templates.down[index].ki.wScan &&
            input.ki.wVk == templates.down[index].ki.wVk) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void TrackSentPrefixLocked(const INPUT* inputs, UINT sent) {
    for (UINT index = 0; index < sent; ++index) {
        const int slot = ReleaseSlotFor(inputs[index]);
        if (slot < 0) continue;
        const bool keyUp = inputs[index].type == INPUT_MOUSE
            ? (inputs[index].mi.dwFlags & MOUSEEVENTF_LEFTUP) != 0
            : (inputs[index].ki.dwFlags & KEYEVENTF_KEYUP) != 0;
        // A successful release resolves any prior recovery request. Track
        // accepted downs so rejected target-bound releases can be deferred
        // without inventing releases for keys this process never held.
        const auto releaseSlot = static_cast<std::size_t>(slot);
        if (keyUp) {
            g_pendingRelease[releaseSlot] = false;
            g_held[releaseSlot] = false;
            g_ownerTarget[releaseSlot] = {};
            g_pendingTarget[releaseSlot] = {};
        } else {
            g_held[releaseSlot] = true;
            g_ownerTarget[releaseSlot] = {};
        }
    }
}

UINT SendAndTrackLocked(const INPUT* inputs, UINT count) {
    const UINT sent = SendLocked(inputs, count);
    TrackSentPrefixLocked(inputs, sent < count ? sent : count);
    return sent;
}

void RefreshReleaseOnlyLocked() {
    g_releaseOnly = false;
    for (bool pending : g_pendingRelease) {
        if (pending) {
            g_releaseOnly = true;
            return;
        }
    }
}

UINT ReleaseSlotLocked(std::size_t slot) {
    const auto& templates = Templates();
    UINT result = 0;
    for (unsigned attempt = 0; attempt < kMaxReleaseAttempts; ++attempt) {
        result = SendLocked(&templates.up[slot], 1);
        if (result == 1) {
            g_pendingRelease[slot] = false;
            g_held[slot] = false;
            g_ownerTarget[slot] = {};
            g_pendingTarget[slot] = {};
            RefreshReleaseOnlyLocked();
            return result;
        }
    }
    g_pendingRelease[slot] = true;
    RefreshReleaseOnlyLocked();
    return result;
}

UINT ReleaseSlotForTargetLocked(
    std::size_t slot, const target_platform::TargetIdentity& target) {
    if (!g_held[slot] && !g_pendingRelease[slot]) return 0;
    if (!target.IsValid() || g_ownerTarget[slot] != target) {
        if (g_held[slot]) {
            g_pendingRelease[slot] = true;
            g_pendingTarget[slot] = g_ownerTarget[slot];
            RefreshReleaseOnlyLocked();
        }
        return 0;
    }
    if (!target_platform::IsExpectedTargetActive(target)) {
        g_pendingRelease[slot] = true;
        g_pendingTarget[slot] = target;
        RefreshReleaseOnlyLocked();
        return 0;
    }
    const UINT released = ReleaseSlotLocked(slot);
    if (released != 1 && g_pendingRelease[slot]) g_pendingTarget[slot] = target;
    return released;
}

bool CanDispatchNonReleaseLocked(DispatchValidator validator = nullptr,
                                 const void* context = nullptr) {
    if (g_releaseOnly) return false;
    return validator == nullptr || validator(context);
}

UINT ReconcilePendingReleasesLocked() {
    UINT released = 0;
    for (std::size_t slot = 0; slot < kReleaseSlotCount; ++slot) {
        if (g_pendingRelease[slot] && ReleaseSlotLocked(slot) == 1) ++released;
    }
    RefreshReleaseOnlyLocked();
    return released;
}

UINT SendDownUpLocked(std::size_t slot) {
    const auto& templates = Templates();
    const INPUT batch[2] = {templates.down[slot], templates.up[slot]};
    const UINT sent = SendAndTrackLocked(batch, 2);

    // SendInput may accept only the down prefix.  A tap/click helper owns the
    // full lifecycle, so repair that partial dispatch here while the backend
    // remains serialized instead of leaving a held input until shutdown.
    if (sent == 1) {
        return ReleaseSlotLocked(slot) == 1 ? sent : 0;
    }
    return sent;
}

bool IsMovementKey(Key key) {
    return static_cast<std::size_t>(ki(key)) < kMovementKeyCount;
}

UINT MouseButton(bool keyUp) {
    const auto& templates = Templates();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (keyUp) return ReleaseSlotLocked(kMouseSlot);
    if (!CanDispatchNonReleaseLocked()) return 0;
    return SendAndTrackLocked(&templates.down[kMouseSlot], 1);
}

} // namespace

UINT KeyDown(Key key) {
    if (key == Key::Mouse1) return MouseButton(false);
    if (!IsMovementKey(key)) return 0;
    const auto& templates = Templates();
    const std::size_t slot = static_cast<std::size_t>(ki(key));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked()) return 0;
    return SendAndTrackLocked(&templates.down[slot], 1);
}

UINT KeyUp(Key key) {
    if (key == Key::Mouse1) return MouseButton(true);
    if (!IsMovementKey(key)) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    return ReleaseSlotLocked(static_cast<std::size_t>(ki(key)));
}

UINT KeyDownIf(Key key, DispatchValidator validator, const void* context) {
    if (key == Key::Mouse1 || !IsMovementKey(key)) return 0;
    const auto& templates = Templates();
    const std::size_t slot = static_cast<std::size_t>(ki(key));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked(validator, context)) return 0;
    return SendAndTrackLocked(&templates.down[slot], 1);
}

UINT KeyUpIf(Key key, DispatchValidator validator, const void* context) {
    if (key == Key::Mouse1 || !IsMovementKey(key)) return 0;
    const std::size_t slot = static_cast<std::size_t>(ki(key));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (validator == nullptr || validator(context)) return ReleaseSlotLocked(slot);
    if (g_held[slot]) {
        g_pendingRelease[slot] = true;
        RefreshReleaseOnlyLocked();
    }
    return 0;
}

UINT KeyDownForTarget(
    Key key, const target_platform::TargetIdentity& target) {
    if (key == Key::Mouse1 || !IsMovementKey(key)) return 0;
    const auto& templates = Templates();
    const std::size_t slot = static_cast<std::size_t>(ki(key));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_releaseOnly || !target_platform::IsExpectedTargetActive(target)) return 0;
    if (g_held[slot]) {
        if (g_ownerTarget[slot] == target) return 1;
        g_pendingRelease[slot] = true;
        g_pendingTarget[slot] = g_ownerTarget[slot];
        RefreshReleaseOnlyLocked();
        return 0;
    }
    const UINT sent = SendAndTrackLocked(&templates.down[slot], 1);
    if (sent == 1) g_ownerTarget[slot] = target;
    return sent;
}

UINT KeyUpForTarget(
    Key key, const target_platform::TargetIdentity& target) {
    if (key == Key::Mouse1 || !IsMovementKey(key)) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    return ReleaseSlotForTargetLocked(static_cast<std::size_t>(ki(key)), target);
}

UINT KeyDownUp(Key key) {
    if (key == Key::Mouse1) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!CanDispatchNonReleaseLocked()) return 0;
        return SendDownUpLocked(kMouseSlot);
    }
    if (!IsMovementKey(key)) return 0;
    const std::size_t slot = static_cast<std::size_t>(ki(key));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked()) return 0;
    return SendDownUpLocked(slot);
}

UINT SpaceDown() {
    const auto& templates = Templates();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked()) return 0;
    return SendAndTrackLocked(&templates.down[kSpaceSlot], 1);
}

UINT SpaceUp() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ReleaseSlotLocked(kSpaceSlot);
}

UINT SpaceUpIf(DispatchValidator validator, const void* context) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (validator == nullptr || validator(context)) return ReleaseSlotLocked(kSpaceSlot);
    if (g_held[kSpaceSlot]) {
        g_pendingRelease[kSpaceSlot] = true;
        RefreshReleaseOnlyLocked();
    }
    return 0;
}

UINT SpaceDownForTarget(const target_platform::TargetIdentity& target) {
    const auto& templates = Templates();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_releaseOnly || !target_platform::IsExpectedTargetActive(target)) return 0;
    if (g_held[kSpaceSlot]) {
        if (g_ownerTarget[kSpaceSlot] == target) return 1;
        g_pendingRelease[kSpaceSlot] = true;
        g_pendingTarget[kSpaceSlot] = g_ownerTarget[kSpaceSlot];
        RefreshReleaseOnlyLocked();
        return 0;
    }
    const UINT sent = SendAndTrackLocked(&templates.down[kSpaceSlot], 1);
    if (sent == 1) g_ownerTarget[kSpaceSlot] = target;
    return sent;
}

UINT SpaceUpForTarget(const target_platform::TargetIdentity& target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ReleaseSlotForTargetLocked(kSpaceSlot, target);
}

UINT MouseWheel(int delta) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    input.mi.dwExtraInfo = kInjectedInputMarker;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked()) return 0;
    return SendLocked(&input, 1);
}

UINT SpaceDownIf(DispatchValidator validator, const void* context) {
    const auto& templates = Templates();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked(validator, context)) return 0;
    return SendAndTrackLocked(&templates.down[kSpaceSlot], 1);
}

UINT MouseWheelIf(int delta, DispatchValidator validator, const void* context) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    input.mi.dwExtraInfo = kInjectedInputMarker;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CanDispatchNonReleaseLocked(validator, context)) return 0;
    return SendLocked(&input, 1);
}

UINT ReconcilePendingReleases() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ReconcilePendingReleasesLocked();
}

UINT ReconcilePendingReleasesIf(DispatchValidator validator, const void* context) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (validator != nullptr && !validator(context)) return 0;
    return ReconcilePendingReleasesLocked();
}

UINT ReconcilePendingReleasesForTarget(
    const target_platform::TargetIdentity& target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!target_platform::IsExpectedTargetActive(target)) return 0;
    UINT released = 0;
    for (std::size_t slot = 0; slot < kReleaseSlotCount; ++slot) {
        if (!g_pendingRelease[slot] || g_pendingTarget[slot] != target) continue;
        released += ReleaseSlotLocked(slot);
    }
    RefreshReleaseOnlyLocked();
    return released;
}

std::size_t PendingReleaseCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::size_t count = 0;
    for (bool pending : g_pendingRelease) count += pending ? 1u : 0u;
    return count;
}

std::uint32_t HeldMovementMask() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::uint32_t mask = 0;
    for (int i = 0; i < 4; ++i) {
        if (g_held[i]) mask |= (1u << i);
    }
    return mask;
}

UINT ShutdownAndRelease() {
    std::lock_guard<std::mutex> lock(g_mutex);
    UINT released = 0;
    for (std::size_t slot = 0; slot < kReleaseSlotCount; ++slot) {
        g_pendingRelease[slot] = true;
        if (ReleaseSlotLocked(slot) == 1) ++released;
    }
    RefreshReleaseOnlyLocked();
    return released;
}

UINT ShutdownAndReleaseIf(DispatchValidator validator, const void* context) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (std::size_t slot = 0; slot < kReleaseSlotCount; ++slot) {
        g_pendingRelease[slot] = true;
    }
    RefreshReleaseOnlyLocked();
    if (validator != nullptr && !validator(context)) return 0;
    return ReconcilePendingReleasesLocked();
}

UINT ShutdownAndReleaseForTarget(
    const target_platform::TargetIdentity& target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (std::size_t slot = 0; slot < kReleaseSlotCount; ++slot) {
        if (!g_held[slot]) continue;
        g_pendingRelease[slot] = true;
        g_pendingTarget[slot] = g_ownerTarget[slot];
    }
    RefreshReleaseOnlyLocked();
    if (!target_platform::IsExpectedTargetActive(target)) return 0;
    UINT released = 0;
    for (std::size_t slot = 0; slot < kReleaseSlotCount; ++slot) {
        if (g_pendingRelease[slot] && g_pendingTarget[slot] == target) {
            released += ReleaseSlotLocked(slot);
        }
    }
    return released;
}

void SetSendInputBackendForTesting(SendInputBackend backend) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_backend = backend ? backend : DefaultSendInput;
}

void ResetForTesting() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_backend = DefaultSendInput;
    g_pendingRelease.fill(false);
    g_held.fill(false);
    g_ownerTarget.fill({});
    g_pendingTarget.fill({});
    g_releaseOnly = false;
}

} // namespace injection
