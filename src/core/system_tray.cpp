#include "system_tray.h"
#include "resource.h"
#include "types.h"
#include "debug_logger.h"

#include <shellapi.h>

namespace tray {
namespace {
NOTIFYICONDATAW icon{};
UINT taskbarCreated = 0;
bool added = false;
bool paused = false;

bool AddIcon() {
    added = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    if (!added) DLOG_ERR(UI, "Failed to add Marco tray icon: %lu", GetLastError());
    return added;
}

void OpenDashboard() {
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"open", L"http://127.0.0.1:47650", nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) DLOG_ERR(UI, "Dashboard launch failed: %lld", static_cast<long long>(result));
}

void ShowMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        DLOG_ERR(UI, "Failed to create tray menu: %lu", GetLastError());
        return;
    }
    const bool ready = AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open Dashboard") &&
        AppendMenuW(menu, MF_STRING | (paused ? MF_UNCHECKED : MF_CHECKED),
                    ID_TRAY_TOGGLE, L"Toggle Engine (Active/Pause)") &&
        AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit Marco");
    POINT cursor{};
    if (ready && GetCursorPos(&cursor)) {
        SetForegroundWindow(window);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                            cursor.x, cursor.y, 0, window, nullptr);
        PostMessageW(window, WM_NULL, 0, 0);
        if (command) PostMessageW(window, WM_COMMAND, command, 0);
    } else {
        DLOG_ERR(UI, "Failed to populate tray menu: %lu", GetLastError());
    }
    DestroyMenu(menu);
}
} // namespace

bool Init(HWND window, HINSTANCE instance, bool suspended) {
    Shutdown();
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = IDI_MARCO;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = WM_TRAY_CALLBACK;
    // LoadIconW returns a shared resource; Windows owns its lifetime.
    icon.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_MARCO));
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!icon.hIcon || !taskbarCreated) {
        DLOG_ERR(UI, "Tray initialization failed: %lu", GetLastError());
        return false;
    }
    Update(suspended);
    // Keep the default callback version for WM_LBUTTONUP / WM_RBUTTONUP.
    return AddIcon();
}

void Update(bool suspended) {
    if (icon.szTip[0] && paused == suspended) return;
    paused = suspended;
    wcscpy_s(icon.szTip, paused ? L"Marco Engine — PAUSED" : L"Marco Engine — ACTIVE");
    if (added && !Shell_NotifyIconW(NIM_MODIFY, &icon)) {
        DLOG_WARN(UI, "Failed to update Marco tray icon: %lu", GetLastError());
    }
}

bool HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated && message == taskbarCreated && icon.hWnd) {
        // HWND_MESSAGE windows do not receive this Explorer broadcast.
        AddIcon();
        return true;
    }
    if (message == WM_TRAY_CALLBACK && wParam == icon.uID) {
        switch (lParam) {
            case WM_LBUTTONUP:
            case NIN_SELECT:
            case NIN_KEYSELECT: OpenDashboard(); break;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU: ShowMenu(window); break;
        }
        return true;
    }
    if (message == WM_COMMAND && lParam == 0) {
        switch (LOWORD(wParam)) {
            case ID_TRAY_OPEN: OpenDashboard(); return true;
            case ID_TRAY_TOGGLE: PostMessageW(window, WM_TOGGLE_SUSPEND, 0, 0); return true;
            case ID_TRAY_EXIT: PostMessageW(window, WM_CLOSE, 0, 0); return true;
        }
    }
    return false;
}

void Shutdown() {
    if (added) Shell_NotifyIconW(NIM_DELETE, &icon);
    added = false;
    icon = {};
    taskbarCreated = 0;
}
} // namespace tray
