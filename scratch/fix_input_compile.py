import sys

with open("c:/Users/toanpq/Desktop/marco/src/core/input_capture.cpp", "r", encoding="utf-8") as f:
    code = f.read()

# Fix 1: Add config_io.h for F3
if '#include "config_io.h"' not in code:
    code = code.replace('#include "ui_main.h"', '#include "ui_main.h"\n#include "config_io.h"')

# Fix 2: Move WorkerThreadFunc below ScanToKeyIndex
# Actually we can just add a forward declaration of ScanToKeyIndex at the top of WorkerThreadFunc, or before it.
if "static int ScanToKeyIndex(DWORD scanCode);" not in code:
    code = code.replace("void WorkerThreadFunc() {", "static int ScanToKeyIndex(DWORD scanCode);\n\nvoid WorkerThreadFunc() {")

# Fix 3: F3 Logic
old_f3 = """            if (ev.vkCode == VK_F3) {
                if (ev.isDown && !s_hkDownF3) {
                    s_hkDownF3 = true;
                    engine::CycleProfile();
                    ui::OnStateChanged();
                }
                else if (!ev.isDown) s_hkDownF3 = false;
                continue;
            }"""

new_f3 = """            if (ev.vkCode == VK_F3) {
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
            }"""
code = code.replace(old_f3, new_f3)

# Fix 4: F8 Logic
old_f8 = """            if (ev.vkCode == VK_F8) {
                if (ev.isDown && !s_hkDownF8) {
                    s_hkDownF8 = true;
                    HWND msgHwnd = capture::IsHookInstalled() ? s_hwnd : nullptr; // Actually, we can just find it
                    if (ui::GetMainHwnd()) {
                        PostMessageW(ui::GetMainHwnd(), WM_CLOSE, 0, 0);
                    }
                }
                else if (!ev.isDown) s_hkDownF8 = false;
                continue;
            }"""

new_f8 = """            if (ev.vkCode == VK_F8) {
                if (ev.isDown && !s_hkDownF8) {
                    s_hkDownF8 = true;
                    if (s_hwnd) {
                        PostMessageW(s_hwnd, WM_CLOSE, 0, 0);
                    }
                }
                else if (!ev.isDown) s_hkDownF8 = false;
                continue;
            }"""
code = code.replace(old_f8, new_f8)

with open("c:/Users/toanpq/Desktop/marco/src/core/input_capture.cpp", "w", encoding="utf-8") as f:
    f.write(code)
print("done")
