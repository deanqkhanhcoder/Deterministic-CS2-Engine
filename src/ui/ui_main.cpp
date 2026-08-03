// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Main UI Window Implementation                    ║
// ║  Win32 owner-draw dark theme, tabbed interface, system tray         ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "ui_main.h"
#include "ui_theme.h"
#include "types.h"
#include "runtime_state.h"
#include "runtime_config.h"
#include "config_io.h"
#include "state_engine.h"
#include "bhop.h"
#include "target_platform.h"
#include "timing.h"
#include "telemetry.h"
#include "topology.h"
#include "debug_logger.h"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "ui_layout.h"
#if MARCO_ENABLE_FORENSIC_UI
#include "ui_analysis.h"
#endif

// MinGW: link comctl32, shell32 via build command

// Defined in ui_dashboard.cpp / ui_settings.cpp / ui_log.cpp
#if MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
namespace ui_dash { void Paint(HDC hdc, RECT rc, const RuntimeSnapshot& snap); void OnMouseWheel(int delta); void Destroy(); }
#endif
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
namespace ui_analysis { void Paint(HDC hdc, RECT rc); void OnCommand(HWND hwnd, WPARAM wp); void OnMouseWheel(int delta); void ShowButtons(bool show); void Destroy(); void Init(); }
#endif
namespace ui_sett { void Paint(HDC hdc, RECT rc); void OnCommand(HWND hwnd, WPARAM wp); void Init(); void ShowButtons(bool show); void OnMouseWheel(int delta); }

namespace ui {

// ════════════════════════════════════════════════════════════════
//  MODULE STATE
// ════════════════════════════════════════════════════════════════
static uint64_t   s_toastTriggerTimeUs = 0;
static int        s_toastPrevProfileIndex = -1;
static HWND       s_hwnd      = nullptr;
static HWND       s_msgHwnd   = nullptr;  // Hidden message window for engine commands
static HINSTANCE  s_hInst     = nullptr;
#if MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
static int        s_activeTab = theme::TAB_DASHBOARD;
#else
static int        s_activeTab = theme::TAB_SETTINGS;
#endif
static bool       s_visible   = true;
static HFONT      s_fontTitle = nullptr;
static HFONT      s_fontBody  = nullptr;
static HFONT      s_fontSmall = nullptr;
static HBRUSH     s_bgBrush   = nullptr;
static HBRUSH     s_panelBrush= nullptr;
static HBRUSH     s_tabBrush  = nullptr;
static HBRUSH     s_tabActBrush=nullptr;
static HBRUSH     s_ctrlBrush = nullptr;
static HBRUSH     s_bgBtn     = nullptr;
static HBRUSH     s_bgBtnPr   = nullptr;
static HBRUSH     s_bgBtnHov  = nullptr;
static HPEN       s_btnBorder = nullptr;
static UINT_PTR   s_refreshTimer = 0;
static bool       s_updatePending = false;
static RuntimeSnapshot s_snap = {};
static NOTIFYICONDATAW s_nid  = {};
static bool       s_trayAdded = false;
static UINT       s_taskbarCreatedMsg = 0;  // For explorer restart

static const wchar_t* CLASS_NAME = L"CS2MacroSuiteMain";

static void UpdateTabVisibility() {
    ui_sett::ShowButtons(s_activeTab == theme::TAB_SETTINGS);
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
    ui_analysis::ShowButtons(s_activeTab == theme::TAB_ANALYSIS);
#endif
}

// Tray menu IDs
enum { IDM_SHOW=1001, IDM_BHOP, IDM_MODE, IDM_SUSPEND, IDM_EXIT };

// ════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════
static void DrawText_(HDC hdc, int x, int y, int w, int h,
                      const wchar_t* text, COLORREF clr, HFONT font, UINT fmt = DT_LEFT) {
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, clr);
    RECT rc = {x, y, x+w, y+h};
    DrawTextW(hdc, text, -1, &rc, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old);
}

static void FillR(HDC hdc, int x, int y, int w, int h, HBRUSH br) {
    RECT rc = {x, y, x+w, y+h};
    FillRect(hdc, &rc, br);
}

