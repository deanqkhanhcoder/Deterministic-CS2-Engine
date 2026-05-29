// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Input Capture Implementation            ║
// ║  Redesigned for Always-Track Physical Layer & Async Resolver        ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "input_capture.h"
#include "state_engine.h"
#include "bhop.h"
#include "types.h"
#include "debug_logger.h"
#include "runtime_config.h"
#include "timing.h"
#include "build_config.h"
#include "target_platform.h"
#include "telemetry.h"
#include "topology.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <mutex>

namespace capture {

static HHOOK s_keyboardHook = nullptr;
static HHOOK s_mouseHook    = nullptr;
static HWND  s_hwnd         = nullptr;

// --- Target Identity Tracking ---
static target_platform::TargetIdentity s_cachedIdentity;
static bool s_wasTargetActive = false;
static std::atomic<HWND> s_activeHwnd{nullptr};

static HWINEVENTHOOK s_winEventHook = nullptr;
static std::thread s_focusThread;
static std::atomic<bool> s_focusRunning{false};
static std::atomic<DWORD> s_focusThreadId{0};

// --- Physical State (Always Tracked) ---
static bool s_physSpaceDown = false;
static bool s_spaceSwallowed = false;
static bool s_wasdSwallowed[4] = {false};

static bool s_hkDownF1 = false;
static bool s_hkDownF2 = false;
static bool s_hkDownF3 = false;
static bool s_hkDownF6 = false;
static bool s_hkDownF8 = false;

void PollTarget() {
    HWND fg = s_activeHwnd.load(std::memory_order_acquire);
    if (fg) {
        auto id = target_platform::TargetIdentity::FromWindow(fg);
        target_platform::ResolveTargetAsync(id);
    }
}

bool IsTargetActiveForUI() {
    HWND fg = s_activeHwnd.load(std::memory_order_acquire);
    if (!fg) return false;
    auto resolvedId = target_platform::GetCurrentIdentity();
    return (fg == resolvedId.hwnd && resolvedId.IsValid());
}

static void ReconcileSwallow() {
    bool bhopEnabled = rcfg::Get().bhopEnabled;
    bool isActive = IsTargetActiveForUI();
    uint32_t caps = target_platform::GetActiveCapabilities();

    bool shouldSwallow = bhopEnabled && isActive && (caps & target_platform::CAP_BHOP);

    if (s_spaceSwallowed && !shouldSwallow) {
        // Explicitly release swallow ownership
        s_spaceSwallowed = false;
        bhop::OnSpaceUp();
        engine::OnSpaceUp();
        DLOG_INFO(Hook, "Swallow: Explicitly released Space swallow ownership");
    }
}

static std::mutex s_focusMutex;

static bool IsTargetActive() {
    std::lock_guard<std::mutex> lock(s_focusMutex);
    HWND fg = s_activeHwnd.load(std::memory_order_acquire);
    if (!fg) return false;

    static HWND s_lastEvaluatedFg = nullptr;

    auto pub = target_platform::GetCurrentIdentity();
    bool isActive = (fg == pub.hwnd && pub.hwnd != nullptr);

    if (fg != s_lastEvaluatedFg) {
        s_lastEvaluatedFg = fg;
        target_platform::TargetIdentity currentId = target_platform::TargetIdentity::FromWindow(fg);
        s_cachedIdentity = currentId;
        if (!isActive) {
            target_platform::ResolveTargetAsync(currentId);
        }
    }

    auto currentId = s_cachedIdentity;
    (void)currentId;

    // --- Unified Focus Reconciliation ---
    if (s_wasTargetActive && !isActive) {
        s_wasTargetActive = isActive;
        DLOG_WARN(Hook, "Target focus LOST [HWND:%p PID:%lu]", reinterpret_cast<int64_t>(currentId.hwnd), static_cast<int64_t>(currentId.pid));
        engine::ClearHeldKeys();
        bhop::OnSpaceUp();
        s_spaceSwallowed = false; // Explicit swallow release
        for (int i = 0; i < 4; ++i) {
            s_wasdSwallowed[i] = false;
        }
        s_physSpaceDown = false;
        s_hkDownF1 = s_hkDownF2 = s_hkDownF3 = s_hkDownF6 = s_hkDownF8 = false; // Clear global hotkey states on focus loss
    } else if (!s_wasTargetActive && isActive) {
        s_wasTargetActive = isActive;
        DLOG_WARN(Hook, "Target focus REGAINED [HWND:%p PID:%lu]", reinterpret_cast<int64_t>(currentId.hwnd), static_cast<int64_t>(currentId.pid));
        
        // Sync local space state with actual hardware truth
        s_physSpaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        s_hkDownF1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        s_hkDownF2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        s_hkDownF3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
        s_hkDownF6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        s_hkDownF8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        
        engine::RebuildState();

        // Re-sync Space swallow state on focus regain using the fresh hardware state
        if (s_physSpaceDown) {
            bool shouldRouteBhop = isActive;
            if (shouldRouteBhop && rcfg::Get().bhopEnabled && (target_platform::GetActiveCapabilities() & target_platform::CAP_BHOP)) {
                bhop::OnSpaceDown();
            } else {
                s_spaceSwallowed = false;
            }
        } else {
            s_spaceSwallowed = false;
        }
    } else {
        s_wasTargetActive = isActive;
    }

    return isActive;
}

static int ScanToKeyIndex(DWORD scanCode) {
    switch (scanCode) {
        case 0x11: return 0;  // W
        case 0x1F: return 1;  // S
        case 0x1E: return 2;  // A
        case 0x20: return 3;  // D
        default:   return -1;
    }
}

// ════════════════════════════════════════════════════════════════
//  KEYBOARD HOOK CALLBACK
// ════════════════════════════════════════════════════════════════
static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    int64_t hookStartUs = timing::NowUs();
#if MARCO_ENABLE_TELEMETRY
    engine::dbgLastHookUs.store(hookStartUs, std::memory_order_relaxed);
    engine::dbgEventSeq.fetch_add(1, std::memory_order_relaxed);
#endif

