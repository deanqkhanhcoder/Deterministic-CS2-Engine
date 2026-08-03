// â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
// â•‘  Counter-Strafe v25.3 C++ â€” Input Capture Implementation            â•‘
// â•‘  Redesigned for Always-Track Physical Layer & Async Resolver        â•‘
// â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

#include "input_capture.h"
#include "state_engine.h"
#include "bhop.h"
#include "types.h"
#include "debug_logger.h"
#include "runtime_config.h"
#include "timing.h"
#include "build_config.h"
#include "target_platform.h"
#include "injection.h"
#include "telemetry.h"
#include "topology.h"
#include "routed_input_queue.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace capture {

static HHOOK s_keyboardHook = nullptr;
static HHOOK s_mouseHook    = nullptr;
static HWND  s_hwnd         = nullptr;

// --- Target Identity Tracking ---
static std::atomic<bool> s_wasTargetActive{false};
static target_platform::detail::TargetPublicationStore s_foregroundPublication;

static HWINEVENTHOOK s_winEventHook = nullptr;
static std::thread s_focusThread;
static std::atomic<bool> s_focusRunning{false};
static std::mutex s_focusStartupMutex;
static std::condition_variable s_focusStartupCv;
static DWORD s_focusThreadId = 0;
static bool s_focusQueueReady = false;
static bool s_focusHookReady = false;

// --- Physical State (Always Tracked) ---
static bool s_physSpaceDown = false;
static bool s_spaceSwallowed = false;
static bool s_wasdSwallowed[4] = {false};

static bool s_hkDownF1 = false;
static bool s_hkDownF2 = false;
static bool s_hkDownF3 = false;
static bool s_hkDownF6 = false;
static bool s_wasdPhysDown[4] = {false};
static bool s_lctrlPhysDown = false;
static bool s_cPhysDown = false;
static bool s_lshiftPhysDown = false;

struct RoutedKeyEvent {
    enum class Kind : uint8_t {
        KeyDown,
        KeyUp,
        SysLCtrl,
        SysC,
        Shift,
        SpaceDown,
        SpaceUp
    } kind = Kind::KeyDown;
    Key key = Key::W;
    bool routeSemantic = false;
    target_platform::TargetIdentity dispatchTarget;
    bool bhopSemantic = false;
    bool down = false;
};
constexpr std::size_t kRoutedEventCapacity = 1024;
constexpr std::size_t kMaxRoutedEventsPerPass = 32;
constexpr int64_t kRoutedDispatchBudgetUs = 2000;
static RoutedInputQueue<RoutedKeyEvent, kRoutedEventCapacity> s_routedEvents;
static RoutedInputWakeGate s_routedWakeGate;

static bool PostRoutedWake() {
    if (s_hwnd && PostMessageW(s_hwnd, WM_ROUTED_INPUT_READY, 0, 0)) {
        return true;
    }
    if (s_hwnd) PostMessageW(s_hwnd, WM_EMERGENCY_UNHOOK, 0, 0);
    return false;
}

static bool QueueRoutedEvent(const RoutedKeyEvent& event) {
    if (!s_routedEvents.TryPush(event)) {
        if (s_hwnd) PostMessageW(s_hwnd, WM_EMERGENCY_UNHOOK, 0, 0);
        return false;
    }
    if (s_routedWakeGate.RequestWake() && !PostRoutedWake()) return false;
    return true;
}

static bool ReconcileTargetFocusNow();

static target_platform::TargetIdentity SampleForegroundIdentity() {
    return s_foregroundPublication.Load().target;
}

void PollTarget() {
    target_platform::ResolveTargetAsync(SampleForegroundIdentity());
}

