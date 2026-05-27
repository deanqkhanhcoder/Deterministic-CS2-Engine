// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Settings Tab                                     ║
// ║  Owner-drawn settings with Apply/Save/Reset buttons                 ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui_theme.h"
#include "ui_layout.h"
#include "runtime_config.h"
#include "config_io.h"
#include "debug_logger.h"
#include "bhop.h"
#include "movement_reconstruction.h"
#include <windows.h>
#include <cstdio>

namespace ui {
    HFONT GetTitleFont();
    HFONT GetBodyFont();
    HFONT GetSmallFont();
    HBRUSH GetPanelBrush();
    HWND GetMainHwnd();
    HINSTANCE GetHInst();
    int GetLineHeight(HDC hdc, HFONT font);
}

namespace ui_sett {

// Button IDs
enum { IDB_APPLY = 2001, IDB_SAVE, IDB_RESET, IDB_RIFLE, IDB_PISTOL, IDB_SNIPER, IDB_SMG, IDB_SAFE_MODE };
static HWND s_btnApply = nullptr, s_btnSave = nullptr, s_btnReset = nullptr, s_btnSafeMode = nullptr;
static HWND s_btnRifle = nullptr, s_btnPistol = nullptr, s_btnSniper = nullptr, s_btnSMG = nullptr;

static int s_scrollY = 0;
static int s_totalContentHeight = 0;
static int s_viewportHeight = 0;

static HBRUSH s_trackBrush = nullptr;
static HBRUSH s_thumbBrush = nullptr;

static void EnsureGdiObjects() {
    if (!s_trackBrush) s_trackBrush = CreateSolidBrush(RGB(40, 40, 40));
    if (!s_thumbBrush) s_thumbBrush = CreateSolidBrush(RGB(100, 100, 100));
}

void OnMouseWheel(int delta) {
    int rowH = 20;
    int step = (delta > 0) ? -3 * rowH : 3 * rowH;
    s_scrollY += step;
    int maxScroll = s_totalContentHeight - s_viewportHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scrollY < 0) s_scrollY = 0;
    if (s_scrollY > maxScroll) s_scrollY = maxScroll;
}

static void PaintScrollbar(HDC hdc, const RECT& viewport) {
    if (s_totalContentHeight <= s_viewportHeight) return;

    int trackX = viewport.right - theme::SCROLLBAR_W - 2;
    int trackY = viewport.top;
    int trackH = viewport.bottom - viewport.top;
    int trackW = theme::SCROLLBAR_W;

    RECT trackRect = { trackX, trackY, trackX + trackW, trackY + trackH };
    FillRect(hdc, &trackRect, s_trackBrush);

    int calcThumbH = (int)((int64_t)trackH * s_viewportHeight / s_totalContentHeight);
    int thumbH = (calcThumbH > 20) ? calcThumbH : 20;
    int maxScroll = s_totalContentHeight - s_viewportHeight;
    int thumbY = trackY;
    if (maxScroll > 0) {
        thumbY = trackY + (int)((int64_t)(trackH - thumbH) * s_scrollY / maxScroll);
    }

    RECT thumbRect = { trackX, thumbY, trackX + trackW, thumbY + thumbH };
    FillRect(hdc, &thumbRect, s_thumbBrush);
}


