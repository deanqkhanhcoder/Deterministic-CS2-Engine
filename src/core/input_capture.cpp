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
#include "ui_main.h"
#include "config_io.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace capture {

thread_local int s_hookDepth = 0;

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

struct PhysicalEvent {
    bool isMouse; // true for mouse, false for keyboard
    WORD vkCode;
    WORD scanCode;
    bool isDown;
    int64_t timestamp_enqueue_us;
    uint64_t generation_id;
};

static std::mutex s_eventMutex;
static std::condition_variable s_eventCv;
static PhysicalEvent s_eventQueue[256];
static int s_eventHead = 0;
static int s_eventTail = 0;

static void PushEvent(const PhysicalEvent& ev) {
    std::lock_guard<std::mutex> lock(s_eventMutex);
    int current_depth = (s_eventHead - s_eventTail + 256) % 256;
    if (current_depth > 16) {
        DLOG_WARN(Hook, "QUEUE_STARVATION_ALERT depth=%d", current_depth);
    }
    int next = (s_eventHead + 1) % 256;
    if (next != s_eventTail) { // don't overwrite if full
        s_eventQueue[s_eventHead] = ev;
        s_eventHead = next;
        s_eventCv.notify_one();
    } else {
        DLOG_ERR(Hook, "QUEUE_OVERFLOW_ALERT dropping event");
    }
}

static std::thread s_workerThread;
static std::atomic<bool> s_workerRunning{false};

static bool IsTargetActive();

static bool s_hkDownF1 = false;
static bool s_hkDownF2 = false;
static bool s_hkDownF3 = false;
static bool s_hkDownF6 = false;
static bool s_hkDownF8 = false;

static int ScanToKeyIndex(DWORD scanCode);