    // Heartbeat & Core tracking
#if MARCO_ENABLE_WATCHDOG
    telemetry::g_heartbeatHook.store(timing::NowMs(), std::memory_order_relaxed);
#endif
    uint32_t procNumber = GetCurrentProcessorNumber();
    uint32_t prevCore = telemetry::g_activeHookCore.exchange(procNumber, std::memory_order_relaxed);
#if MARCO_ENABLE_TELEMETRY
    if (prevCore != 0xFFFFFFFF && prevCore != procNumber) {
        telemetry::g_coreMigrations.fetch_add(1, std::memory_order_relaxed);
        telemetry::g_eventBuffer.Push(5, procNumber, 3, (int32_t)prevCore); // EVENT_CORE_MIGRATION = 3
    }
#endif

    if (nCode < 0) return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (info->flags & LLKHF_INJECTED) return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    struct ScopedTrace {
        int64_t startUs;
        DWORD vkCode;
        WPARAM action;
        uint32_t coreId;
        ~ScopedTrace() {
#if MARCO_ENABLE_TELEMETRY
            int64_t durUs = timing::NowUs() - startUs;
            telemetry::g_hookLatency.Add(durUs);
            telemetry::g_eventBuffer.Push(5, coreId, 0, (int32_t)durUs); // EVENT_HOOK_KEYBOARD = 0
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(1, coreId, (int32_t)durUs, (int32_t)vkCode, (int32_t)action);
            }
#endif
        }
    } tracer{hookStartUs, info->vkCode, wParam, procNumber};

    // [FIX BUG #9] Only reconcile space-swallow state on Space key events
    // Previously ran on EVERY keyboard event system-wide, adding overhead
    // from GetForegroundWindow + identity resolution to the LL hook path.
    if (info->vkCode == VK_SPACE) ReconcileSwallow();

    // [FIX R-3] Filter out extended keys early but ensure tracking if needed
    // (Most movement/combat keys are not extended)
    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
    if (!isDown && !isUp) return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    DWORD vk = info->vkCode;
    DWORD sc = info->scanCode;

    // [FIX COUNTER-STRAFE] If Byfron strips LLKHF_INJECTED, we MUST use dwExtraInfo to identify our own injections.
    // Otherwise, we swallow our own counter-strafe injections and poison our physical state.
    if (info->dwExtraInfo == 0x1337BEEF) {
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }

    // --- 1. GLOBAL HOTKEYS (Before Focus Filter) ---
    if (vk == VK_F1) {
        static int64_t s_hkLastDownF1 = 0;
        int64_t now = timing::NowUs();
        bool isAutoRepeat = s_hkDownF1 && (now - s_hkLastDownF1 < 500000);
        if (isDown) {
            s_hkLastDownF1 = now;
            if (!isAutoRepeat) {
                s_hkDownF1 = true;
                SendNotifyMessageW(s_hwnd, WM_BHOP_TOGGLE, 0, 0);
            }
        } else if (isUp) {
            s_hkDownF1 = false;
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F2) {
        static int64_t s_hkLastDownF2 = 0;
        int64_t now = timing::NowUs();
        bool isAutoRepeat = s_hkDownF2 && (now - s_hkLastDownF2 < 500000);
        if (isDown) {
            s_hkLastDownF2 = now;
            if (!isAutoRepeat) {
                s_hkDownF2 = true;
                SendNotifyMessageW(s_hwnd, WM_BHOP_CYCLE_MODE, 0, 0);
            }
        } else if (isUp) {
            s_hkDownF2 = false;
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F3) {
        static int64_t s_hkLastDownF3 = 0;
        int64_t now = timing::NowUs();
        bool isAutoRepeat = s_hkDownF3 && (now - s_hkLastDownF3 < 500000);
        if (isDown) {
            s_hkLastDownF3 = now;
            if (!isAutoRepeat) {
                s_hkDownF3 = true;
                SendNotifyMessageW(s_hwnd, WM_CYCLE_PROFILE, 0, 0);
            }
        } else if (isUp) {
            s_hkDownF3 = false;
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F6) {
        static int64_t s_hkLastDownF6 = 0;
        int64_t now = timing::NowUs();
        bool isAutoRepeat = s_hkDownF6 && (now - s_hkLastDownF6 < 500000);
        if (isDown) {
            s_hkLastDownF6 = now;
            if (!isAutoRepeat) {
                s_hkDownF6 = true;
                SendNotifyMessageW(s_hwnd, WM_TOGGLE_SUSPEND, 0, 0);
            }
        } else if (isUp) {
            s_hkDownF6 = false;
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F8) {
        static int64_t s_hkLastDownF8 = 0;
        int64_t now = timing::NowUs();
        bool isAutoRepeat = s_hkDownF8 && (now - s_hkLastDownF8 < 500000);
        if (isDown) {
            s_hkLastDownF8 = now;
            if (!isAutoRepeat) {
                s_hkDownF8 = true;
                SendNotifyMessageW(s_hwnd, WM_CLOSE, 0, 0);
            }
        } else if (isUp) {
            s_hkDownF8 = false;
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }

    // --- 2. ALWAYS-TRACK PHYSICAL LAYER ---
    // [FIX Bug #1] Track edges BEFORE any routing or focus exits.
    bool spaceEdgeDown = false;
    bool spaceEdgeUp   = false;
    if (vk == VK_SPACE) {
        if (isDown) {
            if (!s_physSpaceDown) { s_physSpaceDown = true; spaceEdgeDown = true; }
        } else {
            if (s_physSpaceDown) { s_physSpaceDown = false; spaceEdgeUp = true; }
        }
    }

    // --- 3. SEMANTIC ROUTING LAYER ---
    bool isActive = IsTargetActive();
    bool isSuspended = engine::IsSuspended();
    bool shouldRoute = isActive && !isSuspended;
    uint32_t caps = target_platform::GetActiveCapabilities();
    // WASD Routing
    int keyIdx = ScanToKeyIndex(sc);
    if (keyIdx >= 0) {
        assert(keyIdx < 4);
        Key k = static_cast<Key>(keyIdx);
        bool supportStrafe = (caps & target_platform::CAP_CSTRAFE);
        bool routeThis = shouldRoute && supportStrafe;
        
        if (isDown) {
            DLOG_TRACE(Hook, "Key DOWN: %s (route=%d)", reinterpret_cast<int64_t>(keymap::KeyName[keyIdx]), routeThis);
            // Check true hardware physical state BEFORE the engine updates it
            bool isEdge = !engine::GetState().phys[ki(k)];
            
            engine::HandleKeyDown(k, routeThis);
            
            if (isEdge) {
                // ONLY adopt swallow ownership on the true physical edge
                s_wasdSwallowed[keyIdx] = routeThis;
            }
            
            // Always swallow the event (including auto-repeats) to prevent game spam
            if (routeThis) return 1;
        } else {
            DLOG_TRACE(Hook, "Key UP: %s (route=%d)", reinterpret_cast<int64_t>(keymap::KeyName[keyIdx]), routeThis);
            engine::HandleKeyUp(k, routeThis);
            bool wasSwallowed = s_wasdSwallowed[keyIdx];
            s_wasdSwallowed[keyIdx] = false; 
            if (wasSwallowed) return 1; 
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    // Modifiers
    if (vk == VK_LCONTROL) {
        engine::OnSysKeyChange(true, isDown, shouldRoute && (caps & target_platform::CAP_CSTRAFE));
    }
    if (sc == 0x2E) { // 'C' key scan code
        engine::OnSysKeyChange(false, isDown, shouldRoute && (caps & target_platform::CAP_CSTRAFE));
    }
    if (vk == VK_LSHIFT) {
        engine::OnShiftChange(isDown, shouldRoute && (caps & target_platform::CAP_CSTRAFE));
    }

    // Space / Bhop Routing
    if (vk == VK_SPACE) {
        if (isDown) {
            if (spaceEdgeDown) {
                engine::OnSpaceDown(shouldRoute);
                bool shouldRouteBhop = isActive;
                if (shouldRouteBhop && rcfg::Get().bhopEnabled && (caps & target_platform::CAP_BHOP)) {
                    bhop::OnSpaceDown();
                    s_spaceSwallowed = true;
                } else {
                    s_spaceSwallowed = false;
                }
            }
            if (s_spaceSwallowed) return 1;
        } else {
            if (spaceEdgeUp) {
                engine::OnSpaceUp();
                bhop::OnSpaceUp();
            }
            if (s_spaceSwallowed) {
                s_spaceSwallowed = false;
                return 1;
            }
        }
    }

    return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}

// ════════════════════════════════════════════════════════════════
//  MOUSE HOOK CALLBACK
// ════════════════════════════════════════════════════════════════
static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    int64_t hookStartUs = timing::NowUs();
#if MARCO_ENABLE_TELEMETRY
    engine::dbgLastHookUs.store(hookStartUs, std::memory_order_relaxed);
    engine::dbgEventSeq.fetch_add(1, std::memory_order_relaxed);
#endif

    // Heartbeat & Core tracking
#if MARCO_ENABLE_WATCHDOG
    telemetry::g_heartbeatHook.store(timing::NowMs(), std::memory_order_relaxed);
#endif
    uint32_t procNumber = GetCurrentProcessorNumber();
    uint32_t prevCore = telemetry::g_activeHookCore.exchange(procNumber, std::memory_order_relaxed);
#if MARCO_ENABLE_TELEMETRY
    if (prevCore != 0xFFFFFFFF && prevCore != procNumber) {
        telemetry::g_coreMigrations.fetch_add(1, std::memory_order_relaxed);
        telemetry::g_eventBuffer.Push(5, procNumber, 3, (int32_t)prevCore); // EVENT_CORE_MIGRATION = 3
    }
#endif

    if (nCode < 0) return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (info->flags & LLMHF_INJECTED) {
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
    }
    if (info->dwExtraInfo == 0x1337BEEF) {
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
    }

    struct ScopedTrace {
        int64_t startUs;
        WPARAM action;
        uint32_t coreId;
        ~ScopedTrace() {
#if MARCO_ENABLE_TELEMETRY
            int64_t durUs = timing::NowUs() - startUs;
            telemetry::g_hookLatency.Add(durUs);
            telemetry::g_eventBuffer.Push(5, coreId, 1, (int32_t)durUs); // EVENT_HOOK_MOUSE = 1
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(2, coreId, (int32_t)durUs, (int32_t)action);
            }
#endif
        }
    } tracer{hookStartUs, wParam, procNumber};

    // [FIX Bug #1] Mouse layer must also follow focus/suspend but never desync
    if (wParam == WM_LBUTTONDOWN) {
        if (!engine::IsSuspended() && IsTargetActive()) {
            if (target_platform::GetActiveCapabilities() & target_platform::CAP_CSTRAFE) {
                engine::OnLButtonDown();
            }
        }
    }

    return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ════════════════════════════════════════════════════════════════
//  INSTALL / UNINSTALL
// ════════════════════════════════════════════════════════════════
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    (void)hWinEventHook;
    (void)idObject;
    (void)idChild;
    (void)dwEventThread;
    (void)dwmsEventTime;
    if (event != EVENT_SYSTEM_FOREGROUND) return;
    
    s_activeHwnd.store(hwnd, std::memory_order_release);
}

bool Install(HWND hwnd) {
    s_hwnd = hwnd;
    s_activeHwnd.store(hwnd, std::memory_order_release);
    
    s_focusRunning.store(true);
    s_focusThread = std::thread([]() {
        s_focusThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
        topology::PinBackgroundThread();
        s_winEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        MSG msg;
        while (s_focusRunning.load() && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (s_winEventHook) UnhookWinEvent(s_winEventHook);
    });

    PollTarget();

    s_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandle(nullptr), 0);

    if (!s_keyboardHook || !s_mouseHook) {
        DLOG_ERR(Hook, "Failed to install hooks");
        Uninstall();
        return false;
    }

    DLOG_INFO(Hook, "Input capture hooks installed");
    return true;
}

void Uninstall() {
    if (s_keyboardHook) UnhookWindowsHookEx(s_keyboardHook);
    if (s_mouseHook) UnhookWindowsHookEx(s_mouseHook);
    s_keyboardHook = s_mouseHook = nullptr;
    
    if (s_focusRunning.load()) {
        s_focusRunning.store(false);
        if (s_focusThread.joinable()) {
            DWORD tid = s_focusThreadId.load(std::memory_order_relaxed);
            if (tid != 0) {
                while (!PostThreadMessage(tid, WM_QUIT, 0, 0)) {
                    if (GetLastError() == ERROR_INVALID_THREAD_ID) break;
                    Sleep(5);
                }
            }
            s_focusThread.join();
        }
    }

    DLOG_INFO(Hook, "Input capture hooks uninstalled");
}

bool Reinstall() {
    if (s_keyboardHook && s_mouseHook) return true;
    Uninstall();
    return Install(s_hwnd);
}

bool IsHookInstalled() {
    return s_keyboardHook != nullptr;
}

HWND GetActiveWindowFast() {
    return s_activeHwnd.load(std::memory_order_acquire);
}

} // namespace capture