void Init() {
    HWND hw = ui::GetMainHwnd();
    HINSTANCE hi = ui::GetHInst();
    
    // 1. Asymmetric Button Sizing
    int presetBtnW = 55; // Compact, industrial presets
    int actionBtnW = 82; // Wider, distinct actions
    int btnH = 26;
    int btnGap = 6;
    
    int tabBottom = theme::WIN_H - theme::STATUS_BAR_H;
    int footerTextY = tabBottom - 20;
    int baseY = footerTextY - btnH - 12;
    
    // 2. Group Isolation & Mathematical Spacing
    int leftBaseX = theme::MARGIN;
    int rightBaseX = theme::WIN_W - theme::MARGIN - (actionBtnW * 3 + btnGap * 2);

    // Actions (Right Block)
    s_btnApply = CreateWindowExW(0, L"BUTTON", L"APPLY",
        WS_CHILD | BS_OWNERDRAW, rightBaseX, baseY, actionBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_APPLY, hi, nullptr);
    s_btnSave = CreateWindowExW(0, L"BUTTON", L"SAVE",
        WS_CHILD | BS_OWNERDRAW, rightBaseX + actionBtnW + btnGap, baseY, actionBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_SAVE, hi, nullptr);
    s_btnReset = CreateWindowExW(0, L"BUTTON", L"RESET",
        WS_CHILD | BS_OWNERDRAW, rightBaseX + (actionBtnW + btnGap)*2, baseY, actionBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_RESET, hi, nullptr);

    // Presets (Left Block)
    s_btnRifle = CreateWindowExW(0, L"BUTTON", L"RIFLE",
        WS_CHILD | BS_OWNERDRAW, leftBaseX, baseY, presetBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_RIFLE, hi, nullptr);
    s_btnPistol = CreateWindowExW(0, L"BUTTON", L"PISTOL",
        WS_CHILD | BS_OWNERDRAW, leftBaseX + presetBtnW + btnGap, baseY, presetBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_PISTOL, hi, nullptr);
    s_btnSniper = CreateWindowExW(0, L"BUTTON", L"SNIPER",
        WS_CHILD | BS_OWNERDRAW, leftBaseX + (presetBtnW + btnGap)*2, baseY, presetBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_SNIPER, hi, nullptr);
    s_btnSMG = CreateWindowExW(0, L"BUTTON", L"SMG",
        WS_CHILD | BS_OWNERDRAW, leftBaseX + (presetBtnW + btnGap)*3, baseY, presetBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_SMG, hi, nullptr);
    s_btnSafeMode = CreateWindowExW(0, L"BUTTON", L"SAFE: OFF",
        WS_CHILD | BS_OWNERDRAW, leftBaseX + (presetBtnW + btnGap)*4, baseY, actionBtnW, btnH,
        hw, (HMENU)(UINT_PTR)IDB_SAFE_MODE, hi, nullptr);
}

void ShowButtons(bool show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    if (s_btnApply) ShowWindow(s_btnApply, cmd);
    if (s_btnSave)  ShowWindow(s_btnSave, cmd);
    if (s_btnReset) ShowWindow(s_btnReset, cmd);
    if (s_btnSafeMode) ShowWindow(s_btnSafeMode, cmd);
    if (s_btnRifle) ShowWindow(s_btnRifle, cmd);
    if (s_btnPistol)  ShowWindow(s_btnPistol, cmd);
    if (s_btnSniper) ShowWindow(s_btnSniper, cmd);
    if (s_btnSMG)    ShowWindow(s_btnSMG, cmd);
}

