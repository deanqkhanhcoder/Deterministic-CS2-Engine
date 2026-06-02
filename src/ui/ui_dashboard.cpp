// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Dashboard Tab                                    ║
// ║  Scrollable telemetry dashboard with retained-mode layout           ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui_theme.h"
#include "ui_layout.h"
#include "runtime_state.h"
#include "bhop.h"
#include "types.h"
#include "telemetry.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <algorithm>

#include "timing.h"

namespace ui {
    HFONT GetTitleFont();
    HFONT GetBodyFont();
    HFONT GetSmallFont();
    HBRUSH GetPanelBrush();
    int GetLineHeight(HDC hdc, HFONT font);
}

namespace ui_dash {

// ════════════════════════════════════════════════════════════════
//  SCROLL STATE
// ════════════════════════════════════════════════════════════════
static int s_scrollY = 0;
static int s_totalContentHeight = 0;
static int s_viewportHeight = 0;

static HBRUSH s_trackBrush = nullptr;
static HBRUSH s_thumbBrush = nullptr;
static HPEN s_borderPen = nullptr;

static void EnsureGdiObjects() {
    if (!s_trackBrush) s_trackBrush = CreateSolidBrush(RGB(40, 40, 40));
    if (!s_thumbBrush) s_thumbBrush = CreateSolidBrush(RGB(100, 100, 100));
    if (!s_borderPen) s_borderPen = CreatePen(PS_SOLID, 1, theme::CLR_BORDER);
}

void OnMouseWheel(int delta) {
    int rowH = 20;  // Approximate row height for scroll step
    int step = (delta > 0) ? -3 * rowH : 3 * rowH;
    s_scrollY += step;
    int maxScroll = s_totalContentHeight - s_viewportHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scrollY < 0) s_scrollY = 0;
    if (s_scrollY > maxScroll) s_scrollY = maxScroll;
}

