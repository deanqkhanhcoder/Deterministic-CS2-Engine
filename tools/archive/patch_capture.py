import re

with open('src/core/input_capture.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Add mutex to IsTargetActive
focus_mutex_decl = "static std::mutex s_focusMutex;\nstatic bool IsTargetActive() {"
content = re.sub(r'static bool IsTargetActive\(\) \{', focus_mutex_decl, content)

focus_lock = "    std::lock_guard<std::mutex> lock(s_focusMutex);\n    HWND fg"
content = re.sub(r'    HWND fg = s_activeHwnd\.load\(std::memory_order_acquire\);', focus_lock, content, count=1)

# 2. Add timestamps for hotkeys
timestamps_decl = """static bool s_hkDownF8 = false;

static int64_t s_hkLastDownF1 = 0;
static int64_t s_hkLastDownF2 = 0;
static int64_t s_hkLastDownF3 = 0;
static int64_t s_hkLastDownF6 = 0;
static int64_t s_hkLastDownF8 = 0;"""
content = re.sub(r'static bool s_hkDownF8 = false;', timestamps_decl, content)

# 3. Fix F1 logic
f1_old = r"""                if \(ev\.isDown && !s_hkDownF1\) \{
                    s_hkDownF1 = true;
                    bhop::ToggleEnabled\(\);
                    ui::OnStateChanged\(\);
                \}
                else if \(!ev\.isDown\) s_hkDownF1 = false;"""
f1_new = """                bool isAutoRepeat = s_hkDownF1 && (ev.timestamp_enqueue_us - s_hkLastDownF1 < 500000);
                if (ev.isDown) {
                    s_hkLastDownF1 = ev.timestamp_enqueue_us;
                    if (!isAutoRepeat) {
                        s_hkDownF1 = true;
                        bhop::ToggleEnabled();
                        ui::OnStateChanged();
                    }
                } else {
                    s_hkDownF1 = false;
                }"""
content = re.sub(f1_old, f1_new, content)

# 4. Fix F2 logic
f2_old = r"""                if \(ev\.isDown && !s_hkDownF2\) \{
                    s_hkDownF2 = true;
                    bhop::CycleMode\(\);
                    ui::OnStateChanged\(\);
                \}
                else if \(!ev\.isDown\) s_hkDownF2 = false;"""
f2_new = """                bool isAutoRepeat = s_hkDownF2 && (ev.timestamp_enqueue_us - s_hkLastDownF2 < 500000);
                if (ev.isDown) {
                    s_hkLastDownF2 = ev.timestamp_enqueue_us;
                    if (!isAutoRepeat) {
                        s_hkDownF2 = true;
                        bhop::CycleMode();
                        ui::OnStateChanged();
                    }
                } else {
                    s_hkDownF2 = false;
                }"""
content = re.sub(f2_old, f2_new, content)

# 5. Fix F3 logic
f3_old = r"""                if \(ev\.isDown && !s_hkDownF3\) \{
                    s_hkDownF3 = true;
                    RuntimeConfig& cfg = rcfg::GetMutable\(\);
                    cfg\.activeBrakeProfileIndex = \(cfg\.activeBrakeProfileIndex % 4\) \+ 1;
                    rcfg::Apply\(cfg\);
                    config_io::Save\(cfg\);
                    ui::OnStateChanged\(\);
                \}
                else if \(!ev\.isDown\) s_hkDownF3 = false;"""
f3_new = """                bool isAutoRepeat = s_hkDownF3 && (ev.timestamp_enqueue_us - s_hkLastDownF3 < 500000);
                if (ev.isDown) {
                    s_hkLastDownF3 = ev.timestamp_enqueue_us;
                    if (!isAutoRepeat) {
                        s_hkDownF3 = true;
                        RuntimeConfig& cfg = rcfg::GetMutable();
                        cfg.activeBrakeProfileIndex = (cfg.activeBrakeProfileIndex % 4) + 1;
                        rcfg::Apply(cfg);
                        config_io::Save(cfg);
                        ui::OnStateChanged();
                    }
                } else {
                    s_hkDownF3 = false;
                }"""
content = re.sub(f3_old, f3_new, content)

# 6. Fix F6 logic
f6_old = r"""                if \(ev\.isDown && !s_hkDownF6\) \{
                    s_hkDownF6 = true;
                    engine::ToggleSuspend\(\);
                    bhop::OnSuspendChanged\(\);
                    ui::OnStateChanged\(\);
                \}
                else if \(!ev\.isDown\) s_hkDownF6 = false;"""
f6_new = """                bool isAutoRepeat = s_hkDownF6 && (ev.timestamp_enqueue_us - s_hkLastDownF6 < 500000);
                if (ev.isDown) {
                    s_hkLastDownF6 = ev.timestamp_enqueue_us;
                    if (!isAutoRepeat) {
                        s_hkDownF6 = true;
                        engine::ToggleSuspend();
                        bhop::OnSuspendChanged();
                        ui::OnStateChanged();
                    }
                } else {
                    s_hkDownF6 = false;
                }"""
content = re.sub(f6_old, f6_new, content)

# 7. Fix F8 logic
f8_old = r"""                if \(ev\.isDown && !s_hkDownF8\) \{
                    s_hkDownF8 = true;
                    if \(s_hwnd\) \{
                        PostMessageW\(s_hwnd, WM_CLOSE, 0, 0\);
                    \}
                \}
                else if \(!ev\.isDown\) s_hkDownF8 = false;"""
f8_new = """                bool isAutoRepeat = s_hkDownF8 && (ev.timestamp_enqueue_us - s_hkLastDownF8 < 500000);
                if (ev.isDown) {
                    s_hkLastDownF8 = ev.timestamp_enqueue_us;
                    if (!isAutoRepeat) {
                        s_hkDownF8 = true;
                        if (s_hwnd) {
                            PostMessageW(s_hwnd, WM_CLOSE, 0, 0);
                        }
                    }
                } else {
                    s_hkDownF8 = false;
                }"""
content = re.sub(f8_old, f8_new, content)

# Remove ALL PHASE logs
content = re.sub(r'[ \t]*DLOG_ERR\(Hook, "PHASE_\d+.*?\);\n', '', content)
content = re.sub(r'[ \t]*DLOG_TRACE\(Hook, "\[FIRE_TRACE\] EVENT_ENQUEUE.*?\);\n', '', content)
content = re.sub(r'[ \t]*DLOG_TRACE\(Hook, "\[FIRE_TRACE\] EVENT_DEQUEUE.*?\);\n', '', content)

with open('src/core/input_capture.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
print("Patched successfully")