void DrainRoutedInputEvents() {
    s_routedEvents.DrainBounded(
        kMaxRoutedEventsPerPass, kRoutedDispatchBudgetUs,
        [](const RoutedKeyEvent& event) {
            const int64_t eventStartUs = timing::NowUs();
            const bool routeSemantic =
                event.routeSemantic &&
                target_platform::IsExpectedTargetActive(event.dispatchTarget);
            if (event.kind == RoutedKeyEvent::Kind::KeyDown) {
                engine::HandleKeyDown(event.key, routeSemantic,
                                      event.dispatchTarget);
            } else if (event.kind == RoutedKeyEvent::Kind::KeyUp) {
                engine::HandleKeyUp(event.key, routeSemantic,
                                    event.dispatchTarget);
            } else if (event.kind == RoutedKeyEvent::Kind::SysLCtrl) {
                engine::OnSysKeyChange(true, event.down, routeSemantic);
            } else if (event.kind == RoutedKeyEvent::Kind::SysC) {
                engine::OnSysKeyChange(false, event.down, routeSemantic);
            } else if (event.kind == RoutedKeyEvent::Kind::Shift) {
                engine::OnShiftChange(event.down, routeSemantic);
            } else if (event.kind == RoutedKeyEvent::Kind::SpaceDown) {
                engine::OnSpaceDown(routeSemantic);
                if (routeSemantic && event.bhopSemantic) bhop::OnSpaceDown();
            } else if (event.kind == RoutedKeyEvent::Kind::SpaceUp) {
                engine::OnSpaceUp();
                bhop::OnSpaceUp();
            }
            const int64_t eventUs = timing::NowUs() - eventStartUs;
            if (eventUs > 5000) {
                DLOG_WARN(Runtime,
                          "Routed event stalled: kind=%u key=%s route=%u total=%lld us",
                          static_cast<unsigned>(event.kind),
                          keymap::KeyName[ki(event.key)],
                          routeSemantic ? 1U : 0U,
                          static_cast<long long>(eventUs));
            }
        },
        [] { return timing::NowUs(); });

    if (s_routedWakeGate.CompleteDrain(
            [] { return s_routedEvents.Size() == 0; })) {
        (void)PostRoutedWake();
    }
}

void ReconcileTargetFocus() {
    (void)ReconcileTargetFocusNow();
}

bool IsTargetActiveForUI() {
    const auto foreground = SampleForegroundIdentity();
    if (!foreground.IsValid()) return false;
    auto resolvedId = target_platform::GetCurrentIdentity();
    return foreground == resolvedId && resolvedId.IsValid();
}

static bool IsTargetActiveFast(const target_platform::TargetIdentity& expected) {
    const auto published = target_platform::GetCurrentIdentity();
    const bool active = expected.IsValid() && expected == published;
    if (active != s_wasTargetActive.load(std::memory_order_acquire) && s_hwnd) {
        PostMessageW(s_hwnd, WM_TARGET_REFRESH_REQUEST, 0, 0);
    }
    return active;
}

static std::mutex s_focusMutex;
static std::atomic<HWND> s_lastEvaluatedFg{nullptr};