// ════════════════════════════════════════════════════════════════
//  SCROLLBAR RENDERING
// ════════════════════════════════════════════════════════════════
static void PaintScrollbar(HDC hdc, const RECT& viewport) {
    if (s_totalContentHeight <= s_viewportHeight) return;  // No scrollbar needed

    int trackX = viewport.right - theme::SCROLLBAR_W - 2;
    int trackY = viewport.top;
    int trackH = viewport.bottom - viewport.top;
    int trackW = theme::SCROLLBAR_W;

    // Track background
    RECT trackRect = { trackX, trackY, trackX + trackW, trackY + trackH };
    FillRect(hdc, &trackRect, s_trackBrush);

    // Thumb
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

// ════════════════════════════════════════════════════════════════
//  LAYOUT MEASUREMENT (Pass 1)
//  Calculates total content height at PREFERRED sizes.
//  NO shrinking. NO compression. Every panel gets what it needs.
// ════════════════════════════════════════════════════════════════
struct DashboardLayout {
    int rowH;           // Measured row height

    int statusPanelH;   // Status + Thread Health (side by side)
    int threadHealthH;  // Thread Health

    int totalHeight;    // Total content height including all gaps
};

static DashboardLayout MeasureLayout(HDC hdc) {
    DashboardLayout L = {};

    L.rowH = ui::GetLineHeight(hdc, ui::GetBodyFont()) + theme::ROW_PAD_V;
    
    // Panel heights at preferred sizes (using PanelHeight for correct chrome)
    // SYSTEM & CORE STATUS has 8 rows, ACTIVE WEAPON & MOVEMENT TUNING has 9 rows.
    // The panel height must tightly wrap the largest row count to prevent clipping.
    L.statusPanelH = layout::PanelHeight(9 * L.rowH);
    L.threadHealthH = layout::PanelHeight(2 * L.rowH);
    
    // Total with all gaps
    L.totalHeight = L.statusPanelH + theme::GROUP_PAD + L.threadHealthH;


    return L;
}

// ════════════════════════════════════════════════════════════════
//  PAINT (Pass 2 — Render at measured sizes with scroll offset)
// ════════════════════════════════════════════════════════════════
void Paint(HDC hdc, RECT rc, const RuntimeSnapshot& snap) {
    EnsureGdiObjects();
    // Clip entire tab to content region
    layout::ClipGuard tabClip(hdc, rc);
    SetBkMode(hdc, TRANSPARENT);

    // Viewport dimensions (inside margins)
    int marginX = theme::MARGIN;
    int marginY = theme::MARGIN;
    int viewW = (rc.right - rc.left) - marginX * 2;
    s_viewportHeight = (rc.bottom - rc.top) - marginY * 2;

    int rowH = ui::GetLineHeight(hdc, ui::GetBodyFont()) + theme::ROW_PAD_V;
    int statusPanelH = layout::PanelHeight(9 * rowH);

    // ── Pass 1: Measure ──
    DashboardLayout L = MeasureLayout(hdc);

    s_totalContentHeight = L.totalHeight;

    // Clamp scroll
    int maxScroll = s_totalContentHeight - s_viewportHeight;
    if (maxScroll < 0) maxScroll = 0;
    if (s_scrollY > maxScroll) s_scrollY = maxScroll;
    if (s_scrollY < 0) s_scrollY = 0;

    // Virtual cursor: starts at top margin, offset by scroll
    int x = rc.left + marginX;
    int y = rc.top + marginY - s_scrollY;
    int contentW = viewW - (s_totalContentHeight > s_viewportHeight ? theme::SCROLLBAR_W + 4 : 0);

    // ════════════════════════════════════════════════════════════════
        // ════════════════════════════════════════════════════════════════
    //  ROW 1: System Status | Weapon & Engine Tuning
    // ════════════════════════════════════════════════════════════════
    {
        int halfW = (contentW - 16) / 2;
        RECT leftPanel = { x, y, x + halfW, y + statusPanelH };
        RECT rightPanel = { x + halfW + 16, y, x + contentW, y + statusPanelH };

        // LEFT: System & Core Status
        if (leftPanel.bottom >= rc.top && leftPanel.top <= rc.bottom) {
            RECT leftInner = layout::DrawPanel(hdc, leftPanel, L"SYSTEM & CORE STATUS");
            {
            layout::ClipGuard panelClip(hdc, leftPanel);
            int py = leftInner.top;
            int rx = leftInner.left;
            int rwLabel = 140;
            int rwVal = 116;

            using target_platform::MASK_CS2;
            using target_platform::MASK_ROBLOX;
            uint32_t mask = snap.runningGamesMask;
            std::wstring gameStr = L"WAITING";
            COLORREF gameClr = theme::CLR_WARN;
            if ((mask & MASK_CS2) && (mask & MASK_ROBLOX)) {
                gameStr = L"CS2 | ROBLOX"; gameClr = theme::CLR_ON;
            } else if (mask & MASK_CS2) {
                gameStr = L"CS2"; gameClr = theme::CLR_ON;
            } else if (mask & MASK_ROBLOX) {
                gameStr = L"ROBLOX"; gameClr = theme::CLR_ON;
            }
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Game Focus", gameStr.c_str(), gameClr);
            py += rowH;

            wchar_t uptimeW[32];
            swprintf_s(uptimeW, L"%.1f min", snap.uptimeMs / 60000.0);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Session Uptime", uptimeW, theme::FG_VALUE);
            py += rowH;

            const wchar_t* affNames[] = { L"Competitive", L"Balanced", L"Low CPU" };
            uint32_t affIdx = snap.affinityMode;
            if (affIdx > 2) affIdx = 1;
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Precision Mode", affNames[affIdx], theme::CLR_ACCENT);
            py += rowH;

            wchar_t timingCoreW[32];
            if (snap.activeTimingCore == 0xFFFFFFFF) wcscpy_s(timingCoreW, L"OFFLINE");
            else wsprintfW(timingCoreW, L"Core %u", snap.activeTimingCore);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Active Timing Core", timingCoreW, theme::FG_VALUE);
            py += rowH;

            wchar_t hookCoreW[32];
            if (snap.activeHookCore == 0xFFFFFFFF) wcscpy_s(hookCoreW, L"OFFLINE");
            else wsprintfW(hookCoreW, L"Core %u", snap.activeHookCore);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Active Hook Core", hookCoreW, theme::FG_VALUE);
            py += rowH;
            
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Engine State", 
                                  (snap.runtimeState == RuntimeState::Detached) ? L"DETACHED" : 
                                  (snap.runtimeState == RuntimeState::Attached) ? L"ATTACHED" : 
                                  (snap.runtimeState == RuntimeState::ActiveTiming) ? L"ACTIVE TIMING" : L"FAIL-SAFE",
                                  (snap.runtimeState == RuntimeState::ActiveTiming) ? theme::CLR_ON : theme::FG_DIM);
            py += rowH;

            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"SMT Collision",
                                  snap.smtCollision ? L"COLLISION!" : L"OK",
                                  snap.smtCollision ? theme::CLR_OFF : theme::CLR_ON);
            }
        } // end LEFT

        // RIGHT: Active Weapon Profile & Movement Engine Tuning
        if (rightPanel.bottom >= rc.top && rightPanel.top <= rc.bottom) {
            RECT rightInner = layout::DrawPanel(hdc, rightPanel, L"WEAPON & MOVEMENT TUNING");
            {
            layout::ClipGuard panelClip(hdc, rightPanel);
            int py = rightInner.top;
            int rx = rightInner.left;
            int rwLabel = 140;
            int rwVal = 116;

            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Macro Status",
                                  snap.suspended ? L"PAUSED" : L"ACTIVE",
                                  snap.suspended ? theme::CLR_WARN : theme::CLR_ON);
            py += rowH;

            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Active Profile",
                                  snap.activeBrakeProfileName, theme::CLR_MODE);
            py += rowH;


            wchar_t valW[64];
            swprintf_s(valW, L"%lld µs", snap.profileOverlapUs);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Overlap Duration", valW, theme::FG_VALUE);
            py += rowH;

            swprintf_s(valW, L"%.2fx", snap.profileBrakeBias);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Brake Bias", valW, theme::FG_VALUE);
            py += rowH;

            swprintf_s(valW, L"%+d ms", (int)snap.profileAuthorityBiasMs);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Authority Bias", valW, theme::FG_VALUE);
            py += rowH;

            swprintf_s(valW, L"%.2fx", snap.profileAggrCurve);
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Aggr. Curve", valW, theme::FG_VALUE);
            py += rowH;

            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Bhop Engine",
                                  snap.bhopEnabled ? L"ENABLED" : L"DISABLED",
                                  snap.bhopEnabled ? theme::CLR_ON : theme::CLR_OFF);
            py += rowH;

            const wchar_t* modeNamesW[] = { L"UNKNOWN", L"LEGIT", L"AGGRESSIVE", L"HUMANIZED", L"SCROLL_EMU" };
            int mIdx = (int)snap.bhopMode;
            if (mIdx < 1 || mIdx > 4) mIdx = 4;
            layout::DrawRowCustom(hdc, rx, py, rwLabel, rwVal, rowH, L"Default Mode", modeNamesW[mIdx], theme::CLR_MODE);
            }
        } // end RIGHT

        y += statusPanelH + theme::GROUP_PAD;
    }

    // ════════════════════════════════════════════════════════════════
    //  ROW 1B: Thread Health & Safety (ALL BUILDS)
    // ════════════════════════════════════════════════════════════════
    {
        int threadHealthH = layout::PanelHeight(2 * rowH);
        RECT thPanel = { x, y, x + contentW, y + threadHealthH };
        if (thPanel.bottom >= rc.top && thPanel.top <= rc.bottom) {
            RECT thInner = layout::DrawPanel(hdc, thPanel, L"THREAD HEALTH & SAFETY");
            {
            layout::ClipGuard panelClip(hdc, thPanel);
            int innerY = thInner.top;
            int innerX = thInner.left;
            int innerW = thInner.right - thInner.left;
            
            // Strict 3-Column Grid
            int colWidth = innerW / 3;
            int col1X = innerX;
            int col2X = innerX + colWidth;
            int col3X = innerX + colWidth * 2;
            
            int labelW = (int)(colWidth * 0.55f);
            int valW   = (int)(colWidth * 0.35f);

            // Column 1
            int py1 = innerY;
            wchar_t spikesW[32]; swprintf_s(spikesW, L"%u", snap.schedulerSpikeCount);
            layout::DrawRowCustom(hdc, col1X, py1, labelW, valW, rowH, L"Scheduler Spikes", spikesW, (snap.schedulerSpikeCount > 0) ? theme::CLR_WARN : theme::FG_VALUE); py1 += rowH;

            wchar_t migrationsW[32]; swprintf_s(migrationsW, L"%u", snap.coreMigrationCount);
            layout::DrawRowCustom(hdc, col1X, py1, labelW, valW, rowH, L"Core Migrations", migrationsW, (snap.coreMigrationCount > 0) ? theme::CLR_WARN : theme::FG_VALUE);

            // Column 2
            int py2 = innerY;
            wchar_t oversleepW[32]; swprintf_s(oversleepW, L"%lld \x03BCs", (long long)snap.timerOversleepPeakUs);
            layout::DrawRowCustom(hdc, col2X, py2, labelW, valW, rowH, L"Peak Oversleep", oversleepW, (snap.timerOversleepPeakUs > 50) ? theme::CLR_WARN : theme::FG_VALUE); py2 += rowH;

            wchar_t varianceW[32]; swprintf_s(varianceW, L"%lld \x03BCs", (long long)snap.wakeVarianceUs);
            layout::DrawRowCustom(hdc, col2X, py2, labelW, valW, rowH, L"Wake Variance", varianceW, (snap.wakeVarianceUs > 20) ? theme::CLR_WARN : theme::FG_VALUE);

            // Column 3
            int py3 = innerY;
            wchar_t triggersW[32]; swprintf_s(triggersW, L"0x%X", snap.failSafeTriggers);
            layout::DrawRowCustom(hdc, col3X, py3, labelW, valW, rowH, L"Fail-Safe Flags", triggersW, (snap.failSafeTriggers != 0) ? theme::CLR_WARN : theme::FG_VALUE); py3 += rowH;
            
            wchar_t recoveryW[32]; swprintf_s(recoveryW, L"%u", snap.recoveryCount);
            layout::DrawRowCustom(hdc, col3X, py3, labelW, valW, rowH, L"Recovery Count", recoveryW, (snap.recoveryCount > 0) ? theme::CLR_WARN : theme::FG_VALUE);
            }
        }
        y += threadHealthH + theme::GROUP_PAD;
    }

    // ════════════════════════════════════════════════════════════════
    //  SCROLLBAR (renders in viewport coordinates, NOT virtual)
    // ════════════════════════════════════════════════════════════════

    PaintScrollbar(hdc, rc);
}

void Destroy() {
    if (s_trackBrush) { DeleteObject(s_trackBrush); s_trackBrush = nullptr; }
    if (s_thumbBrush) { DeleteObject(s_thumbBrush); s_thumbBrush = nullptr; }
    if (s_borderPen) { DeleteObject(s_borderPen); s_borderPen = nullptr; }
}

} // namespace ui_dash