static void DrawLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF clr) {
    HPEN pen = CreatePen(PS_SOLID, 1, clr);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

// ════════════════════════════════════════════════════════════════
//  SYSTEM TRAY
// ════════════════════════════════════════════════════════════════
static void AddTrayIcon() {
    memset(&s_nid, 0, sizeof(s_nid));
    s_nid.cbSize = sizeof(s_nid);
    s_nid.hWnd   = s_hwnd;
    s_nid.uID    = 1;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_TRAY_CALLBACK;
    s_nid.hIcon  = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wcscpy(s_nid.szTip, L"CS2 Macro Suite");
    Shell_NotifyIconW(NIM_ADD, &s_nid);
    s_trayAdded = true;
}

static void RemoveTrayIcon() {
    if (s_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        s_trayAdded = false;
    }
}

static void ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW,    L"Show Window");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_BHOP,    s_snap.bhopEnabled ? L"Bhop: ON" : L"Bhop: OFF");
    AppendMenuW(hMenu, MF_STRING, IDM_MODE,    L"Cycle Mode");
    AppendMenuW(hMenu, MF_STRING, IDM_SUSPEND, s_snap.suspended ? L"Resume" : L"Suspend");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,    L"Exit");
    SetForegroundWindow(s_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, s_hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ════════════════════════════════════════════════════════════════
//  TAB BAR PAINT
// ════════════════════════════════════════════════════════════════
static void PaintTabBar(HDC hdc) {
    FillR(hdc, 0, 0, theme::WIN_W, theme::TAB_BAR_H, s_tabBrush);
    int tabW = theme::WIN_W / theme::TAB_COUNT;

    for (int i = 0; i < theme::TAB_COUNT; i++) {
        int x = i * tabW;
        bool active = (i == s_activeTab);
        if (active) {
            FillR(hdc, x, 0, tabW, theme::TAB_BAR_H, s_tabActBrush);
            DrawLine(hdc, x, theme::TAB_BAR_H - 2, x + tabW, theme::TAB_BAR_H - 2, theme::CLR_ACCENT);
        }
        COLORREF clr = active ? theme::FG_TITLE : theme::FG_DIM;
        DrawText_(hdc, x, 0, tabW, theme::TAB_BAR_H, theme::TabNames[i], clr, s_fontBody, DT_CENTER);
    }
    DrawLine(hdc, 0, theme::TAB_BAR_H, theme::WIN_W, theme::TAB_BAR_H, theme::CLR_BORDER);
}

// ════════════════════════════════════════════════════════════════
//  STATUS BAR PAINT
// ════════════════════════════════════════════════════════════════
static void PaintStatusBar(HDC hdc) {
    int y = theme::WIN_H - theme::STATUS_BAR_H;
    FillR(hdc, 0, y, theme::WIN_W, theme::STATUS_BAR_H, s_tabBrush);
    DrawLine(hdc, 0, y, theme::WIN_W, y, theme::CLR_BORDER);

    int row1 = y + 4;
    int row2 = y + 22;
    int col = 12;

    if (!s_snap.targetActive) {
        DrawText_(hdc, col, row1, theme::WIN_W - 24, 16, L"WAITING FOR GAME WINDOW...", theme::CLR_WARN, s_fontSmall);
    } else {
        // Row 1: status indicators
        int currentX = col;
        if (s_snap.waitingForSpaceRepress) {
            DrawText_(hdc, currentX, row1, 190, 16, L"WAITING FOR SPACE RE-PRESS", theme::CLR_WARN, s_fontSmall);
            currentX += 200;
        } else if (!(s_snap.activeCapabilities & target_platform::CAP_CSTRAFE)) {
            wchar_t msgBuf[128];
            swprintf(msgBuf, 128, L"Counter-strafe unavailable for %ls", s_snap.targetName);
            DrawText_(hdc, currentX, row1, 230, 16, msgBuf, theme::CLR_WARN, s_fontSmall);
            currentX += 240;
        } else {
            const wchar_t* strafeIcon = s_snap.suspended ? L"\x25CF PAUSED" : L"\x25CF ON";
            COLORREF strafClr = s_snap.suspended ? theme::CLR_WARN : theme::CLR_ON;
            DrawText_(hdc, currentX, row1, 60, 16, L"STRAFE:", theme::FG_DIM, s_fontSmall);
            DrawText_(hdc, currentX+52, row1, 70, 16, strafeIcon, strafClr, s_fontSmall);
            currentX += 135;
        }

        const wchar_t* bhopIcon;
        COLORREF bhopClr;
        if (!(s_snap.activeCapabilities & target_platform::CAP_BHOP)) {
            bhopIcon = L"\x25A0 UNAVAIL";
            bhopClr = theme::FG_DIM;
        } else {
            if (s_snap.suspended) {
                bhopIcon = s_snap.bhopEnabled ? L"\x25CF PAUSED" : L"\x25A0 OFF";
                bhopClr = s_snap.bhopEnabled ? theme::CLR_WARN : theme::CLR_OFF;
            } else {
                bhopIcon = s_snap.bhopEnabled ? L"\x25CF ON" : L"\x25A0 OFF";
                bhopClr = s_snap.bhopEnabled ? theme::CLR_ON : theme::CLR_OFF;
            }
        }
        DrawText_(hdc, currentX, row1, 50, 16, L"BHOP:", theme::FG_DIM, s_fontSmall);
        DrawText_(hdc, currentX+40, row1, 70, 16, bhopIcon, bhopClr, s_fontSmall);
        currentX += 115;

        const char* modeName = bhop::GetModeName();
        wchar_t modeW[32]; MultiByteToWideChar(CP_UTF8, 0, modeName, -1, modeW, 32);
        DrawText_(hdc, currentX, row1, 55, 16, L"MODE:", theme::FG_DIM, s_fontSmall);
        DrawText_(hdc, currentX+40, row1, 100, 16, modeW, theme::CLR_MODE, s_fontSmall);
    }

    // Row 2: hotkeys
    DrawText_(hdc, col, row2, theme::WIN_W - 24, 16,
              L"F1:Bhop  F2:Mode  F3:Profile  F6:Suspend",
              theme::FG_HOTKEY, s_fontSmall, DT_LEFT);
}

// ════════════════════════════════════════════════════════════════
//  WINDOW PROCEDURE
// ════════════════════════════════════════════════════════════════
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#if MARCO_ENABLE_HEARTBEATS
    telemetry::g_heartbeatHook.store(timing::NowMs(), std::memory_order_relaxed);
#endif
    switch (msg) {
    case WM_PAINT: {
        engine::dbgLastRenderUs.store(timing::NowUs(), std::memory_order_relaxed);
        engine::dbgRenderCount.fetch_add(1, std::memory_order_relaxed);

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) {
            return 0;
        }

        // [FIX Bug #5] Double-buffering to prevent GDI repaint storm/flicker
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = memDC ? CreateCompatibleBitmap(hdc, theme::WIN_W, theme::WIN_H) : nullptr;
        HBITMAP oldBitmap = memBitmap ? (HBITMAP)SelectObject(memDC, memBitmap) : nullptr;

        RECT rcFull = {0, 0, theme::WIN_W, theme::WIN_H};
        RECT rcContent = {0, theme::TAB_BAR_H + 1, theme::WIN_W,
                          theme::WIN_H - theme::STATUS_BAR_H};

        if (memDC && memBitmap) {
            SetBkMode(memDC, TRANSPARENT);
            FillRect(memDC, &rcFull, s_bgBrush);
            PaintTabBar(memDC);

            // Safety clip: enforce tab content region boundary
            {
                layout::ClipGuard safetyClip(memDC, rcContent);
                switch (s_activeTab) {
#if MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
                    case theme::TAB_DASHBOARD: ui_dash::Paint(memDC, rcContent, s_snap); break;
#endif
                    case theme::TAB_SETTINGS:  ui_sett::Paint(memDC, rcContent); break;
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
                    case theme::TAB_ANALYSIS:  ui_analysis::Paint(memDC, rcContent, s_snap); break;
#endif
                }
            } // ClipGuard restores clip here

            PaintStatusBar(memDC);

#ifdef _DEBUG
            ui_diagnostics::PaintOverlay(memDC, rcFull);
#endif

            // --- IN-WINDOW TOAST (Lightweight, Top-Right) ---
            uint64_t nowUs = timing::NowUs();
            if (s_toastTriggerTimeUs > 0 && (nowUs - s_toastTriggerTimeUs) < 1500000) {
                int toastW = 220;
                int toastH = 50;
                int toastX = theme::WIN_W - toastW - 20;
                int toastY = theme::TAB_BAR_H + 10;
                
                RECT rToast = { toastX, toastY, toastX + toastW, toastY + toastH };
                FillRect(memDC, &rToast, s_ctrlBrush);
                
                HPEN oldPen = (HPEN)SelectObject(memDC, s_btnBorder);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, (HBRUSH)GetStockObject(NULL_BRUSH));
                Rectangle(memDC, rToast.left, rToast.top, rToast.right, rToast.bottom);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);

                HFONT oldF = (HFONT)SelectObject(memDC, s_fontTitle);
                SetTextColor(memDC, theme::CLR_ON);
                SetBkMode(memDC, TRANSPARENT);
                RECT rText1 = { toastX + 10, toastY + 6, toastX + toastW - 10, toastY + 26 };
                DrawTextW(memDC, s_snap.activeBrakeProfileName, -1, &rText1, DT_LEFT | DT_SINGLELINE);

                SelectObject(memDC, s_fontSmall);
                SetTextColor(memDC, theme::FG_VALUE);
                wchar_t tb[64];
                swprintf(tb, 64, L"Overlap: %u\x03BCs | Bias: %.2f", (uint32_t)s_snap.profileOverlapUs, s_snap.profileBrakeBias);
                RECT rText2 = { toastX + 10, toastY + 28, toastX + toastW - 10, toastY + 48 };
                DrawTextW(memDC, tb, -1, &rText2, DT_LEFT | DT_SINGLELINE);

                SelectObject(memDC, oldF);
            }

            // BitBlt final composited frame to window
            BitBlt(hdc, 0, 0, theme::WIN_W, theme::WIN_H, memDC, 0, 0, SRCCOPY);

            // Cleanup
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
        } else {
            // [CHAOS FALLBACK] Fallback to direct rendering on the primary HDC under near GDI exhaustion
            DLOG_WARN(UI, "GDI Exhaustion detected: falling back to single-buffered direct render");
            SetBkMode(hdc, TRANSPARENT);
            FillRect(hdc, &rcFull, s_bgBrush);
            PaintTabBar(hdc);

            // Safety clip in fallback path too
            {
                layout::ClipGuard safetyClip(hdc, rcContent);
                switch (s_activeTab) {
#if MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
                    case theme::TAB_DASHBOARD: ui_dash::Paint(hdc, rcContent, s_snap); break;
#endif
                    case theme::TAB_SETTINGS:  ui_sett::Paint(hdc, rcContent); break;
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
                    case theme::TAB_ANALYSIS:  ui_analysis::Paint(hdc, rcContent, s_snap); break;
#endif
                }
            }

            PaintStatusBar(hdc);

#ifdef _DEBUG
            ui_diagnostics::PaintOverlay(hdc, rcFull);
#endif

            // --- IN-WINDOW TOAST (Fallback) ---
            uint64_t nowUsFall = timing::NowUs();
            if (s_toastTriggerTimeUs > 0 && (nowUsFall - s_toastTriggerTimeUs) < 1500000) {
                int toastW = 220, toastH = 50;
                int toastX = theme::WIN_W - toastW - 20, toastY = theme::TAB_BAR_H + 10;
                RECT rToast = { toastX, toastY, toastX + toastW, toastY + toastH };
                FillRect(hdc, &rToast, s_ctrlBrush);
                HPEN oldPen = (HPEN)SelectObject(hdc, s_btnBorder);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rToast.left, rToast.top, rToast.right, rToast.bottom);
                SelectObject(hdc, oldBrush); SelectObject(hdc, oldPen);
                HFONT oldF = (HFONT)SelectObject(hdc, s_fontTitle);
                SetTextColor(hdc, theme::CLR_ON); SetBkMode(hdc, TRANSPARENT);
                RECT rT1 = { toastX + 10, toastY + 6, toastX + toastW - 10, toastY + 26 };
                DrawTextW(hdc, s_snap.activeBrakeProfileName, -1, &rT1, DT_LEFT | DT_SINGLELINE);
                SelectObject(hdc, s_fontSmall); SetTextColor(hdc, theme::FG_VALUE);
                wchar_t tb[64]; swprintf(tb, 64, L"Overlap: %u\x03BCs | Bias: %.2f", (uint32_t)s_snap.profileOverlapUs, s_snap.profileBrakeBias);
                RECT rT2 = { toastX + 10, toastY + 28, toastX + toastW - 10, toastY + 48 };
                DrawTextW(hdc, tb, -1, &rT2, DT_LEFT | DT_SINGLELINE);
                SelectObject(hdc, oldF);
            }

            if (memDC) DeleteDC(memDC);
            if (memBitmap) DeleteObject(memBitmap);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER: {
        // Dashboard refresh timer (fallback heartbeat)
        if (wParam == 42) {
            engine::TakeSnapshot(s_snap);
            if (s_visible && !IsIconic(hwnd)) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        // Tab click detection
        if (y < theme::TAB_BAR_H) {
            int tabW = theme::WIN_W / theme::TAB_COUNT;
            int newTab = x / tabW;
            if (newTab >= 0 && newTab < theme::TAB_COUNT && newTab != s_activeTab) {
                s_activeTab = newTab;
                UpdateTabVisibility();
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
                if (s_activeTab == theme::TAB_ANALYSIS) {
                    ui_analysis::ResetScroll();
                }
#endif
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        // Forward to settings/log/analysis for button clicks
        if (s_activeTab == theme::TAB_SETTINGS) ui_sett::OnCommand(hwnd, MAKEWPARAM(0, 0));
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
        if (s_activeTab == theme::TAB_ANALYSIS) ui_analysis::OnCommand(hwnd, MAKEWPARAM(0, 0));
#endif
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        // [FIX #18] Route engine commands to msgHwnd, not UI window
        switch (id) {
            case IDM_SHOW:    Show(); break;
            case IDM_BHOP:    PostMessage(s_msgHwnd, WM_BHOP_TOGGLE, 0, 0); break;
            case IDM_MODE:    PostMessage(s_msgHwnd, WM_BHOP_CYCLE_MODE, 0, 0); break;
            case IDM_SUSPEND: PostMessage(s_msgHwnd, WM_TOGGLE_SUSPEND, 0, 0); break;
            case IDM_EXIT:    PostMessage(s_msgHwnd, WM_CLOSE, 0, 0); break;
        }
        // Forward to active tab
        if (s_activeTab == theme::TAB_SETTINGS) ui_sett::OnCommand(hwnd, wParam);
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
        if (s_activeTab == theme::TAB_ANALYSIS) ui_analysis::OnCommand(hwnd, wParam);
#endif
        return 0;
    }

    case WM_TRAY_CALLBACK: {
        if (lParam == WM_LBUTTONUP) {
            if (s_visible) Hide(); else Show();
        } else if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu();
        }
        return 0;
    }

    case WM_UI_REFRESH: {
        engine::dbgLastRefreshUs.store(timing::NowUs(), std::memory_order_relaxed);
        engine::dbgRefreshCount.fetch_add(1, std::memory_order_relaxed);
        s_updatePending = false;
        engine::TakeSnapshot(s_snap);
        
        int currentProfileIndex = rcfg::Get().activeBrakeProfileIndex;
        if (currentProfileIndex != s_toastPrevProfileIndex) {
            if (s_toastPrevProfileIndex != -1) {
                s_toastTriggerTimeUs = timing::NowUs();
            }
            s_toastPrevProfileIndex = currentProfileIndex;
        }

        if (s_visible && !IsIconic(hwnd)) {
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        }
        return 0;
    }

    case WM_EMERGENCY_UNHOOK:
        PostMessage(s_msgHwnd, WM_EMERGENCY_UNHOOK, 0, 0);
        return 0;

    case WM_ANALYSIS_ETW_START_FAILED:
        MessageBoxW(hwnd,
                    L"Failed to start ETW Kernel Logger.\n\nERROR_ACCESS_DENIED.\n\nYou must run CS2 Macro Suite as an Administrator to use ETW Diagnostics.",
                    L"Access Denied",
                    MB_ICONERROR | MB_TOPMOST);
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && rcfg::Get().minimizeToTray) {
            Hide();
            return 0;
        }
        break;

    case WM_CLOSE:
        // [FIX #17] UI close → signal the message window to initiate shutdown
        // Do NOT call PostQuitMessage from UI window
        config_io::Save(rcfg::Get());
        PostMessage(s_msgHwnd, WM_CLOSE, 0, 0);
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        // [FIX #17] Do NOT call PostQuitMessage here — msgHwnd owns that
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdcCtrl = (HDC)wParam;
        SetTextColor(hdcCtrl, theme::FG_VALUE);
        SetBkColor(hdcCtrl, theme::BG_CONTROL);
        return (LRESULT)s_ctrlBrush;
    }
    
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) {
            HDC hdcBtn = dis->hDC;
            RECT rcBtn = dis->rcItem;
            
            // Background color logic
            HBRUSH bgBrush = s_bgBtn;
            if (dis->itemState & ODS_SELECTED) bgBrush = s_bgBtnPr;
            else if (dis->itemState & ODS_HOTLIGHT) bgBrush = s_bgBtnHov;

            FillRect(hdcBtn, &rcBtn, bgBrush);
            
            // Border
            HPEN oldPen = (HPEN)SelectObject(hdcBtn, s_btnBorder);
            HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdcBtn, nullBrush);
            Rectangle(hdcBtn, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom);
            SelectObject(hdcBtn, oldBrush);
            SelectObject(hdcBtn, oldPen);
            
            // Text
            wchar_t text[128];
            GetWindowTextW(dis->hwndItem, text, 128);
            HFONT oldFont = (HFONT)SelectObject(hdcBtn, s_fontBody);
            SetBkMode(hdcBtn, TRANSPARENT);
            SetTextColor(hdcBtn, theme::FG_TITLE);
            DrawTextW(hdcBtn, text, -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdcBtn, oldFont);
            return TRUE;
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
#if MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
        if (s_activeTab == theme::TAB_DASHBOARD) {
            ui_dash::OnMouseWheel(delta);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
#endif
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
        if (s_activeTab == theme::TAB_ANALYSIS) {
            ui_analysis::OnMouseWheel(delta);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
#endif
        if (s_activeTab == theme::TAB_SETTINGS) {
            ui_sett::OnMouseWheel(delta);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    // [FIX #20] Handle Explorer restart — re-add tray icon
    if (s_taskbarCreatedMsg && msg == s_taskbarCreatedMsg) {
        AddTrayIcon();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ════════════════════════════════════════════════════════════════
//  PUBLIC API
// ════════════════════════════════════════════════════════════════
HWND Create(HINSTANCE hInst, HWND msgHwnd) {
    s_hInst = hInst;
    s_msgHwnd = msgHwnd;

    topology::PinUIThread();

    // [FIX #20] Register for Explorer restart notification
    s_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Brushes
    s_bgBrush    = CreateSolidBrush(theme::BG_WINDOW);
    s_panelBrush = CreateSolidBrush(theme::BG_PANEL);
    s_tabBrush   = CreateSolidBrush(theme::BG_TAB_BAR);
    s_tabActBrush= CreateSolidBrush(theme::BG_TAB_ACTIVE);
    s_ctrlBrush  = CreateSolidBrush(theme::BG_CONTROL);
    s_bgBtn      = CreateSolidBrush(theme::BG_BUTTON);
    s_bgBtnPr    = CreateSolidBrush(theme::BG_BUTTON_PR);
    s_bgBtnHov   = CreateSolidBrush(theme::BG_BUTTON_HOV);
    s_btnBorder  = CreatePen(PS_SOLID, 1, theme::CLR_BORDER);

    // Fonts
    s_fontTitle = CreateFontW(-14, 0,0,0, FW_SEMIBOLD,  0,0,0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH|FF_SWISS, L"Segoe UI");
    s_fontBody  = CreateFontW(-13, 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH|FF_SWISS, L"Segoe UI");
    s_fontSmall = CreateFontW(-12, 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH|FF_MODERN, L"Consolas");

    // Register class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = CLASS_NAME;
    wc.hbrBackground  = s_bgBrush;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon          = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    RegisterClassExW(&wc);

    // Center on screen
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT wRect = {0, 0, theme::WIN_W, theme::WIN_H};
    AdjustWindowRect(&wRect, style, FALSE);
    int adjW = wRect.right - wRect.left;
    int adjH = wRect.bottom - wRect.top;

    int wx = (sx - adjW) / 2;
    int wy = (sy - adjH) / 2;

    s_hwnd = CreateWindowExW(0, CLASS_NAME, L"CS2 Macro Suite",
        style,
        wx, wy, adjW, adjH,
        nullptr, nullptr, hInst, nullptr);

    if (!s_hwnd) return nullptr;

    // Init sub-modules
    ui_sett::Init();
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
    ui_analysis::Init();
#endif
    UpdateTabVisibility();

    // Tray icon
    AddTrayIcon();

    // Dashboard refresh timer (250ms)
    s_refreshTimer = SetTimer(s_hwnd, 42, rcfg::Get().dashboardRefreshMs, nullptr);

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);

    DLOG_INFO(UI, "UI window created");
    return s_hwnd;
}

void Destroy() {
    if (s_refreshTimer) { KillTimer(s_hwnd, 42); s_refreshTimer = 0; }
    RemoveTrayIcon();
    
    // Destroy window first to prevent any late paint (WM_PAINT) using deleted GDI objects
    if (s_hwnd) { DestroyWindow(s_hwnd); s_hwnd = nullptr; }
    
    // Safely delete all GDI brushes and fonts
#if MARCO_ENABLE_FORENSIC_UI || MARCO_ENABLE_RELEASE_DASHBOARD
    ui_dash::Destroy();
#endif
#if MARCO_ENABLE_FORENSIC_UI && MARCO_ENABLE_ETW
    ui_analysis::Destroy();
#endif
    if (s_fontTitle) { DeleteObject(s_fontTitle); s_fontTitle = nullptr; }
    if (s_fontBody)  { DeleteObject(s_fontBody);  s_fontBody  = nullptr; }
    if (s_fontSmall) { DeleteObject(s_fontSmall); s_fontSmall = nullptr; }
    if (s_bgBrush)   { DeleteObject(s_bgBrush);   s_bgBrush   = nullptr; }
    if (s_panelBrush){ DeleteObject(s_panelBrush); s_panelBrush= nullptr; }
    if (s_tabBrush)  { DeleteObject(s_tabBrush);   s_tabBrush  = nullptr; }
    if (s_tabActBrush){DeleteObject(s_tabActBrush); s_tabActBrush=nullptr;}
    if (s_ctrlBrush) { DeleteObject(s_ctrlBrush);  s_ctrlBrush=nullptr; }
    if (s_bgBtn)     { DeleteObject(s_bgBtn);      s_bgBtn = nullptr; }
    if (s_bgBtnPr)   { DeleteObject(s_bgBtnPr);    s_bgBtnPr = nullptr; }
    if (s_bgBtnHov)  { DeleteObject(s_bgBtnHov);   s_bgBtnHov = nullptr; }
    if (s_btnBorder) { DeleteObject(s_btnBorder);  s_btnBorder = nullptr; }
}

void RefreshDashboard() {
    OnStateChanged();
}

void OnStateChanged() {
    HWND target = s_hwnd;
    if (!s_updatePending && target && IsWindow(target)) {
        s_updatePending = true;
        PostMessage(target, WM_UI_REFRESH, 0, 0);
    }
}

void Show() {
    if (s_hwnd) { ShowWindow(s_hwnd, SW_RESTORE); SetForegroundWindow(s_hwnd); s_visible = true; }
}
void Hide() {
    if (s_hwnd) { ShowWindow(s_hwnd, SW_HIDE); s_visible = false; }
}
bool IsVisible() { return s_visible; }

// Expose fonts/brushes to sub-modules
HFONT GetTitleFont() { return s_fontTitle; }
HFONT GetBodyFont()  { return s_fontBody; }
HFONT GetSmallFont() { return s_fontSmall; }
HBRUSH GetPanelBrush(){ return s_panelBrush; }
HWND GetMainHwnd()   { return s_hwnd; }
HINSTANCE GetHInst() { return s_hInst; }

int GetLineHeight(HDC hdc, HFONT font) {
    HFONT old = (HFONT)SelectObject(hdc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    return tm.tmHeight + tm.tmExternalLeading;
}

} // namespace ui
