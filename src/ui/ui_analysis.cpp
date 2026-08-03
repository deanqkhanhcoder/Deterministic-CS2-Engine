// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Analysis Tab — Region-Based Professional Diagnostics UI            ║
// ║  Architecture: Metrics Panel → Scrollable Forensic → Control Band   ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui_analysis.h"
#include "ui_theme.h"
#include "ui_layout.h"
#include "analysis_toolkit.h"
#include "etw_controller.h"
#include "debug_logger.h"
#include "runtime_state.h"
#include "telemetry.h"
#include "types.h"
#include <windows.h>
#include <commdlg.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <thread>

namespace ui {
    HFONT GetTitleFont();
    HFONT GetBodyFont();
    HFONT GetSmallFont();
    HBRUSH GetPanelBrush();
    HWND GetMainHwnd();
    HINSTANCE GetHInst();
    int GetLineHeight(HDC hdc, HFONT font);
}

namespace ui_analysis {

// ════════════════════════════════════════════════════════════════
//  REGION LAYOUT SYSTEM
// ════════════════════════════════════════════════════════════════

struct LayoutRegions {
    RECT metricsRect;    // Fixed top: compact live metrics summary
    RECT forensicRect;   // Flexible middle: scrollable forensic content
    RECT controlRect;    // Fixed bottom: button control band
};

static LayoutRegions ComputeRegions(RECT tabContent, int metricsPanelHeight) {
    LayoutRegions lr;
    int x = tabContent.left + theme::MARGIN;
    int w = (tabContent.right - tabContent.left) - theme::MARGIN * 2;
    int contentTop = tabContent.top + theme::MARGIN;
    int contentBottom = tabContent.bottom - theme::MARGIN;

    // Control band: fixed at bottom
    lr.controlRect = { x, contentBottom - theme::CONTROL_BAND_H,
                       x + w, contentBottom };

    // Metrics panel: fixed at top
    lr.metricsRect = { x, contentTop,
                       x + w, contentTop + metricsPanelHeight };

    // Forensic content: fills remaining space between metrics and controls
    int forensicTop = lr.metricsRect.bottom + theme::REGION_GAP;
    int forensicBottom = lr.controlRect.top - theme::REGION_GAP;
    lr.forensicRect = { x, forensicTop, x + w, forensicBottom };

    return lr;
}

// ════════════════════════════════════════════════════════════════
//  STATE
// ════════════════════════════════════════════════════════════════

// Button IDs
enum {
    IDB_SAVE_CSV = 3001,
    IDB_SAVE_JSON,
    IDB_SAVE_BASE,
    IDB_COMPARE,
    IDB_ETW_START,
    IDB_ETW_STOP,
    IDB_ETW_ANALYZE,
    IDB_ETW_ANALYZE_COMPLETE
};

static HWND s_btnCSV = nullptr;
static HWND s_btnJSON = nullptr;
static HWND s_btnBase = nullptr;
static HWND s_btnCompare = nullptr;
static HWND s_btnEtwStart = nullptr;
static HWND s_btnEtwStop = nullptr;
static HWND s_btnEtwAnalyze = nullptr;

// Scroll state
static int s_scrollY = 0;
static int s_maxScrollY = 0;
static int s_forensicContentHeight = 0;

// ETW state
static etw::TraceAnalysis s_etwAnalysis;
static bool s_hasEtwAnalysis = false;
static std::atomic<bool> s_isAnalyzing = false;
static std::thread s_analysisThread;

// Comparison state
static COLORREF s_compareColor = theme::FG_VALUE;
static wchar_t s_compareStatus[256] = L"No comparison run yet.";
static std::atomic<etw::TraceAnalysis*> s_pendingAnalysis{nullptr};

// Cached GDI Objects
static HBRUSH s_bgPanelBr = nullptr;
static HPEN   s_borderPen = nullptr;
static HPEN   s_sepPen    = nullptr;
static HBRUSH s_hdrBr     = nullptr;
static HBRUSH s_altBr     = nullptr;
static HBRUSH s_trackBr   = nullptr;
static HBRUSH s_thumbBr   = nullptr;
static HBRUSH s_histBrush = nullptr;
static HBRUSH s_jitterBrush = nullptr;
static HBRUSH s_oversleepBrush = nullptr;
static HBRUSH s_spikeBrush = nullptr;
static HBRUSH s_jitterInactiveBrush = nullptr;
static HBRUSH s_oversleepInactiveBrush = nullptr;
static HBRUSH s_spikeInactiveBrush = nullptr;

// ════════════════════════════════════════════════════════════════
//  DRAWING PRIMITIVES
// ════════════════════════════════════════════════════════════════

// Draw a bordered panel with header title and subtle background
static void PaintPanelFrame(HDC hdc, RECT r, const wchar_t* title) {
    // Panel background
    FillRect(hdc, &r, s_bgPanelBr);

    // Border
    HPEN oldP = (HPEN)SelectObject(hdc, s_borderPen);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldP);

    // Header title
    if (title && title[0]) {
        HFONT old = (HFONT)SelectObject(hdc, ui::GetSmallFont());
        SetTextColor(hdc, theme::CLR_ACCENT);
        RECT titleR = { r.left + theme::PANEL_PAD, r.top + 3,
                        r.right - theme::PANEL_PAD, r.top + theme::PANEL_HEADER_H };
        DrawTextW(hdc, title, -1, &titleR, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, old);

        // Separator line under header
        HPEN oldSep = (HPEN)SelectObject(hdc, s_sepPen);
        MoveToEx(hdc, r.left + 1, r.top + theme::PANEL_HEADER_H, nullptr);
        LineTo(hdc, r.right - 1, r.top + theme::PANEL_HEADER_H);
        SelectObject(hdc, oldSep);
    }
}