void Paint(HDC hdc, RECT rc) {
    EnsureGdiObjects();

    const RuntimeConfig& cfg = rcfg::Get();
    wchar_t buf[64];
    
    if (s_btnSafeMode) {
        SetWindowTextW(s_btnSafeMode, cfg.safeModeEnabled ? L"SAFE: ON" : L"SAFE: OFF");
    }

    int rowH = ui::GetLineHeight(hdc, ui::GetBodyFont()) + theme::ROW_PAD_V;

    // Viewport dimensions (leave space for buttons and footer text)
    int tabBottom = rc.bottom;
    int footerTextY = tabBottom - 20;
    int baseY = footerTextY - 26 - 12;
    int viewportBottom = baseY - 12;
    s_viewportHeight = viewportBottom - (rc.top + theme::MARGIN);

    // Pass 1: measure total content height
    int p1H = layout::PanelHeight(8 * rowH);
    int p_evoH = layout::PanelHeight(7 * rowH);
    int p2H = layout::PanelHeight(4 * rowH);
    int p3H = layout::PanelHeight(3 * rowH);
    
    s_totalContentHeight = p1H + theme::GROUP_PAD + p_evoH + theme::GROUP_PAD + p2H + theme::GROUP_PAD + p3H;

    // Clamp scroll
    int maxScroll = s_totalContentHeight - s_viewportHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scrollY > maxScroll) s_scrollY = maxScroll;
    if (s_scrollY < 0) s_scrollY = 0;

    // Viewport rect for clipping
    RECT viewRect = rc;
    viewRect.bottom = viewportBottom;
    layout::ClipGuard tabClip(hdc, viewRect);

    int x = rc.left + theme::MARGIN;
    int w = (rc.right - rc.left) - theme::MARGIN * 2;
    if (s_totalContentHeight > s_viewportHeight) w -= theme::SCROLLBAR_W + 4;

    int y = rc.top + theme::MARGIN - s_scrollY;

    // ── Panel 1: Counter-Strafe Settings (8 rows) ──
    RECT p1Rect = { x, y, x + w, y + p1H };
    if (p1Rect.bottom >= viewRect.top && p1Rect.top <= viewRect.bottom) {
        RECT p1Inner = layout::DrawPanel(hdc, p1Rect, L"COUNTER-STRAFE TUNING");
        layout::ClipGuard panelClip(hdc, p1Rect);
        int py = p1Inner.top;
        int px = p1Inner.left;
        int pw = p1Inner.right - p1Inner.left;

        swprintf(buf, 64, L"%d ms", cfg.latencyMarginMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Latency Margin", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.quickTapMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Quick Tap", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.maxScaleMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Max Scale", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%.2f", cfg.crouchMult);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Crouch Multiplier", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.tapDelayMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Tap Delay", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.sprayDelayMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Spray Delay", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.minStopMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Min Stop", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.walkMemoryMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Walk Memory", buf, theme::FG_VALUE);
    }
    y += p1H + theme::GROUP_PAD;

    // ── Panel 2: Movement Evolution (5 rows) ──
    RECT p_evoRect = { x, y, x + w, y + p_evoH };
    if (p_evoRect.bottom >= viewRect.top && p_evoRect.top <= viewRect.bottom) {
        RECT p_evoInner = layout::DrawPanel(hdc, p_evoRect, L"MOVEMENT EVOLUTION");
        layout::ClipGuard panelClip(hdc, p_evoRect);
        int py = p_evoInner.top;
        int px = p_evoInner.left;
        int pw = p_evoInner.right - p_evoInner.left;

        swprintf(buf, 64, L"%d µs", cfg.hardwareDebounceUs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Hardware Debounce", buf, theme::FG_VALUE); py += rowH;
        
        swprintf(buf, 64, L"%d to %d µs", cfg.humanizeMinUs, cfg.humanizeMaxUs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Humanize Range", buf, theme::FG_VALUE); py += rowH;
        
        const wchar_t* names[] = { L"OFF", L"RIFLE", L"PISTOL", L"SNIPER", L"SMG" };
        layout::DrawRow(hdc, px, py, pw, rowH, L"Active Profile", names[cfg.activeBrakeProfileIndex], theme::CLR_MODE); py += rowH;

        const auto& prof = cfg.brakeProfiles[cfg.activeBrakeProfileIndex];
        
        swprintf(buf, 64, L"%d µs", (int)prof.overlap_duration_us);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Overlap Duration", buf, theme::FG_VALUE); py += rowH;

        swprintf(buf, 64, L"%.2fx", prof.brake_bias_multiplier);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Brake Bias Mult", buf, theme::FG_VALUE); py += rowH;

        swprintf(buf, 64, L"%+d ms", (int)prof.authority_bias_ms);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Authority Bias", buf, theme::FG_VALUE); py += rowH;

        swprintf(buf, 64, L"%.2fx", prof.aggressiveness_curve);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Aggr. Curve", buf, theme::FG_VALUE);
    }
    y += p_evoH + theme::GROUP_PAD;

    // ── Panel 3: Bhop Settings (4 rows) ──
    RECT p2Rect = { x, y, x + w, y + p2H };
    if (p2Rect.bottom >= viewRect.top && p2Rect.top <= viewRect.bottom) {
        RECT p2Inner = layout::DrawPanel(hdc, p2Rect, L"BHOP ENGINE TIMING");
        layout::ClipGuard panelClip(hdc, p2Rect);
        int py = p2Inner.top;
        int px = p2Inner.left;
        int pw = p2Inner.right - p2Inner.left;

        const char* modeNames[] = {"", "LEGIT", "AGGRESSIVE", "HUMANIZED", "SCROLL_EMU"};
        int mi = cfg.bhopMode; if (mi < 1 || mi > 4) mi = 4;
        wchar_t modeW[32]; MultiByteToWideChar(CP_UTF8, 0, modeNames[mi], -1, modeW, 32);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Default Mode", modeW, theme::CLR_MODE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.airborneDelayMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Airborne Delay", buf, theme::FG_VALUE); py += rowH;
        swprintf(buf, 64, L"%d ms", cfg.scrollBurstGapMs);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Scroll Burst Gap", buf, theme::FG_VALUE); py += rowH;

        const auto& mt = cfg.modeCfg[mi];
        swprintf(buf, 64, L"H[%d-%d] D[%d-%d] ms", mt.hMin, mt.hMax, mt.dMin, mt.dMax);
        layout::DrawRow(hdc, px, py, pw, rowH, L"Mode Timing", buf, theme::FG_VALUE);
    }
    y += p2H + theme::GROUP_PAD;

    // ── Panels 3 & 4: Physics + Application (side by side, 3 rows each) ──
    int halfW = (w - 12) / 2;
    RECT pLeft = { x, y, x + halfW, y + p3H };
    RECT pRight = { x + halfW + 12, y, x + w, y + p3H };
    
    if (y + p3H >= viewRect.top && y <= viewRect.bottom) {
        // Physics panel (left)
        RECT physInner = layout::DrawPanel(hdc, pLeft, L"PHYSICS");
        {
            layout::ClipGuard panelClip(hdc, pLeft);
            int py = physInner.top;
            int px = physInner.left;
            int pw = physInner.right - physInner.left;

            swprintf(buf, 64, L"%.1f", cfg.physMaxSpeed);
            layout::DrawRow(hdc, px, py, pw, rowH, L"Max Speed", buf, theme::FG_VALUE); py += rowH;
            swprintf(buf, 64, L"%.1f", cfg.physFriction);
            layout::DrawRow(hdc, px, py, pw, rowH, L"Friction", buf, theme::FG_VALUE); py += rowH;
            swprintf(buf, 64, L"%.1f", cfg.physStopSpeed);
            layout::DrawRow(hdc, px, py, pw, rowH, L"Stop Speed", buf, theme::FG_VALUE);
        }

        // Application panel (right)
        RECT appInner = layout::DrawPanel(hdc, pRight, L"APPLICATION");
        {
            layout::ClipGuard panelClip(hdc, pRight);
            int py = appInner.top;
            int px = appInner.left;
            int pw = appInner.right - appInner.left;

            layout::DrawRow(hdc, px, py, pw, rowH, L"Minimize to Tray",
                cfg.minimizeToTray ? L"Yes" : L"No", theme::FG_VALUE); py += rowH;
            swprintf(buf, 64, L"%d ms", cfg.dashboardRefreshMs);
            layout::DrawRow(hdc, px, py, pw, rowH, L"Refresh Rate", buf, theme::FG_VALUE); py += rowH;
            layout::DrawRow(hdc, px, py, pw, rowH, L"Safe Mode",
                cfg.safeModeEnabled ? L"ACTIVE" : L"Off", cfg.safeModeEnabled ? theme::CLR_WARN : theme::FG_VALUE);
        }
    }
    y += p3H + theme::GROUP_PAD;

    // ── Footer note ──
    RECT footerR = { rc.left + theme::MARGIN, footerTextY, rc.right - theme::MARGIN, footerTextY + 20 };
    layout::DrawTextSafe(hdc, footerR, L"Edit config.ini to change values. Apply reloads.",
                         theme::FG_DIM, ui::GetSmallFont(), DT_LEFT | DT_VCENTER);

    PaintScrollbar(hdc, viewRect);
}