void WorkerThreadFunc() {
    topology::PinBackgroundThread(); // Pin to background
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    
    while (s_workerRunning) {
        PhysicalEvent ev;
        {
            std::unique_lock<std::mutex> lock(s_eventMutex);
            s_eventCv.wait(lock, []{ return s_eventHead != s_eventTail || !s_workerRunning; });
            if (!s_workerRunning) break;
            
            ev = s_eventQueue[s_eventTail];
            s_eventTail = (s_eventTail + 1) % 256;
        }
        
        int64_t dequeue_time = timing::NowUs();
        int64_t queue_latency_us = dequeue_time - ev.timestamp_enqueue_us;
        
        if (queue_latency_us > 20000) {
            DLOG_ERR(Hook, "STALE_EVENT_DROPPED latency=%lld gen=%llu", queue_latency_us, ev.generation_id);
            continue; // Drop the event!
        }

        // Delegate to state engine outside the queue lock!
        if (ev.isMouse) {
            if (ev.isDown) {
                if (!engine::IsSuspended() && IsTargetActive()) {
                    if (target_platform::GetActiveCapabilities() & target_platform::CAP_CSTRAFE) {
                        engine::OnLButtonDown();
                    }
                }
            } else {
                if (!engine::IsSuspended() && IsTargetActive()) {
                    engine::OnLButtonUp();
                }
            }
        } else {
            bool isActive = IsTargetActive();
            bool isSuspended = engine::IsSuspended();
            bool shouldRoute = isActive && !isSuspended;
            uint32_t caps = target_platform::GetActiveCapabilities();
            bool supportStrafe = (caps & target_platform::CAP_CSTRAFE);
            bool routeThis = shouldRoute && supportStrafe;
            
            // --- 1. GLOBAL HOTKEYS ---
            if (ev.vkCode == VK_F1) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS F1"); continue; }
                if (ev.isDown && !s_hkDownF1) {
                    s_hkDownF1 = true;
                    bhop::ToggleEnabled();
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF1 = false;
                continue;
            }
            if (ev.vkCode == VK_F2) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS F2"); continue; }
                if (ev.isDown && !s_hkDownF2) {
                    s_hkDownF2 = true;
                    bhop::CycleMode();
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF2 = false;
                continue;
            }
            if (ev.vkCode == VK_F3) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS F3"); continue; }
                if (ev.isDown && !s_hkDownF3) {
                    s_hkDownF3 = true;
                    RuntimeConfig& cfg = rcfg::GetMutable();
                    cfg.activeBrakeProfileIndex = (cfg.activeBrakeProfileIndex % 4) + 1;
                    rcfg::Apply(cfg);
                    config_io::Save(cfg);
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF3 = false;
                continue;
            }
            if (ev.vkCode == VK_F6) {
                if (ev.isDown && !s_hkDownF6) {
                    s_hkDownF6 = true;
                    engine::ToggleSuspend();
                    bhop::OnSuspendChanged();
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF6 = false;
                continue;
            }
            if (ev.vkCode == VK_F8) {
                if (ev.isDown && !s_hkDownF8) {
                    s_hkDownF8 = true;
                    if (s_hwnd) {
                        PostMessageW(s_hwnd, WM_CLOSE, 0, 0);
                    }
                }
                else if (!ev.isDown) s_hkDownF8 = false;
                continue;
            }
            
            // --- 2. WASD ROUTING ---
            int keyIdx = ScanToKeyIndex(ev.scanCode);
            if (keyIdx >= 0) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS WASD"); continue; }
                Key k = static_cast<Key>(keyIdx);
                if (ev.isDown) {
                    engine::HandleKeyDown(k, routeThis, ev.timestamp_enqueue_us);
                } else {
                    engine::HandleKeyUp(k, routeThis, ev.timestamp_enqueue_us);
                }
                continue;
            }
            
            // --- 3. MODIFIER ROUTING ---
            if (!isActive) {
                if (ev.vkCode == VK_LCONTROL || ev.scanCode == 0x2E || ev.vkCode == VK_LSHIFT || ev.vkCode == VK_SPACE) {
                    DLOG_TRACE(Hook, "ENGINE_BYPASS MODIFIER/SPACE");
                    continue;
                }
            }
            if (ev.vkCode == VK_LCONTROL) {
                engine::OnSysKeyChange(true, ev.isDown, shouldRoute && supportStrafe);
                continue;
            }
            if (ev.scanCode == 0x2E) { // 'C' key scan code
                engine::OnSysKeyChange(false, ev.isDown, shouldRoute && supportStrafe);
                continue;
            }
            if (ev.vkCode == VK_LSHIFT) {
                engine::OnShiftChange(ev.isDown, shouldRoute && supportStrafe);
                continue;
            }
            
            // --- 4. SPACE ROUTING ---
            if (ev.vkCode == VK_SPACE) {
                if (ev.isDown) {
                    engine::OnSpaceDown(shouldRoute);
                    bool shouldRouteBhop = isActive;
                    if (shouldRouteBhop && rcfg::Get().bhopEnabled && (caps & target_platform::CAP_BHOP)) {
                        bhop::OnSpaceDown();
                    }
                } else {
                    engine::OnSpaceUp();
                    bhop::OnSpaceUp();
                }
                continue;
            }
        }
    }
}



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

static bool IsTargetActive() {
HWND fg = s_activeHwnd.load(std::memory_order_acquire);
    if (!fg) return false;

    static HWND s_lastEvaluatedFg = nullptr;

    auto pub = target_platform::GetCurrentIdentity();
    bool isActive = (fg == pub.hwnd && pub.hwnd != nullptr && pub.IsValid());
    if (isActive) DLOG_TRACE(Hook, "WINDOW_ACCEPT");
    else DLOG_TRACE(Hook, "WINDOW_REJECT");

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
    struct HookDepthGuard {
        HookDepthGuard() { s_hookDepth++; }
        ~HookDepthGuard() { s_hookDepth--; }
    } guard;

    if (s_hookDepth > 1) {
        DLOG_ERR(Hook, "[FIRE_TRACE] RE-ENTRANCY DETECTED depth=%d", s_hookDepth);
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }

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
    if (info->flags & LLKHF_INJECTED) {
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }

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
    // [FIX V26.5.1] All semantic processing moved to WorkerThreadFunc.
    // Hook thread only handles `return 1` swallow decisions and pushing to queue.
    
    // Evaluate if Space should be swallowed
    if (vk == VK_SPACE) {
        if (isDown) {
            if (!s_physSpaceDown) { s_physSpaceDown = true; } // Update physical truth early for swallow logic
            bool isActive = IsTargetActive();
            uint32_t caps = target_platform::GetActiveCapabilities();
            if (isActive && rcfg::Get().bhopEnabled && (caps & target_platform::CAP_BHOP)) {
                s_spaceSwallowed = true;
            } else {
                s_spaceSwallowed = false;
            }
        } else {
            if (s_physSpaceDown) { s_physSpaceDown = false; }
            if (s_spaceSwallowed) {
                s_spaceSwallowed = false;
                // Important: still push the Up event so Worker knows!
                PushEvent({false, (WORD)vk, (WORD)sc, isDown, hookStartUs, engine::dbgEventSeq.load(std::memory_order_relaxed)});
                return 1;
            }
        }
    }
    
    // Always push the raw event
    PushEvent({false, (WORD)vk, (WORD)sc, isDown, hookStartUs, engine::dbgEventSeq.load(std::memory_order_relaxed)});
    
    // Swallow space if needed
    if (vk == VK_SPACE && isDown && s_spaceSwallowed) {
        return 1;
    }

    return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
}