// Draw a label:value row
static void DrawRow(HDC hdc, int x, int y, int w, int rowH,
                    const wchar_t* label, const wchar_t* value, COLORREF valClr) {
    HFONT old = (HFONT)SelectObject(hdc, ui::GetBodyFont());
    SetTextColor(hdc, theme::FG_LABEL);
    int labelW = 220;
    RECT rl = {x, y, x + labelW, y + rowH};
    DrawTextW(hdc, label, -1, &rl, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(hdc, valClr);
    RECT rv = {x + labelW, y, x + w, y + rowH};
    DrawTextW(hdc, value, -1, &rv, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old);
}

// Draw a section header inside the forensic region
static void DrawSectionHeader(HDC hdc, int x, int y, int w, const wchar_t* title) {
    HFONT old = (HFONT)SelectObject(hdc, ui::GetSmallFont());
    SetTextColor(hdc, theme::CLR_ACCENT);
    RECT r = {x, y, x + w, y + 16};
    DrawTextW(hdc, title, -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);

    HPEN oldP = (HPEN)SelectObject(hdc, s_sepPen);
    MoveToEx(hdc, x, y + 16, nullptr);
    LineTo(hdc, x + w, y + 16);
    SelectObject(hdc, oldP);
}

// ════════════════════════════════════════════════════════════════
//  REGION 1: METRICS PANEL (fixed, compact)
// ════════════════════════════════════════════════════════════════

static void PaintMetricsPanel(HDC hdc, RECT panelRect, int rowH) {
    PaintPanelFrame(hdc, panelRect, L"\x25C8  REALTIME PERFORMANCE METRICS");

    // Clip to panel interior
    RECT inner = { panelRect.left + theme::PANEL_PAD,
                   panelRect.top + theme::PANEL_HEADER_H + 2,
                   panelRect.right - theme::PANEL_PAD,
                   panelRect.bottom - 4 };
    HRGN clip = CreateRectRgnIndirect(&inner);
    SelectClipRgn(hdc, clip);

    int x = inner.left;
    int y = inner.top + 2;
    int w = inner.right - inner.left;
    wchar_t buf[128];

    // Fetch metrics
    analysis::LatencyStats hook, jitter, oversleep;
    analysis::SpikeCluster spikes;
    analysis::CoreStats cores[64];
    uint32_t activeCores = 0, migrations = 0, smtCollisions = 0;
    analysis::AnalyzeSession(hook, jitter, oversleep, spikes, cores, activeCores, migrations, smtCollisions);

    // Row 1: Hook Latency
    swprintf_s(buf, 128, L"P50: %d.%d \x03BCs   P99: %d.%d \x03BCs   Max: %d.%d \x03BCs",
               hook.p50/10, hook.p50%10, hook.p99/10, hook.p99%10, hook.maxVal/10, hook.maxVal%10);
    DrawRow(hdc, x, y, w, rowH, L"Hook Input Latency:", buf, hook.maxVal > 2000 ? theme::CLR_OFF : theme::FG_VALUE);
    y += rowH;

    // Row 2: Timer Jitter
    swprintf_s(buf, 128, L"P50: %d.%d \x03BCs   P99: %d.%d \x03BCs",
               jitter.p50/10, jitter.p50%10, jitter.p99/10, jitter.p99%10);
    DrawRow(hdc, x, y, w, rowH, L"Timer Jitter:", buf, theme::FG_VALUE);
    y += rowH;

    // Row 3: Oversleep
    swprintf_s(buf, 128, L"P50: %d.%d \x03BCs   P99: %d.%d \x03BCs   Peak: %d.%d \x03BCs",
               oversleep.p50/10, oversleep.p50%10, oversleep.p99/10, oversleep.p99%10,
               oversleep.maxVal/10, oversleep.maxVal%10);
    DrawRow(hdc, x, y, w, rowH, L"Timer Oversleep:", buf, oversleep.maxVal > 5000 ? theme::CLR_OFF : theme::FG_VALUE);
    y += rowH;

    // Row 4: Scheduler Status (single-line summary)
    swprintf_s(buf, 128, L"%u Cores  |  %u Migrations  |  %ls",
               activeCores, migrations,
               spikes.sustainedDegradation ? L"DEGRADED" : L"STABLE");
    COLORREF schedClr = spikes.sustainedDegradation ? theme::CLR_OFF :
                        (migrations > 50 ? theme::CLR_WARN : theme::CLR_ON);
    DrawRow(hdc, x, y, w, rowH, L"Scheduler:", buf, schedClr);

    // Restore clipping
    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);
}

// ════════════════════════════════════════════════════════════════
//  REGION 2: FORENSIC CONTENT (scrollable, clipped)
// ════════════════════════════════════════════════════════════════