static bool ReconcileTargetFocusNow() {
    const auto foreground = SampleForegroundIdentity();
    HWND fg = foreground.hwnd;
    auto pub = target_platform::GetCurrentIdentity();
    bool isActive = foreground.IsValid() && foreground == pub && pub.IsValid();

    // Focus reconciliation is serialized onto the message thread. Hook
    // callbacks only enqueue resolver work and never mutate engine state.
    bool wasActive = s_wasTargetActive.load(std::memory_order_acquire);
    if (isActive == wasActive) {
        s_lastEvaluatedFg.store(fg, std::memory_order_relaxed);
        return isActive;
    }

    std::unique_lock<std::mutex> lock(s_focusMutex);

    // Check again under lock
    wasActive = s_wasTargetActive.load(std::memory_order_relaxed);
    if (isActive == wasActive) {
        return isActive;
    }

    s_lastEvaluatedFg.store(fg, std::memory_order_relaxed);

    bool didLoseFocus = false;
    bool didRegainFocus = false;

    // --- Unified Focus Reconciliation ---
    if (wasActive && !isActive) {
        const int64_t reconcileStartUs = timing::NowUs();
        s_wasTargetActive.store(false, std::memory_order_release);
        didLoseFocus = true;
        engine::ClearHeldKeys(pub);
        bhop::OnSpaceUp();
        s_spaceSwallowed = false; // Explicit swallow release
        for (int i = 0; i < 4; ++i) {
            s_wasdSwallowed[i] = false;
        }
        s_physSpaceDown = false;
        s_lctrlPhysDown = false;
        s_cPhysDown = false;
        s_lshiftPhysDown = false;
        s_hkDownF1 = s_hkDownF2 = s_hkDownF3 = s_hkDownF6 = false; // Clear global hotkey states on focus loss
        const int64_t reconcileUs = timing::NowUs() - reconcileStartUs;
        if (reconcileUs > 5000) {
            DLOG_WARN(Runtime, "Focus-loss reconciliation stalled for %lld us",
                      reconcileUs);
        }
    } else if (!wasActive && isActive) {
        const int64_t reconcileStartUs = timing::NowUs();
        s_wasTargetActive.store(true, std::memory_order_release);
        didRegainFocus = true;
        
        // Sync local space state with actual hardware truth
        s_physSpaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        s_wasdPhysDown[ki(Key::W)] = (GetAsyncKeyState('W') & 0x8000) != 0;
        s_wasdPhysDown[ki(Key::S)] = (GetAsyncKeyState('S') & 0x8000) != 0;
        s_wasdPhysDown[ki(Key::A)] = (GetAsyncKeyState('A') & 0x8000) != 0;
        s_wasdPhysDown[ki(Key::D)] = (GetAsyncKeyState('D') & 0x8000) != 0;
        s_lctrlPhysDown = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
        s_cPhysDown = (GetAsyncKeyState('C') & 0x8000) != 0;
        s_lshiftPhysDown = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
        s_hkDownF1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        s_hkDownF2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        s_hkDownF3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
        s_hkDownF6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        
        engine::RebuildState();

        // Re-sync Space swallow state on focus regain using the fresh hardware state
        if (s_physSpaceDown) {
            bool shouldRouteBhop = isActive;
            if (shouldRouteBhop && rcfg::Get().bhopEnabled && (target_platform::GetActiveCapabilities() & target_platform::CAP_BHOP)) {
                bhop::OnSpaceDown();
                s_spaceSwallowed = true;
            } else {
                s_spaceSwallowed = false;
            }
        } else {
            s_spaceSwallowed = false;
        }
        const int64_t reconcileUs = timing::NowUs() - reconcileStartUs;
        if (reconcileUs > 5000) {
            DLOG_WARN(Runtime, "Focus-regain reconciliation stalled for %lld us",
                      reconcileUs);
        }
    }

    lock.unlock();

    if (didLoseFocus) {
        DLOG_WARN(Hook, "Target focus LOST [HWND:%p PID:%lu]", static_cast<void*>(pub.hwnd), pub.pid);
        telemetry::ForensicEvent ev = { telemetry::ForensicTrapType::FOCUS_LOST, GetCurrentThreadId(), timing::NowUs(), 0, 0, 0, false };
        telemetry::g_forensicBuffer.Push(ev);
        telemetry::RequestForensicFlush();
    } else if (didRegainFocus) {
        DLOG_WARN(Hook, "Target focus REGAINED [HWND:%p PID:%lu]", static_cast<void*>(pub.hwnd), pub.pid);
        telemetry::ForensicEvent ev = { telemetry::ForensicTrapType::FOCUS_GAINED, GetCurrentThreadId(), timing::NowUs(), 0, 0, 0, true };
        telemetry::g_forensicBuffer.Push(ev);
        telemetry::RequestForensicFlush();
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

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  KEYBOARD HOOK CALLBACK
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    int64_t hookStartUs = timing::NowUs();
#if MARCO_ENABLE_FORENSIC
    engine::dbgLastHookUs.store(hookStartUs, std::memory_order_relaxed);
    engine::dbgEventSeq.fetch_add(1, std::memory_order_relaxed);
#endif

    // Heartbeat & Core tracking
    uint32_t procNumber = GetCurrentProcessorNumber();
    uint32_t prevCore = telemetry::g_activeHookCore.exchange(procNumber, std::memory_order_relaxed);
    (void)prevCore;
    if (prevCore != 0xFFFFFFFF && prevCore != procNumber) {
        telemetry::g_coreMigrations.fetch_add(1, std::memory_order_relaxed);
#if MARCO_ENABLE_FORENSIC
        telemetry::g_eventBuffer.Push(5, procNumber, 3, (int32_t)prevCore); // EVENT_CORE_MIGRATION = 3
#endif
    }

    if (nCode < 0) return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if ((info->flags & LLKHF_INJECTED) ||
        info->dwExtraInfo == injection::kInjectedInputMarker) {
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }

    struct ScopedTrace {
        int64_t startUs;
        DWORD vkCode;
        WPARAM action;
        uint32_t coreId;
        ~ScopedTrace() {
#if MARCO_ENABLE_FORENSIC
            int64_t durUs = timing::NowUs() - startUs;
            telemetry::g_hookLatency.Add(durUs);
            telemetry::g_eventBuffer.Push(5, coreId, 0, (int32_t)durUs); // EVENT_HOOK_KEYBOARD = 0
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(1, coreId, (int32_t)durUs, (int32_t)vkCode, (int32_t)action);
            }
#endif
        }
    } tracer{hookStartUs, info->vkCode, wParam, procNumber};


    // [FIX R-3] Filter out extended keys early but ensure tracking if needed
    // (Most movement/combat keys are not extended)
    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
    if (!isDown && !isUp) return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);

    DWORD vk = info->vkCode;
    DWORD sc = info->scanCode;


    // --- 1. GLOBAL HOTKEYS (Before Focus Filter) ---
    if (vk == VK_F1) {
        static int64_t s_hkLastDownF1 = 0;
        int64_t now = timing::NowUs();
        bool isAutoRepeat = s_hkDownF1 && (now - s_hkLastDownF1 < 500000);
        if (isDown) {
            s_hkLastDownF1 = now;
            if (!isAutoRepeat) {
                s_hkDownF1 = true;
                PostMessageW(s_hwnd, WM_BHOP_TOGGLE, 0, 0);
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
                PostMessageW(s_hwnd, WM_BHOP_CYCLE_MODE, 0, 0);
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
                PostMessageW(s_hwnd, WM_CYCLE_PROFILE, 0, 0);
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
                PostMessageW(s_hwnd, WM_TOGGLE_SUSPEND, 0, 0);
            }
        } else if (isUp) {
            s_hkDownF6 = false;
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
    const auto dispatchTarget = SampleForegroundIdentity();
    const bool isActive = IsTargetActiveFast(dispatchTarget);
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
            const bool isEdge = !s_wasdPhysDown[keyIdx];
            if (isEdge) {
                s_wasdPhysDown[keyIdx] = true;
                const bool queued = QueueRoutedEvent(
                    {RoutedKeyEvent::Kind::KeyDown, k, routeThis, dispatchTarget});
                s_wasdSwallowed[keyIdx] = routeThis && queued;
            }
            if (s_wasdSwallowed[keyIdx]) return 1;
        } else {
            if (s_wasdPhysDown[keyIdx]) {
                s_wasdPhysDown[keyIdx] = false;
                (void)QueueRoutedEvent(
                    {RoutedKeyEvent::Kind::KeyUp, k, routeThis, dispatchTarget});
            }
            bool wasSwallowed = s_wasdSwallowed[keyIdx];
            s_wasdSwallowed[keyIdx] = false; 
            if (wasSwallowed) return 1; 
        }
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    // Modifiers
    if (vk == VK_LCONTROL) {
        if (s_lctrlPhysDown != isDown) {
            s_lctrlPhysDown = isDown;
            (void)QueueRoutedEvent(
                {RoutedKeyEvent::Kind::SysLCtrl, Key::W,
                 shouldRoute && (caps & target_platform::CAP_CSTRAFE),
                 dispatchTarget, false, isDown});
        }
    }
    if (sc == 0x2E) { // 'C' key scan code
        if (s_cPhysDown != isDown) {
            s_cPhysDown = isDown;
            (void)QueueRoutedEvent(
                {RoutedKeyEvent::Kind::SysC, Key::W,
                 shouldRoute && (caps & target_platform::CAP_CSTRAFE),
                 dispatchTarget, false, isDown});
        }
    }
    if (vk == VK_LSHIFT) {
        if (s_lshiftPhysDown != isDown) {
            s_lshiftPhysDown = isDown;
            (void)QueueRoutedEvent(
                {RoutedKeyEvent::Kind::Shift, Key::W,
                 shouldRoute && (caps & target_platform::CAP_CSTRAFE),
                 dispatchTarget, false, isDown});
        }
    }

    // Space / Bhop Routing
    if (vk == VK_SPACE) {
        if (isDown) {
            if (spaceEdgeDown) {
                const bool shouldRouteBhop =
                    shouldRoute && rcfg::Get().bhopEnabled &&
                    (caps & target_platform::CAP_BHOP);
                const bool queued = QueueRoutedEvent(
                    {RoutedKeyEvent::Kind::SpaceDown, Key::W, shouldRoute,
                     dispatchTarget, shouldRouteBhop});
                s_spaceSwallowed = shouldRouteBhop && queued;
            }
            if (s_spaceSwallowed) return 1;
        } else {
            if (spaceEdgeUp) {
                (void)QueueRoutedEvent(
                    {RoutedKeyEvent::Kind::SpaceUp, Key::W, false,
                     dispatchTarget});
            }
            if (s_spaceSwallowed) {
                s_spaceSwallowed = false;
                return 1;
            }
        }
    }

    return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  MOUSE HOOK CALLBACK
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    int64_t hookStartUs = timing::NowUs();
#if MARCO_ENABLE_FORENSIC
    engine::dbgLastHookUs.store(hookStartUs, std::memory_order_relaxed);
    engine::dbgEventSeq.fetch_add(1, std::memory_order_relaxed);
#endif

    // Heartbeat & Core tracking
    uint32_t procNumber = GetCurrentProcessorNumber();
    uint32_t prevCore = telemetry::g_activeHookCore.exchange(procNumber, std::memory_order_relaxed);
    (void)prevCore;
    if (prevCore != 0xFFFFFFFF && prevCore != procNumber) {
        telemetry::g_coreMigrations.fetch_add(1, std::memory_order_relaxed);
#if MARCO_ENABLE_FORENSIC
        telemetry::g_eventBuffer.Push(5, procNumber, 3, (int32_t)prevCore); // EVENT_CORE_MIGRATION = 3
#endif
    }

    if (nCode < 0) return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);

    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (info->flags & LLMHF_INJECTED) {
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
    }
    if (info->dwExtraInfo == injection::kInjectedInputMarker) {
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
    }

    struct ScopedTrace {
        int64_t startUs;
        WPARAM action;
        uint32_t coreId;
        ~ScopedTrace() {
#if MARCO_ENABLE_FORENSIC
            int64_t durUs = timing::NowUs() - startUs;
            telemetry::g_hookLatency.Add(durUs);
            telemetry::g_eventBuffer.Push(5, coreId, 1, (int32_t)durUs); // EVENT_HOOK_MOUSE = 1
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(2, coreId, (int32_t)durUs, (int32_t)action);
            }
#endif
        }
    } tracer{hookStartUs, wParam, procNumber};

    return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  INSTALL / UNINSTALL
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    (void)hWinEventHook;
    (void)idObject;
    (void)idChild;
    (void)dwEventThread;
    (void)dwmsEventTime;
    if (event != EVENT_SYSTEM_FOREGROUND) return;

    const auto identity = target_platform::TargetIdentity::FromWindow(hwnd);
    s_foregroundPublication.Store(nullptr, identity);
    target_platform::ResolveTargetAsync(identity);
    if (s_hwnd) PostMessageW(s_hwnd, WM_TARGET_REFRESH_REQUEST, reinterpret_cast<WPARAM>(hwnd), 1);
}