void OnCommand(HWND hwnd, WPARAM wp) {
    WORD id = LOWORD(wp);
    switch (id) {
        case IDB_APPLY: {
            RuntimeConfig cfg{};
            if (config_io::Load(cfg)) {
                rcfg::Apply(cfg);
                DLOG_INFO(Config, "Config applied from INI");
            } else {
                DLOG_WARN(Config, "No INI found, using defaults");
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDB_SAVE: {
            config_io::Save(rcfg::GetMutable());
            DLOG_INFO(Config, "Config saved to %ls", reinterpret_cast<int64_t>(config_io::GetConfigPath()));
            break;
        }
        case IDB_RESET: {
            RuntimeConfig defaults{};
            rcfg::Apply(defaults);
            config_io::Save(defaults);
            DLOG_INFO(Config, "Config reset to defaults");
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDB_RIFLE: {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = 1;
            config_io::Save(cfg); rcfg::Apply(cfg);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDB_PISTOL: {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = 2;
            config_io::Save(cfg); rcfg::Apply(cfg);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDB_SNIPER: {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = 3;
            config_io::Save(cfg); rcfg::Apply(cfg);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDB_SMG: {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = 4;
            config_io::Save(cfg); rcfg::Apply(cfg);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
        case IDB_SAFE_MODE: {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.safeModeEnabled = !cfg.safeModeEnabled;
            config_io::Save(cfg);
            rcfg::Apply(cfg);
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }
    }
}

} // namespace ui_sett