// ════════════════════════════════════════════════════════════════
//  MOUSE HOOK CALLBACK
// ════════════════════════════════════════════════════════════════
static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    struct HookDepthGuard {
        HookDepthGuard() { s_hookDepth++; }
        ~HookDepthGuard() { s_hookDepth--; }
    } guard;

    if (s_hookDepth > 1) {
        DLOG_ERR(Hook, "[FIRE_TRACE] RE-ENTRANCY DETECTED depth=%d", s_hookDepth);
        return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
    }

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
        if (info->dwExtraInfo == 0x1337BEEF) {
            return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
        }
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
    uint64_t gen = engine::dbgEventSeq.load(std::memory_order_relaxed);
    if (wParam == WM_LBUTTONDOWN) {
        PushEvent({true, 0, 0, true, timing::NowUs(), gen});
    } else if (wParam == WM_LBUTTONUP) {
        PushEvent({true, 0, 0, false, timing::NowUs(), gen});
    }

    return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ════════════════════════════════════════════════════════════════
//  INSTALL / UNINSTALL
// ════════════════════════════════════════════════════════════════
static std::thread s_hookThread;
static std::atomic<bool> s_hookThreadRunning{false};
static std::atomic<bool> s_hookInstalled{false};
static std::mutex s_hookMutex;
static std::condition_variable s_hookCv;
static DWORD s_hookThreadId = 0;

void HookThreadFunc() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    s_hookThreadId = GetCurrentThreadId();
    
    s_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    
    bool success = (s_keyboardHook && s_mouseHook);
    s_hookInstalled = success;
    
    // Notify start
    s_hookCv.notify_all();
    
    if (success) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    if (s_keyboardHook) UnhookWindowsHookEx(s_keyboardHook);
    if (s_mouseHook) UnhookWindowsHookEx(s_mouseHook);
    s_keyboardHook = nullptr;
    s_mouseHook = nullptr;
    s_hookInstalled = false;
}

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
    if (s_hookThreadRunning) return true;
    s_hwnd = hwnd;
    s_activeHwnd.store(hwnd, std::memory_order_release);
    
    if (!s_focusRunning.load()) {
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
    }

    PollTarget();

    s_hookThreadRunning = true;
    s_workerRunning = true;
    s_workerThread = std::thread(WorkerThreadFunc);
    
    std::unique_lock<std::mutex> lock(s_hookMutex);
    s_hookThread = std::thread(HookThreadFunc);
    s_hookCv.wait(lock); // Wait for thread to attempt install

    if (!s_hookInstalled.load()) {
        DLOG_ERR(Hook, "Failed to install hooks");
        Uninstall();
        return false;
    }

    DLOG_INFO(Hook, "Input capture hooks installed");
    return true;
}

void Uninstall() {
    if (s_hookThreadRunning) {
        s_hookThreadRunning = false;
        if (s_hookThreadId) {
            PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
        }
        if (s_hookThread.joinable()) {
            s_hookThread.join();
        }
        s_hookThreadId = 0;
    }
    
    if (s_workerRunning) {
        s_workerRunning = false;
        s_eventCv.notify_all();
        if (s_workerThread.joinable()) {
            s_workerThread.join();
        }
    }
    
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
    if (s_hookInstalled.load()) return true;
    Uninstall();
    return Install(s_hwnd);
}

bool IsHookInstalled() {
    return s_hookInstalled.load();
}

HWND GetActiveWindowFast() {
    return s_activeHwnd.load(std::memory_order_acquire);
}

} // namespace capture
