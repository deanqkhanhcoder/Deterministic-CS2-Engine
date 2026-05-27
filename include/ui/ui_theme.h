#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — UI Theme Constants                               ║
// ║  Dark-mode color palette, fonts, and layout metrics                  ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>

namespace theme {

// ── Window Metrics ──
constexpr int WIN_W          = 620;
constexpr int WIN_H          = 700;
constexpr int TAB_BAR_H      = 32;
constexpr int STATUS_BAR_H   = 42;
constexpr int MARGIN          = 14;
constexpr int ROW_PAD_V       = 6;     // Padding added to font height to form row bounds
constexpr int GROUP_PAD       = 12;

// ── Analysis Tab Region Layout ──
constexpr int CONTROL_BAND_H    = 68;   // Fixed height for button control band
constexpr int REGION_GAP         = 6;    // Gap between regions
constexpr int PANEL_PAD          = 8;    // Internal horizontal padding inside panels
constexpr int PANEL_PAD_BOTTOM   = 10;   // Extra bottom padding to clear borders
constexpr int PANEL_HEADER_H     = 20;   // Height of panel header/title
constexpr int SCROLLBAR_W        = 6;    // Scrollbar track width

// ── Button Size Constants ──
constexpr int BTN_W = 110; // width of standard buttons
constexpr int BTN_H = 26;  // height of standard buttons
constexpr int BTN_GAP = 8; // spacing between buttons
constexpr int LABEL_W         = 180;
constexpr int VALUE_W         = 120;

// ── Colors — Professional Charcoal Palette ──
constexpr COLORREF BG_WINDOW    = RGB(22, 22, 22);     // Main background
constexpr COLORREF BG_TAB_BAR   = RGB(28, 28, 28);     // Tab bar strip
constexpr COLORREF BG_TAB_ACTIVE= RGB(38, 38, 38);     // Active tab bg
constexpr COLORREF BG_PANEL     = RGB(32, 32, 32);     // Group box bg
constexpr COLORREF BG_STATUS    = RGB(18, 18, 18);     // Status bar bg
constexpr COLORREF BG_CONTROL   = RGB(40, 40, 40);     // Edit/combo bg
constexpr COLORREF BG_BUTTON    = RGB(45, 45, 45);     // Button bg
constexpr COLORREF BG_BUTTON_HOV= RGB(60, 60, 60);     // Button hover
constexpr COLORREF BG_BUTTON_PR = RGB(35, 35, 35);     // Button pressed
constexpr COLORREF BG_LOG_ITEM  = RGB(26, 26, 26);     // Log row alt

constexpr COLORREF FG_TITLE     = RGB(220, 220, 220);  // Window title text
constexpr COLORREF FG_LABEL     = RGB(150, 150, 150);  // Label text
constexpr COLORREF FG_VALUE     = RGB(200, 200, 200);  // Value text
constexpr COLORREF FG_DIM       = RGB(110, 110, 110);  // Dim/footer text
constexpr COLORREF FG_HOTKEY    = RGB(130, 130, 130);  // Hotkey hint text

constexpr COLORREF CLR_ON       = RGB(70, 200, 110);   // Active/ON green
constexpr COLORREF CLR_OFF      = RGB(230, 70, 70);    // Inactive/OFF red
constexpr COLORREF CLR_WARN     = RGB(240, 160, 50);   // Warning/suspended orange
constexpr COLORREF CLR_MODE     = RGB(0, 160, 230);    // Mode cyan
constexpr COLORREF CLR_ACCENT   = RGB(0, 120, 215);    // Professional selection blue
constexpr COLORREF CLR_BORDER   = RGB(55, 55, 55);     // Border lines

// Log severity colors
constexpr COLORREF CLR_LOG_INFO = RGB(160, 160, 160);
constexpr COLORREF CLR_LOG_WARN = RGB(240, 180, 80);
constexpr COLORREF CLR_LOG_ERR  = RGB(240, 80, 80);

//  Tab IDs 
#include "build_config.h"

#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
constexpr int TAB_DASHBOARD = 0;
constexpr int TAB_SETTINGS  = 1;
constexpr int TAB_ANALYSIS  = 2;
constexpr int TAB_COUNT     = 3;

inline const wchar_t* TabNames[] = { L"Dashboard", L"Settings", L"Analysis" };
#elif MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
constexpr int TAB_DASHBOARD = 0;
constexpr int TAB_SETTINGS  = 1;
constexpr int TAB_COUNT     = 2;

inline const wchar_t* TabNames[] = { L"Dashboard", L"Settings" };
#else
constexpr int TAB_SETTINGS  = 0;
constexpr int TAB_COUNT     = 1;

inline const wchar_t* TabNames[] = { L"Settings" };
#endif

} // namespace theme