bool Install(HWND hwnd) {
    s_hwnd = hwnd;
    s_routedEvents.Clear();
    s_routedWakeGate.Reset();
    s_foregroundPublication.Store(
        nullptr, target_platform::TargetIdentity::FromWindow(GetForegroundWindow()));
    
    s_focusRunning.store(true);
    s_focusThread = std::thread([]() {
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        topology::PinBackgroundThread();
        const HWINEVENTHOOK hook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        {
            std::lock_guard<std::mutex> startupLock(s_focusStartupMutex);
            s_focusThreadId = GetCurrentThreadId();
            s_focusQueueReady = true;
            s_focusHookReady = hook != nullptr;
            s_winEventHook = hook;
        }
        s_focusStartupCv.notify_all();
        if (!hook) return;
        while (s_focusRunning.load() && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        UnhookWinEvent(hook);
    });
    bool focusHookReady = false;
    {
        std::unique_lock<std::mutex> startupLock(s_focusStartupMutex);
        s_focusStartupCv.wait(startupLock, [] { return s_focusQueueReady; });
        focusHookReady = s_focusHookReady;
    }
    if (!focusHookReady) {
        s_focusRunning.store(false, std::memory_order_release);
        if (s_focusThread.joinable()) s_focusThread.join();
        {
            std::lock_guard<std::mutex> startupLock(s_focusStartupMutex);
            s_focusThreadId = 0;
            s_focusQueueReady = false;
            s_focusHookReady = false;
            s_winEventHook = nullptr;
        }
        DLOG_ERR(Hook, "Failed to install foreground WinEvent hook");
        return false;
    }

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
    
    s_focusRunning.store(false, std::memory_order_release);
    if (s_focusThread.joinable()) {
        DWORD tid = 0;
        {
            std::lock_guard<std::mutex> startupLock(s_focusStartupMutex);
            tid = s_focusThreadId;
        }
        if (tid != 0) {
            PostThreadMessageW(tid, WM_QUIT, 0, 0);
        }
        s_focusThread.join();
    }
    {
        std::lock_guard<std::mutex> startupLock(s_focusStartupMutex);
        s_focusThreadId = 0;
        s_focusQueueReady = false;
        s_focusHookReady = false;
    }
    s_winEventHook = nullptr;
    s_foregroundPublication.Store(nullptr, {});
    s_wasTargetActive.store(false, std::memory_order_release);
    s_routedEvents.Clear();
    s_routedWakeGate.Reset();

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
    return GetActiveIdentityFast().hwnd;
}

target_platform::TargetIdentity GetActiveIdentityFast() {
    // Pair identity capture with a second foreground read so a focus transition
    // between GetForegroundWindow and GetWindowThreadProcessId fails closed.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const HWND before = GetForegroundWindow();
        const auto identity = target_platform::TargetIdentity::FromWindow(before);
        if (GetForegroundWindow() == before) {
            s_foregroundPublication.Store(nullptr, identity);
            return identity;
        }
    }
    s_foregroundPublication.Store(nullptr, {});
    return {};
}

} // namespace capture
