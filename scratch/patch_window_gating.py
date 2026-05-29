import sys
import re

file_path = "c:/Users/toanpq/Desktop/marco/src/core/input_capture.cpp"

with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# 1. Fix IsTargetActive()
old_isActive = "bool isActive = true; // (fg == pub.hwnd && pub.hwnd != nullptr);"
new_isActive = "bool isActive = (fg == pub.hwnd && pub.hwnd != nullptr && pub.IsValid());\n    if (isActive) DLOG_TRACE(Hook, \"WINDOW_ACCEPT\");\n    else DLOG_TRACE(Hook, \"WINDOW_REJECT\");"

content = content.replace(old_isActive, new_isActive)

# 2. Fix global hotkeys
# F1
content = content.replace(
"""            if (ev.vkCode == VK_F1) {
                if (ev.isDown && !s_hkDownF1) {""",
"""            if (ev.vkCode == VK_F1) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS F1"); continue; }
                if (ev.isDown && !s_hkDownF1) {""")

# F2
content = content.replace(
"""            if (ev.vkCode == VK_F2) {
                if (ev.isDown && !s_hkDownF2) {""",
"""            if (ev.vkCode == VK_F2) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS F2"); continue; }
                if (ev.isDown && !s_hkDownF2) {""")

# F3
content = content.replace(
"""            if (ev.vkCode == VK_F3) {
                if (ev.isDown && !s_hkDownF3) {""",
"""            if (ev.vkCode == VK_F3) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS F3"); continue; }
                if (ev.isDown && !s_hkDownF3) {""")

# 3. Bypass WASD
content = content.replace(
"""            // --- 2. WASD ROUTING ---
            int keyIdx = ScanToKeyIndex(ev.scanCode);
            if (keyIdx >= 0) {
                Key k = static_cast<Key>(keyIdx);""",
"""            // --- 2. WASD ROUTING ---
            int keyIdx = ScanToKeyIndex(ev.scanCode);
            if (keyIdx >= 0) {
                if (!isActive) { DLOG_TRACE(Hook, "ENGINE_BYPASS WASD"); continue; }
                Key k = static_cast<Key>(keyIdx);""")

# 4. Bypass Modifiers
content = content.replace(
"""            // --- 3. MODIFIER ROUTING ---
            if (ev.vkCode == VK_LCONTROL) {""",
"""            // --- 3. MODIFIER ROUTING ---
            if (!isActive) {
                if (ev.vkCode == VK_LCONTROL || ev.scanCode == 0x2E || ev.vkCode == VK_LSHIFT || ev.vkCode == VK_SPACE) {
                    DLOG_TRACE(Hook, "ENGINE_BYPASS MODIFIER/SPACE");
                    continue;
                }
            }
            if (ev.vkCode == VK_LCONTROL) {""")

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)

print("done")
