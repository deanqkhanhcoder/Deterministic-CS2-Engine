import sys
import re

with open("c:/Users/toanpq/Desktop/marco/src/core/input_capture.cpp", "r", encoding="utf-8") as f:
    code = f.read()

# 1. Add #include "ui_main.h" at the top
code = code.replace('#include "topology.h"', '#include "topology.h"\n#include "ui_main.h"')

# 2. Modify PhysicalEvent
old_phys = """struct PhysicalEvent {
    bool isMouse; // true for mouse, false for keyboard
    Key key;
    bool isDown;
    int64_t timestamp_enqueue_us;
    uint64_t generation_id;
};"""

new_phys = """struct PhysicalEvent {
    bool isMouse; // true for mouse, false for keyboard
    WORD vkCode;
    WORD scanCode;
    bool isDown;
    int64_t timestamp_enqueue_us;
    uint64_t generation_id;
};"""
code = code.replace(old_phys, new_phys)

# 3. Move global hotkeys state variables ABOVE WorkerThreadFunc
old_hk = """static bool s_hkDownF1 = false;
static bool s_hkDownF2 = false;
static bool s_hkDownF3 = false;
static bool s_hkDownF6 = false;
static bool s_hkDownF8 = false;"""

code = code.replace(old_hk, "") # remove from current location

# insert above WorkerThreadFunc
old_worker = "void WorkerThreadFunc() {"
new_worker = """static bool s_hkDownF1 = false;
static bool s_hkDownF2 = false;
static bool s_hkDownF3 = false;
static bool s_hkDownF6 = false;
static bool s_hkDownF8 = false;

void WorkerThreadFunc() {"""
code = code.replace(old_worker, new_worker)


# 4. Modify WorkerThreadFunc routing
old_worker_routing = """        // Delegate to state engine outside the queue lock!
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
            
            if (ev.isDown) {
                engine::HandleKeyDown(ev.key, routeThis);
            } else {
                engine::HandleKeyUp(ev.key, routeThis);
            }
        }"""

new_worker_routing = """        // Delegate to state engine outside the queue lock!
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
                if (ev.isDown && !s_hkDownF1) {
                    s_hkDownF1 = true;
                    bhop::ToggleEnabled();
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF1 = false;
                continue;
            }
            if (ev.vkCode == VK_F2) {
                if (ev.isDown && !s_hkDownF2) {
                    s_hkDownF2 = true;
                    bhop::CycleMode();
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF2 = false;
                continue;
            }
            if (ev.vkCode == VK_F3) {
                if (ev.isDown && !s_hkDownF3) {
                    s_hkDownF3 = true;
                    engine::CycleProfile();
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
                    HWND msgHwnd = capture::IsHookInstalled() ? s_hwnd : nullptr; // Actually, we can just find it
                    if (ui::GetMainHwnd()) {
                        PostMessageW(ui::GetMainHwnd(), WM_CLOSE, 0, 0);
                    }
                }
                else if (!ev.isDown) s_hkDownF8 = false;
                continue;
            }
            
            // --- 2. WASD ROUTING ---
            int keyIdx = ScanToKeyIndex(ev.scanCode);
            if (keyIdx >= 0) {
                Key k = static_cast<Key>(keyIdx);
                if (ev.isDown) {
                    engine::HandleKeyDown(k, routeThis);
                } else {
                    engine::HandleKeyUp(k, routeThis);
                }
                continue;
            }
            
            // --- 3. MODIFIER ROUTING ---
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
        }"""
code = code.replace(old_worker_routing, new_worker_routing)

# 5. Modify KeyboardProc
old_kb_proc = """    // --- 1. GLOBAL HOTKEYS (Before Focus Filter) ---
    if (vk == VK_F1) {
        if (isDown && !s_hkDownF1) { s_hkDownF1 = true; SendNotifyMessageW(s_hwnd, WM_BHOP_TOGGLE, 0, 0); }
        else if (isUp) s_hkDownF1 = false;
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F2) {
        if (isDown && !s_hkDownF2) { s_hkDownF2 = true; SendNotifyMessageW(s_hwnd, WM_BHOP_CYCLE_MODE, 0, 0); }
        else if (isUp) s_hkDownF2 = false;
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F3) {
        if (isDown && !s_hkDownF3) { s_hkDownF3 = true; SendNotifyMessageW(s_hwnd, WM_CYCLE_PROFILE, 0, 0); }
        else if (isUp) s_hkDownF3 = false;
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F6) {
        if (isDown && !s_hkDownF6) { s_hkDownF6 = true; SendNotifyMessageW(s_hwnd, WM_TOGGLE_SUSPEND, 0, 0); }
        else if (isUp) s_hkDownF6 = false;
        return CallNextHookEx(s_keyboardHook, nCode, wParam, lParam);
    }
    if (vk == VK_F8) {
        if (isDown && !s_hkDownF8) { s_hkDownF8 = true; SendNotifyMessageW(s_hwnd, WM_CLOSE, 0, 0); }
        else if (isUp) s_hkDownF8 = false;
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
        uint64_t gen = engine::dbgEventSeq.load(std::memory_order_relaxed);
        PushEvent({false, k, isDown, timing::NowUs(), gen});
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
    }"""

new_kb_proc = """    // --- 1. GLOBAL HOTKEYS (Before Focus Filter) ---
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
    }"""
code = code.replace(old_kb_proc, new_kb_proc)

# 6. Modify MouseProc
old_mouse_proc = """    // [FIX Bug #1] Mouse layer must also follow focus/suspend but never desync
    uint64_t gen = engine::dbgEventSeq.load(std::memory_order_relaxed);
    if (wParam == WM_LBUTTONDOWN) {
        PushEvent({true, Key::Mouse1, true, timing::NowUs(), gen});
    } else if (wParam == WM_LBUTTONUP) {
        PushEvent({true, Key::Mouse1, false, timing::NowUs(), gen});
    }"""

new_mouse_proc = """    // [FIX Bug #1] Mouse layer must also follow focus/suspend but never desync
    uint64_t gen = engine::dbgEventSeq.load(std::memory_order_relaxed);
    if (wParam == WM_LBUTTONDOWN) {
        PushEvent({true, 0, 0, true, timing::NowUs(), gen});
    } else if (wParam == WM_LBUTTONUP) {
        PushEvent({true, 0, 0, false, timing::NowUs(), gen});
    }"""
code = code.replace(old_mouse_proc, new_mouse_proc)

with open("c:/Users/toanpq/Desktop/marco/src/core/input_capture.cpp", "w", encoding="utf-8") as f:
    f.write(code)
print("done")