// Returns total content height (for scroll calculation)
static int PaintForensicContent(HDC hdc, RECT panelRect, int scrollY, int rowH, const RuntimeSnapshot& snap) {
    (void)snap;
    PaintPanelFrame(hdc, panelRect, L"\x25C8  FORENSIC DIAGNOSTICS");

    // Inner content area (below header, inside panel)
    RECT inner = { panelRect.left + theme::PANEL_PAD,
                   panelRect.top + theme::PANEL_HEADER_H + 2,
                   panelRect.right - theme::PANEL_PAD - theme::SCROLLBAR_W - 2,
                   panelRect.bottom - 2 };

    // Clip to inner
    HRGN clip = CreateRectRgnIndirect(&inner);
    SelectClipRgn(hdc, clip);

    int x = inner.left;
    int y = inner.top - scrollY;  // Apply scroll offset
    int w = inner.right - inner.left;
    int contentStartY = y;
    wchar_t buf[256];

    // Re-fetch detailed metrics for forensic sections
    analysis::LatencyStats hook, jitter, oversleep;
    analysis::SpikeCluster spikes;
    analysis::CoreStats cores[64];
    uint32_t activeCores = 0, migrations = 0, smtCollisions = 0;
    analysis::AnalyzeSession(hook, jitter, oversleep, spikes, cores, activeCores, migrations, smtCollisions);

    // ── Section: Scheduler & Topology ──
    DrawSectionHeader(hdc, x, y, w, L"\x2501 SCHEDULER & TOPOLOGY");
    y += 20;

    swprintf_s(buf, 128, L"%u Cores Used", activeCores);
    DrawRow(hdc, x, y, w, rowH, L"Active Core Residencies:", buf, theme::FG_VALUE);
    y += rowH;

    swprintf_s(buf, 128, L"%u Migrations", migrations);
    DrawRow(hdc, x, y, w, rowH, L"Core Affinity Migrations:", buf, migrations > 50 ? theme::CLR_WARN : theme::FG_VALUE);
    y += rowH;

    swprintf_s(buf, 128, L"%u Spikes (%u Bursts)", spikes.totalSpikes, spikes.burstCount);
    DrawRow(hdc, x, y, w, rowH, L"Jitter Spikes (>100\x03BCs):", buf, spikes.totalSpikes > 10 ? theme::CLR_WARN : theme::FG_VALUE);
    y += rowH;

    swprintf_s(buf, 128, L"%.2f Hz  [%ls]", spikes.frequencyHz,
               spikes.sustainedDegradation ? L"DEGRADED" : L"STABLE");
    DrawRow(hdc, x, y, w, rowH, L"Scheduler Stability Index:", buf,
            spikes.sustainedDegradation ? theme::CLR_OFF : theme::CLR_ON);
    y += rowH + theme::GROUP_PAD;

    // ── Section: Baseline Comparison ──
    DrawSectionHeader(hdc, x, y, w, L"\x2501 BASELINE COMPARISON");
    y += 20;

    {
        HFONT old = (HFONT)SelectObject(hdc, ui::GetBodyFont());
        SetTextColor(hdc, theme::FG_LABEL);
        RECT rl = {x, y, x + w, y + rowH};
        DrawTextW(hdc, L"Regression State:", -1, &rl, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        y += rowH;

        SetTextColor(hdc, s_compareColor);
        RECT rv = {x, y, x + w, y + rowH * 2};
        DrawTextW(hdc, s_compareStatus, -1, &rv, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old);
    }
    y += rowH * 2 + theme::GROUP_PAD;

    // ── Section: ETW DPC/ISR Forensics ──
    DrawSectionHeader(hdc, x, y, w, L"\x2501 ETW SCHEDULER & DPC FORENSICS");
    y += 20;

    swprintf_s(buf, 128, L"%ls",
               etw::IsGlobalTraceRunning() ? L"RUNNING (Capturing DPC/ISR/CSwitch)" : L"STOPPED");
    DrawRow(hdc, x, y, w, rowH, L"Live ETW Session:", buf,
            etw::IsGlobalTraceRunning() ? theme::CLR_WARN : theme::CLR_OFF);
    y += rowH;

    if (s_hasEtwAnalysis) {
        swprintf_s(buf, 128, L"%u Total", s_etwAnalysis.cswitchCount);
        DrawRow(hdc, x, y, w, rowH, L"Context Switches:", buf, theme::FG_VALUE);
        y += rowH;

        swprintf_s(buf, 128, L"%u DPCs  /  %u ISRs", s_etwAnalysis.dpcCount, s_etwAnalysis.isrCount);
        DrawRow(hdc, x, y, w, rowH, L"Interrupt Volume:", buf, theme::FG_VALUE);
        y += rowH;

        DrawRow(hdc, x, y, w, rowH, L"Top Offender:", s_etwAnalysis.rogueDriverName.c_str(), theme::CLR_WARN);
        y += rowH;

        // Offender table
        if (!s_etwAnalysis.offenders.empty()) {
            y += 4;
            DrawSectionHeader(hdc, x, y, w, L"\x2501 TOP DPC/ISR OFFENDERS");
            y += 20;

            // Table header background
            RECT hdrRect = {x, y, x + w, y + 20};
            FillRect(hdc, &hdrRect, s_hdrBr);
            
            // Table headers
            {
                HFONT old = (HFONT)SelectObject(hdc, ui::GetSmallFont());
                SetTextColor(hdc, theme::CLR_ACCENT);
                RECT rC1 = {x + 10, y + 3, x + 40, y + 20};
                RECT rC2 = {x + 50, y + 3, x + 250, y + 20};
                RECT rC3 = {x + 260, y + 3, x + 340, y + 20};
                RECT rC4 = {x + 350, y + 3, x + 430, y + 20};
                DrawTextW(hdc, L"#", -1, &rC1, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(hdc, L"Driver Module", -1, &rC2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(hdc, L"DPC Count", -1, &rC3, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(hdc, L"ISR Count", -1, &rC4, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hdc, old);
            }
            y += 24;

            // Table rows
            HFONT old = (HFONT)SelectObject(hdc, ui::GetSmallFont());
            int maxRows = (int)s_etwAnalysis.offenders.size();
            if (maxRows > 10) maxRows = 10;  // Cap at top 10
            
            for (int i = 0; i < maxRows; i++) {
                auto& o = s_etwAnalysis.offenders[i];
                
                if (i % 2 == 1) {
                    RECT rowRc = {x, y, x + w, y + 18};
                    FillRect(hdc, &rowRc, s_altBr);
                }
                
                COLORREF rowClr = (i == 0) ? theme::CLR_WARN :
                                  (i < 3)  ? theme::FG_VALUE : theme::FG_DIM;
                SetTextColor(hdc, rowClr);
                
                wchar_t b1[16], b3[32], b4[32];
                swprintf_s(b1, 16, L"%d", i + 1);
                swprintf_s(b3, 32, L"%u", o.dpcCount);
                swprintf_s(b4, 32, L"%u", o.isrCount);

                RECT rC1 = {x + 10, y + 2, x + 40, y + 18};
                RECT rC2 = {x + 50, y + 2, x + 250, y + 18};
                RECT rC3 = {x + 260, y + 2, x + 340, y + 18};
                RECT rC4 = {x + 350, y + 2, x + 430, y + 18};
                
                DrawTextW(hdc, b1, -1, &rC1, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(hdc, o.driverName, -1, &rC2, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(hdc, b3, -1, &rC3, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                DrawTextW(hdc, b4, -1, &rC4, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

                y += 18;
            }
            SelectObject(hdc, old);
        }
    } else {
        DrawRow(hdc, x, y, w, rowH, L"Analysis Data:", L"No ETW Trace Analyzed", theme::CLR_OFF);
        y += rowH;
    }

    y += theme::GROUP_PAD;

#if MARCO_ENABLE_FORENSIC_UI
    int graphH = 75;
    int histRowH = 14;

    // ════════════════════════════════════════════════════════════════
    //  RUNTIME TELEMETRY METRICS
    // ════════════════════════════════════════════════════════════════
    {
        int telemetryH = layout::PanelHeight(60);
        RECT telRect = { x, y, x + w, y + telemetryH };
        RECT telInner = layout::DrawPanel(hdc, telRect, L"RUNTIME TELEMETRY METRICS");

        {
            layout::ClipGuard panelClipGuard(hdc, telRect);
            int innerY = telInner.top;
            int innerX = telInner.left;
            int innerH = telInner.bottom - telInner.top;
            int panelWidth = (telInner.right - telInner.left - 22) / 3;
            int panelGap = 11;

            RECT rp1 = { innerX, innerY, innerX + panelWidth, innerY + innerH };
            RECT rp2 = { innerX + panelWidth + panelGap, innerY, innerX + panelWidth * 2 + panelGap, innerY + innerH };
            RECT rp3 = { innerX + (panelWidth + panelGap) * 2, innerY, innerX + panelWidth * 3 + panelGap * 2, innerY + innerH };

            HFONT oldFont = (HFONT)SelectObject(hdc, ui::GetSmallFont());
            wchar_t valW1[64], valW2[64];

            // Panel 1: Hook Latency
            SetTextColor(hdc, theme::CLR_ACCENT);
            RECT rText = { rp1.left, rp1.top, rp1.right, rp1.top + 20 };
            DrawTextW(hdc, L"INPUT HOOK LATENCY", -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(hdc, theme::FG_VALUE);
            wsprintfW(valW1, L"p50: %d.%01d µs", (int)(snap.hookLatencyP50Us / 10), (int)(snap.hookLatencyP50Us % 10));
            wsprintfW(valW2, L"p99: %d.%01d µs", (int)(snap.hookLatencyP99Us / 10), (int)(snap.hookLatencyP99Us % 10));
            rText.top += 18; rText.bottom += 18;
            DrawTextW(hdc, valW1, -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            rText.top += 16; rText.bottom += 16;
            DrawTextW(hdc, valW2, -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Panel 2: Timer Jitter
            SelectObject(hdc, ui::GetBodyFont());
            SetTextColor(hdc, theme::CLR_ACCENT);
            rText = { rp2.left, rp2.top, rp2.right, rp2.top + 20 };
            DrawTextW(hdc, L"TIMER JITTER", -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(hdc, theme::FG_VALUE);
            SelectObject(hdc, ui::GetSmallFont());
            wsprintfW(valW1, L"p50: %d.%01d µs", (int)(snap.timerJitterUs / 10), (int)(snap.timerJitterUs % 10));
            wsprintfW(valW2, L"Spin: %d.%01d µs", (int)(snap.spinDurationUs / 10), (int)(snap.spinDurationUs % 10));
            rText.top += 18; rText.bottom += 18;
            DrawTextW(hdc, valW1, -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            rText.top += 16; rText.bottom += 16;
            DrawTextW(hdc, valW2, -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Panel 3: Wake Oversleep
            SelectObject(hdc, ui::GetBodyFont());
            SetTextColor(hdc, theme::CLR_ACCENT);
            rText = { rp3.left, rp3.top, rp3.right, rp3.top + 20 };
            DrawTextW(hdc, L"WAKE OVERSLEEP", -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            SetTextColor(hdc, theme::FG_VALUE);
            SelectObject(hdc, ui::GetSmallFont());
            wsprintfW(valW1, L"p50: %d.%01d µs", (int)(snap.wakeOversleepUs / 10), (int)(snap.wakeOversleepUs % 10));
            wsprintfW(valW2, L"Peak: %d.%01d µs", (int)(snap.timerOversleepPeakUs / 10), (int)(snap.timerOversleepPeakUs % 10));
            rText.top += 18; rText.bottom += 18;
            DrawTextW(hdc, valW1, -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            rText.top += 16; rText.bottom += 16;
            DrawTextW(hdc, valW2, -1, &rText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(hdc, oldFont);
        }
        y += telemetryH + theme::GROUP_PAD;
    }

    // ════════════════════════════════════════════════════════════════
    //  REALTIME LATENCY TIMELINE
    // ════════════════════════════════════════════════════════════════
    {
        int timelineH = layout::PanelHeight(graphH);
        RECT timelineRect = { x, y, x + w, y + timelineH };
        RECT timelineInner = layout::DrawPanel(hdc, timelineRect, L"REALTIME LATENCY TIMELINE");

        {
            layout::ClipGuard panelClipGuard(hdc, timelineRect);
            int gx = timelineInner.left;
            int gy = timelineInner.top;
            int graphW = timelineInner.right - timelineInner.left;

            RECT rg = { gx, gy, gx + graphW, gy + graphH };
            FillRect(hdc, &rg, ui::GetPanelBrush());

            HPEN oldBorderPen = (HPEN)SelectObject(hdc, s_borderPen);
            MoveToEx(hdc, rg.left, rg.top, nullptr);
            LineTo(hdc, rg.right, rg.top);
            LineTo(hdc, rg.right, rg.bottom);
            LineTo(hdc, rg.left, rg.bottom);
            LineTo(hdc, rg.left, rg.top);

            HBRUSH jitterBrush;
            HBRUSH oversleepBrush;
            HBRUSH spikeBrush;

            if (snap.runtimeState != RuntimeState::ActiveTiming) {
                jitterBrush = s_jitterInactiveBrush;
                oversleepBrush = s_oversleepInactiveBrush;
                spikeBrush = s_spikeInactiveBrush;
            } else {
                jitterBrush = s_jitterBrush;
                oversleepBrush = s_oversleepBrush;
                spikeBrush = s_spikeBrush;
            }

            double barW = (double)graphW / RuntimeSnapshot::TIMELINE_SIZE;
            for (int i = 0; i < RuntimeSnapshot::TIMELINE_SIZE; i++) {
                int idx = (snap.timelineIndex + i) % RuntimeSnapshot::TIMELINE_SIZE;
                int64_t jitterVal = snap.timelineJitter[idx];
                int64_t oversleepVal = snap.timelineOversleep[idx];
                bool hasSpike = snap.timelineSpike[idx];

                int xBar = gx + (int)(i * barW);
                int nextX = gx + (int)((i + 1) * barW);
                int wBar = nextX - xBar;
                if (wBar <= 0) wBar = 1;

                int jitterH2 = (int)((jitterVal * graphH) / 200);
                if (jitterVal > 0 && jitterH2 == 0) jitterH2 = 1;
                if (jitterH2 > graphH) jitterH2 = graphH;

                int oversleepH = (int)((oversleepVal * graphH) / 200);
                if (oversleepVal > 0 && oversleepH == 0) oversleepH = 1;
                if (oversleepH > graphH) oversleepH = graphH;

                if (hasSpike) {
                    RECT rBar = { xBar, rg.top + 1, xBar + wBar, rg.bottom - 1 };
                    FillRect(hdc, &rBar, spikeBrush);
                } else {
                    if (jitterH2 > 0) {
                        RECT rBar = { xBar, rg.bottom - 1 - jitterH2, xBar + wBar, rg.bottom - 1 };
                        FillRect(hdc, &rBar, jitterBrush);
                    }
                    if (oversleepH > 0) {
                        RECT rBar = { xBar, rg.bottom - 1 - oversleepH, xBar + wBar, rg.bottom - 1 };
                        FillRect(hdc, &rBar, oversleepBrush);
                    }
                }
            }

            // Inactive overlay
            if (snap.runtimeState != RuntimeState::ActiveTiming) {
                int overlayW = 280;
                int overlayH = 46;
                RECT rBox = {
                    rg.left + (graphW - overlayW) / 2,
                    rg.top + (graphH - overlayH) / 2,
                    rg.left + (graphW + overlayW) / 2,
                    rg.top + (graphH + overlayH) / 2
                };

                FillRect(hdc, &rBox, ui::GetPanelBrush());

                HPEN oldObp = (HPEN)SelectObject(hdc, s_borderPen);
                MoveToEx(hdc, rBox.left, rBox.top, nullptr);
                LineTo(hdc, rBox.right, rBox.top);
                LineTo(hdc, rBox.right, rBox.bottom);
                LineTo(hdc, rBox.left, rBox.bottom);
                LineTo(hdc, rBox.left, rBox.top);
                SelectObject(hdc, oldObp);

                wchar_t line1[128] = {0};
                wchar_t line2[128] = {0};

                if (snap.runtimeState == RuntimeState::Detached) {
                    std::swprintf(line1, sizeof(line1)/sizeof(wchar_t), L"NO ACTIVE GAME");
                    std::swprintf(line2, sizeof(line2)/sizeof(wchar_t), L"No active game process");
                } else if (snap.runtimeState == RuntimeState::Attached) {
                    std::swprintf(line1, sizeof(line1)/sizeof(wchar_t), L"ENGINE READY");
                    if (snap.telemetryAgeMs < 0) {
                        std::swprintf(line2, sizeof(line2)/sizeof(wchar_t), L"Awaiting timing workload...");
                    } else {
                        double ageSec = snap.telemetryAgeMs / 1000.0;
                        std::swprintf(line2, sizeof(line2)/sizeof(wchar_t), L"Last active: %.1fs ago", ageSec);
                    }
                } else if (snap.runtimeState == RuntimeState::FailSafe) {
                    std::swprintf(line1, sizeof(line1)/sizeof(wchar_t), L"FAIL-SAFE ACTIVE");
                    std::swprintf(line2, sizeof(line2)/sizeof(wchar_t), L"Input injection suspended");
                }

                HFONT oldF = (HFONT)SelectObject(hdc, ui::GetBodyFont());
                SetTextColor(hdc, (snap.runtimeState == RuntimeState::FailSafe) ? theme::CLR_OFF : theme::FG_TITLE);
                SetBkMode(hdc, TRANSPARENT);
                RECT rLine1 = rBox;
                rLine1.bottom = rBox.top + 22;
                DrawTextW(hdc, line1, -1, &rLine1, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hdc, ui::GetSmallFont());
                SetTextColor(hdc, theme::FG_DIM);
                RECT rLine2 = rBox;
                rLine2.top = rBox.top + 22;
                DrawTextW(hdc, line2, -1, &rLine2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                SelectObject(hdc, oldF);
            }

            SelectObject(hdc, oldBorderPen);
        }
        y += timelineH + 4; // gap before legend

        RECT legendR = { x, y, x + w, y + 18 };
        layout::DrawTextSafe(hdc, legendR, L"Cyan: Jitter | Orange: Oversleep | Red Line: Scheduler Spike (>50us late) | Height: 20us",
                             theme::FG_DIM, ui::GetSmallFont(), DT_CENTER | DT_SINGLELINE);
        y += 18 + theme::GROUP_PAD;
    }

    // ════════════════════════════════════════════════════════════════
    //  METRICS PROBABILITY DENSITY HISTOGRAMS
    // ════════════════════════════════════════════════════════════════
    {
        int histogramH = layout::PanelHeight(6 * (histRowH + 2));
        RECT histRect = { x, y, x + w, y + histogramH };
        RECT histInner = layout::DrawPanel(hdc, histRect, L"METRICS PROBABILITY DENSITY HISTOGRAMS");

        {
            layout::ClipGuard panelClipGuard(hdc, histRect);

            int xLeft = histInner.left;
            int hy = histInner.top;
            int fullW = histInner.right - histInner.left;
            int histColW = fullW / 3;

            const wchar_t* binsHook[] = { L"<0.5us", L"<1us", L"<2us", L"<5us", L"<10us", L"10us+" };
            const wchar_t* binsJitter[] = { L"<2us", L"<5us", L"<10us", L"<25us", L"<50us", L"50us+" };
            const wchar_t* binsOversleep[] = { L"<5us", L"<10us", L"<25us", L"<50us", L"<100us", L"100us+" };

            uint32_t totalHook = 0, totalJitter = 0, totalOversleep = 0;
            for (int r = 0; r < 6; r++) {
                totalHook += snap.histHook[r];
                totalJitter += snap.histJitter[r];
                totalOversleep += snap.histOversleep[r];
            }

            HFONT oldFont = (HFONT)SelectObject(hdc, ui::GetBodyFont());

            for (int row = 0; row < 6; row++) {
                int yRow = hy + row * (histRowH + 2);

                // Hook Latency Row
                SetTextColor(hdc, theme::FG_LABEL);
                RECT rBin = { xLeft, yRow, xLeft + 50, yRow + histRowH };
                DrawTextW(hdc, binsHook[row], -1, &rBin, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                RECT rBar = { xLeft + 54, yRow + 2, xLeft + histColW - 8, yRow + histRowH - 2 };
                FrameRect(hdc, &rBar, (HBRUSH)GetStockObject(GRAY_BRUSH));
                int pctHook = 0;
                if (totalHook > 0) pctHook = (snap.histHook[row] * (rBar.right - rBar.left - 4)) / totalHook;
                if (pctHook > 0) {
                    RECT rFill = { rBar.left + 2, rBar.top + 2, rBar.left + 2 + pctHook, rBar.bottom - 2 };
                    FillRect(hdc, &rFill, s_histBrush);
                }

                // Jitter Row
                int xJitterCol = xLeft + histColW;
                rBin = { xJitterCol, yRow, xJitterCol + 45, yRow + histRowH };
                DrawTextW(hdc, binsJitter[row], -1, &rBin, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                rBar = { xJitterCol + 48, yRow + 2, xJitterCol + histColW - 8, yRow + histRowH - 2 };
                FrameRect(hdc, &rBar, (HBRUSH)GetStockObject(GRAY_BRUSH));
                int pctJitter = 0;
                if (totalJitter > 0) pctJitter = (snap.histJitter[row] * (rBar.right - rBar.left - 4)) / totalJitter;
                if (pctJitter > 0) {
                    RECT rFill = { rBar.left + 2, rBar.top + 2, rBar.left + 2 + pctJitter, rBar.bottom - 2 };
                    FillRect(hdc, &rFill, s_histBrush);
                }

                // Oversleep Row
                int xOversleepCol = xLeft + histColW * 2;
                rBin = { xOversleepCol, yRow, xOversleepCol + 50, yRow + histRowH };
                DrawTextW(hdc, binsOversleep[row], -1, &rBin, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                rBar = { xOversleepCol + 56, yRow + 2, xOversleepCol + histColW - 8, yRow + histRowH - 2 };
                FrameRect(hdc, &rBar, (HBRUSH)GetStockObject(GRAY_BRUSH));
                int pctOversleep = 0;
                if (totalOversleep > 0) pctOversleep = (snap.histOversleep[row] * (rBar.right - rBar.left - 4)) / totalOversleep;
                if (pctOversleep > 0) {
                    RECT rFill = { rBar.left + 2, rBar.top + 2, rBar.left + 2 + pctOversleep, rBar.bottom - 2 };
                    FillRect(hdc, &rFill, s_histBrush);
                }
            }

            SelectObject(hdc, oldFont);
        }
        y += histogramH + theme::GROUP_PAD;
    }
#endif

    // Calculate total content height (exclude scrollY so it doesn't grow infinitely)
    int totalHeight = y - contentStartY;

    // Restore clipping
    SelectClipRgn(hdc, nullptr);
    DeleteObject(clip);

    return totalHeight;
}

// ════════════════════════════════════════════════════════════════
//  SCROLLBAR
// ════════════════════════════════════════════════════════════════

static void PaintScrollbar(HDC hdc, RECT forensicRect, int contentHeight, int scrollY) {
    int viewH = (forensicRect.bottom - forensicRect.top) - theme::PANEL_HEADER_H - 4;
    if (contentHeight <= viewH) return;  // No scrollbar needed

    // Track area (right edge of forensic panel)
    int trackX = forensicRect.right - theme::SCROLLBAR_W - 1;
    int trackTop = forensicRect.top + theme::PANEL_HEADER_H + 2;
    int trackBottom = forensicRect.bottom - 2;
    int trackH = trackBottom - trackTop;

    // Track background
    RECT trackRect = { trackX, trackTop, trackX + theme::SCROLLBAR_W, trackBottom };
    FillRect(hdc, &trackRect, s_trackBr);

    // Thumb
    double ratio = (double)viewH / (double)contentHeight;
    int thumbH = (std::max)(16, (int)(trackH * ratio));
    int maxScroll = contentHeight - viewH;
    double scrollRatio = (maxScroll > 0) ? (double)scrollY / (double)maxScroll : 0.0;
    int thumbY = trackTop + (int)((trackH - thumbH) * scrollRatio);

    RECT thumbRect = { trackX + 1, thumbY, trackX + theme::SCROLLBAR_W - 1, thumbY + thumbH };
    FillRect(hdc, &thumbRect, s_thumbBr);
}

// ════════════════════════════════════════════════════════════════
//  REGION 3: CONTROL BAND (fixed, buttons)
// ════════════════════════════════════════════════════════════════

static void PaintControlBand(HDC hdc, RECT ctrlRect) {
    PaintPanelFrame(hdc, ctrlRect, nullptr);  // No header text — just the bordered panel
}

// ════════════════════════════════════════════════════════════════
//  FLOW LAYOUT (for button positioning)
// ════════════════════════════════════════════════════════════════

struct FlowLayout {
    int startX;
    int x;
    int y;
    int maxWidth;
    int rowHeight;
    int gap;
};

static RECT NextButtonRect(FlowLayout& fl, int w, int h) {
    if (fl.x + w > fl.maxWidth) {
        fl.x = fl.startX;
        fl.y += fl.rowHeight + fl.gap;
        fl.rowHeight = 0;
    }
    RECT rc = { fl.x, fl.y, fl.x + w, fl.y + h };
    fl.x += w + fl.gap;
    if (h > fl.rowHeight) fl.rowHeight = h;
    return rc;
}

// ════════════════════════════════════════════════════════════════
//  INIT / SHOW / PAINT
// ════════════════════════════════════════════════════════════════

void Init() {
    HWND hw = ui::GetMainHwnd();
    HINSTANCE hi = ui::GetHInst();

    HDC hdc = GetDC(hw);
    int rowH = ui::GetLineHeight(hdc, ui::GetBodyFont()) + theme::ROW_PAD_V;
    int metricsPanelHeight = theme::PANEL_HEADER_H + 4 * rowH + theme::PANEL_PAD_BOTTOM;
    ReleaseDC(hw, hdc);

    // Compute regions to position buttons inside the control band
    RECT tabContent = { 0, theme::TAB_BAR_H + 1, theme::WIN_W, theme::WIN_H - theme::STATUS_BAR_H };
    LayoutRegions lr = ComputeRegions(tabContent, metricsPanelHeight);

    // Flow layout inside control band, centered
    int ctrlWidth = lr.controlRect.right - lr.controlRect.left;
    int cols = 4;
    int totalBtnRowW = theme::BTN_W * cols + theme::BTN_GAP * (cols - 1);

    FlowLayout fl;
    fl.startX = lr.controlRect.left + (ctrlWidth - totalBtnRowW) / 2;
    fl.x = fl.startX;
    fl.y = lr.controlRect.top + theme::PANEL_PAD;
    fl.maxWidth = lr.controlRect.right - theme::PANEL_PAD;
    fl.rowHeight = 0;
    fl.gap = theme::BTN_GAP;

    RECT rc;
    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnCSV = CreateWindowExW(0, L"BUTTON", L"Save CSV",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_SAVE_CSV, hi, nullptr);

    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnJSON = CreateWindowExW(0, L"BUTTON", L"Save JSON",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_SAVE_JSON, hi, nullptr);

    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnBase = CreateWindowExW(0, L"BUTTON", L"Save Base",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_SAVE_BASE, hi, nullptr);

    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnCompare = CreateWindowExW(0, L"BUTTON", L"Compare Base",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_COMPARE, hi, nullptr);

    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnEtwStart = CreateWindowExW(0, L"BUTTON", L"Start ETW",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_ETW_START, hi, nullptr);

    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnEtwStop = CreateWindowExW(0, L"BUTTON", L"Stop ETW",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_ETW_STOP, hi, nullptr);

    rc = NextButtonRect(fl, theme::BTN_W, theme::BTN_H);
    s_btnEtwAnalyze = CreateWindowExW(0, L"BUTTON", L"Analyze ETW",
        WS_CHILD | BS_OWNERDRAW, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hw, (HMENU)(UINT_PTR)IDB_ETW_ANALYZE, hi, nullptr);

    // Create global GDI objects
    if (!s_bgPanelBr) s_bgPanelBr = CreateSolidBrush(theme::BG_PANEL);
    if (!s_borderPen) s_borderPen = CreatePen(PS_SOLID, 1, theme::CLR_BORDER);
    if (!s_sepPen) s_sepPen = CreatePen(PS_SOLID, 1, RGB(35, 50, 70));
    if (!s_hdrBr) s_hdrBr = CreateSolidBrush(theme::BG_CONTROL);
    if (!s_altBr) s_altBr = CreateSolidBrush(theme::BG_LOG_ITEM);
    if (!s_trackBr) s_trackBr = CreateSolidBrush(RGB(20, 28, 38));
    if (!s_thumbBr) s_thumbBr = CreateSolidBrush(RGB(60, 80, 110));
    if (!s_histBrush) s_histBrush = CreateSolidBrush(theme::CLR_ACCENT);
    if (!s_jitterBrush) s_jitterBrush = CreateSolidBrush(theme::CLR_MODE);
    if (!s_oversleepBrush) s_oversleepBrush = CreateSolidBrush(theme::CLR_WARN);
    if (!s_spikeBrush) s_spikeBrush = CreateSolidBrush(theme::CLR_OFF);
    if (!s_jitterInactiveBrush) s_jitterInactiveBrush = CreateSolidBrush(RGB(28, 56, 85));
    if (!s_oversleepInactiveBrush) s_oversleepInactiveBrush = CreateSolidBrush(RGB(85, 56, 0));
    if (!s_spikeInactiveBrush) s_spikeInactiveBrush = CreateSolidBrush(RGB(85, 17, 17));
}

void Destroy() {
    if (s_analysisThread.joinable()) s_analysisThread.join();
    delete s_pendingAnalysis.exchange(nullptr);
    DeleteObject(s_bgPanelBr); s_bgPanelBr = nullptr;
    if (s_borderPen) { DeleteObject(s_borderPen); s_borderPen = nullptr; }
    if (s_sepPen) { DeleteObject(s_sepPen); s_sepPen = nullptr; }
    if (s_hdrBr) { DeleteObject(s_hdrBr); s_hdrBr = nullptr; }
    if (s_altBr) { DeleteObject(s_altBr); s_altBr = nullptr; }
    if (s_trackBr) { DeleteObject(s_trackBr); s_trackBr = nullptr; }
    if (s_thumbBr) { DeleteObject(s_thumbBr); s_thumbBr = nullptr; }
    if (s_histBrush) { DeleteObject(s_histBrush); s_histBrush = nullptr; }
    if (s_jitterBrush) { DeleteObject(s_jitterBrush); s_jitterBrush = nullptr; }
    if (s_oversleepBrush) { DeleteObject(s_oversleepBrush); s_oversleepBrush = nullptr; }
    if (s_spikeBrush) { DeleteObject(s_spikeBrush); s_spikeBrush = nullptr; }
    if (s_jitterInactiveBrush) { DeleteObject(s_jitterInactiveBrush); s_jitterInactiveBrush = nullptr; }
    if (s_oversleepInactiveBrush) { DeleteObject(s_oversleepInactiveBrush); s_oversleepInactiveBrush = nullptr; }
    if (s_spikeInactiveBrush) { DeleteObject(s_spikeInactiveBrush); s_spikeInactiveBrush = nullptr; }
}

void ShowButtons(bool show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    if (s_btnCSV) ShowWindow(s_btnCSV, cmd);
    if (s_btnJSON) ShowWindow(s_btnJSON, cmd);
    if (s_btnBase) ShowWindow(s_btnBase, cmd);
    if (s_btnCompare) ShowWindow(s_btnCompare, cmd);
    if (s_btnEtwStart) ShowWindow(s_btnEtwStart, cmd);
    if (s_btnEtwStop) ShowWindow(s_btnEtwStop, cmd);
    if (s_btnEtwAnalyze) ShowWindow(s_btnEtwAnalyze, cmd);
}

void ResetScroll() {
    s_scrollY = 0;
    s_maxScrollY = 0;
    s_forensicContentHeight = 0;
}

void Paint(HDC hdc, RECT rc, const RuntimeSnapshot& snap) {
    int rowH = ui::GetLineHeight(hdc, ui::GetBodyFont()) + theme::ROW_PAD_V;
    int metricsPanelHeight = theme::PANEL_HEADER_H + 4 * rowH + theme::PANEL_PAD_BOTTOM;

    LayoutRegions lr = ComputeRegions(rc, metricsPanelHeight);

    // Region 1: Metrics panel (clipped, fixed)
    PaintMetricsPanel(hdc, lr.metricsRect, rowH);

    // Region 2: Forensic content (clipped, scrollable)
    s_forensicContentHeight = PaintForensicContent(hdc, lr.forensicRect, s_scrollY, rowH, snap);

    // Scrollbar overlay
    PaintScrollbar(hdc, lr.forensicRect, s_forensicContentHeight, s_scrollY);

    // Region 3: Control band background (buttons are child windows, painted by Windows)
    PaintControlBand(hdc, lr.controlRect);
}

void OnMouseWheel(int delta) {
    HDC hdc = GetDC(ui::GetMainHwnd());
    int rowH = ui::GetLineHeight(hdc, ui::GetBodyFont()) + theme::ROW_PAD_V;
    int metricsPanelHeight = theme::PANEL_HEADER_H + 4 * rowH + theme::PANEL_PAD_BOTTOM;
    ReleaseDC(ui::GetMainHwnd(), hdc);
    
    int viewH = 0;
    {
        RECT tabContent = { 0, theme::TAB_BAR_H + 1, theme::WIN_W, theme::WIN_H - theme::STATUS_BAR_H };
        LayoutRegions lr = ComputeRegions(tabContent, metricsPanelHeight);
        viewH = (lr.forensicRect.bottom - lr.forensicRect.top) - theme::PANEL_HEADER_H - 4;
    }

    int scrollStep = rowH * 3;  // 3 rows per notch
    if (delta > 0) s_scrollY -= scrollStep;  // Scroll up
    else           s_scrollY += scrollStep;  // Scroll down

    // Clamp
    s_maxScrollY = s_forensicContentHeight - viewH;
    if (s_maxScrollY < 0) s_maxScrollY = 0;
    if (s_scrollY < 0) s_scrollY = 0;
    if (s_scrollY > s_maxScrollY) s_scrollY = s_maxScrollY;
}

// ════════════════════════════════════════════════════════════════
//  FILE DIALOGS
// ════════════════════════════════════════════════════════════════

static bool ShowSaveDialog(HWND owner, const wchar_t* filter, const wchar_t* ext, wchar_t* outPath, DWORD maxPath) {
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = outPath;
    ofn.lpstrFile[0] = L'\0';
    ofn.nMaxFile = maxPath;
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn);
}

static bool ShowOpenDialog(HWND owner, const wchar_t* filter, const wchar_t* ext, wchar_t* outPath, DWORD maxPath) {
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = outPath;
    ofn.lpstrFile[0] = L'\0';
    ofn.nMaxFile = maxPath;
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn);
}

// ════════════════════════════════════════════════════════════════
//  COMMAND HANDLER
// ════════════════════════════════════════════════════════════════

void OnCommand(HWND hwnd, WPARAM wParam) {
    wchar_t path[MAX_PATH] = L"";

    switch (LOWORD(wParam)) {
        case IDB_SAVE_CSV:
            if (ShowSaveDialog(hwnd, L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0", L"csv", path, MAX_PATH)) {
                analysis::ExportCSV(path);
                DLOG_INFO(UI, "Exported CSV trace to %ls", path);
            }
            break;

        case IDB_SAVE_JSON:
            if (ShowSaveDialog(hwnd, L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0", L"json", path, MAX_PATH)) {
                analysis::ExportJSON(path);
                DLOG_INFO(UI, "Exported JSON trace to %ls", path);
            }
            break;

        case IDB_SAVE_BASE:
            if (ShowSaveDialog(hwnd, L"Baseline Profiles (*.base.json)\0*.base.json\0All Files (*.*)\0*.*\0", L"base.json", path, MAX_PATH)) {
                analysis::SaveBaseline(path);
                DLOG_INFO(UI, "Saved baseline profile to %ls", path);
            }
            break;

        case IDB_COMPARE:
            if (ShowOpenDialog(hwnd, L"Baseline Profiles (*.base.json)\0*.base.json\0All Files (*.*)\0*.*\0", L"base.json", path, MAX_PATH)) {
                bool regression = analysis::CompareSession(path, s_compareStatus, 256);
                s_compareColor = regression ? theme::CLR_OFF : theme::CLR_ON;
                DLOG_INFO(UI, "Comparison completed against baseline %ls", path);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            break;

        case IDB_ETW_START:
            if (!etw::StartGlobalTrace()) {
                PostMessage(hwnd, WM_ANALYSIS_ETW_START_FAILED, 0, 0);
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            break;

        case IDB_ETW_STOP:
            (void)etw::StopGlobalTrace();
            InvalidateRect(hwnd, nullptr, TRUE);
            break;

        case IDB_ETW_ANALYZE:
            if (s_isAnalyzing.load()) break;
            if (s_analysisThread.joinable()) s_analysisThread.join();
            s_isAnalyzing.store(true);
            s_analysisThread = std::thread([hwnd]() {
                auto newAnalysis = new etw::TraceAnalysis();
                bool res = etw::AnalyzeTrace(L"performance_trace.etl", *newAnalysis);
                if (!res) {
                    delete newAnalysis;
                    newAnalysis = nullptr;
                }
                s_pendingAnalysis.store(newAnalysis);
                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDB_ETW_ANALYZE_COMPLETE, res ? 1 : 0), 0);
            });
            break;
            
        case IDB_ETW_ANALYZE_COMPLETE:
            s_isAnalyzing.store(false);
            s_hasEtwAnalysis = (HIWORD(wParam) != 0);
            {
                etw::TraceAnalysis* pending = s_pendingAnalysis.exchange(nullptr);
                if (pending) {
                    if (s_hasEtwAnalysis) {
                        s_etwAnalysis = *pending;
                        DLOG_INFO(ETW, "Trace analysis complete. Found %u CSwitches, %u DPCs, %u ISRs",
                            s_etwAnalysis.cswitchCount, s_etwAnalysis.dpcCount, s_etwAnalysis.isrCount);
                        if (!s_etwAnalysis.offenders.empty()) {
                            DLOG_WARN(ETW, "Top DPC Offender: %ls (%u events)",
                                      s_etwAnalysis.offenders[0].driverName, s_etwAnalysis.offenders[0].dpcCount);
                        }
                    }
                    delete pending;
                }
            }
            if (!s_hasEtwAnalysis) {
                DLOG_ERR(ETW, "Failed to analyze trace. Run as Administrator?");
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
    }
}

} // namespace ui_analysis
