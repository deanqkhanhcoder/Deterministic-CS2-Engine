#include <windows.h>
#include <shellapi.h>
#include "system_tray.h"
#include "resource.h"
#include "types.h"
#include "debug_logger.h"

#include <cassert>
#include <cwchar>
#include <string>
#include <vector>

namespace {
std::vector<DWORD> notifications;
std::vector<UINT> posted;
std::vector<UINT_PTR> menuItems;
std::wstring tooltip;
bool notifySuccess = true;
bool menuSuccess = true;
bool loadSuccess = true;
UINT choice = 0;
UINT toggleFlags = 0;
int launches = 0;
int destroyed = 0;
constexpr UINT taskbarMessage = 0xC001;

BOOL FakeNotify(DWORD operation, PNOTIFYICONDATAW data) {
    assert(data->cbSize == sizeof(*data));
    assert(data->uID == IDI_MARCO);
    notifications.push_back(operation);
    tooltip = data->szTip;
    return notifySuccess;
}
HICON FakeLoad(HINSTANCE, LPCWSTR name) {
    assert(name == MAKEINTRESOURCEW(IDI_MARCO));
    return loadSuccess ? reinterpret_cast<HICON>(1) : nullptr;
}
UINT FakeRegister(LPCWSTR name) {
    assert(std::wcscmp(name, L"TaskbarCreated") == 0);
    return taskbarMessage;
}
HINSTANCE FakeExecute(HWND, LPCWSTR verb, LPCWSTR url, LPCWSTR, LPCWSTR, INT) {
    assert(std::wcscmp(verb, L"open") == 0);
    assert(std::wcscmp(url, L"http://127.0.0.1:47650") == 0);
    ++launches;
    return reinterpret_cast<HINSTANCE>(33);
}
HMENU FakeCreateMenu() { return menuSuccess ? reinterpret_cast<HMENU>(2) : nullptr; }
BOOL FakeAppend(HMENU, UINT flags, UINT_PTR command, LPCWSTR) {
    menuItems.push_back(command);
    if (command == ID_TRAY_TOGGLE) toggleFlags = flags;
    return TRUE;
}
BOOL FakeCursor(LPPOINT point) { *point = {100, 200}; return TRUE; }
BOOL FakeForeground(HWND) { return TRUE; }
BOOL FakeTrack(HMENU, UINT flags, int x, int y, int, HWND, const RECT*) {
    assert(flags & TPM_RETURNCMD);
    assert(x == 100 && y == 200);
    return static_cast<BOOL>(choice);
}
BOOL FakePost(HWND, UINT message, WPARAM value, LPARAM) {
    posted.push_back(message == WM_COMMAND ? static_cast<UINT>(value) : message);
    return TRUE;
}
BOOL FakeDestroy(HMENU) { ++destroyed; return TRUE; }
} // namespace

namespace dlog {
void Write(Subsystem, Level, const char*, int, const char*, ...) {}
}

// Exercise production branches with local Win32 fakes: no real tray, menus,
// browser launch, engine messages, or desktop input in the safe test suite.
#define Shell_NotifyIconW FakeNotify
#define LoadIconW FakeLoad
#define RegisterWindowMessageW FakeRegister
#define ShellExecuteW FakeExecute
#define CreatePopupMenu FakeCreateMenu
#define AppendMenuW FakeAppend
#define GetCursorPos FakeCursor
#define SetForegroundWindow FakeForeground
#define TrackPopupMenu FakeTrack
#define PostMessageW FakePost
#define DestroyMenu FakeDestroy
#include "../../src/core/system_tray.cpp"

int main() {
    const HWND window = reinterpret_cast<HWND>(3);
    assert(tray::Init(window, nullptr, false));
    assert(notifications.back() == NIM_ADD);
    assert(tooltip.find(L"ACTIVE") != std::wstring::npos);
    const auto unchanged = notifications.size();
    tray::Update(false);
    assert(notifications.size() == unchanged);
    tray::Update(true);
    assert(notifications.back() == NIM_MODIFY);
    assert(tooltip.find(L"PAUSED") != std::wstring::npos);

    assert(tray::HandleMessage(window, WM_TRAY_CALLBACK, IDI_MARCO, WM_LBUTTONUP));
    assert(tray::HandleMessage(window, WM_TRAY_CALLBACK, IDI_MARCO, NIN_KEYSELECT));
    assert(launches == 2);
    choice = ID_TRAY_TOGGLE;
    assert(tray::HandleMessage(window, WM_TRAY_CALLBACK, IDI_MARCO, WM_RBUTTONUP));
    assert((menuItems == std::vector<UINT_PTR>{ID_TRAY_OPEN, ID_TRAY_TOGGLE, ID_TRAY_EXIT}));
    assert(!(toggleFlags & MF_CHECKED));
    assert(posted.back() == ID_TRAY_TOGGLE);
    assert(destroyed == 1);
    assert(tray::HandleMessage(window, WM_COMMAND, ID_TRAY_TOGGLE, 0));
    assert(posted.back() == WM_TOGGLE_SUSPEND);
    assert(tray::HandleMessage(window, WM_COMMAND, ID_TRAY_EXIT, 0));
    assert(posted.back() == WM_CLOSE);
    assert(tray::HandleMessage(window, WM_COMMAND, ID_TRAY_OPEN, 0));
    assert(launches == 3);
    assert(!tray::HandleMessage(window, WM_COMMAND, 1, 0));
    assert(!tray::HandleMessage(window, WM_TRAY_CALLBACK, 999, WM_LBUTTONUP));

    tray::Update(false);
    choice = 0;
    assert(tray::HandleMessage(window, WM_TRAY_CALLBACK, IDI_MARCO, WM_RBUTTONUP));
    assert(toggleFlags & MF_CHECKED);
    assert(posted.back() == WM_NULL);
    menuSuccess = false;
    assert(tray::HandleMessage(window, WM_TRAY_CALLBACK, IDI_MARCO, WM_RBUTTONUP));
    assert(destroyed == 2);

    assert(tray::HandleMessage(window, taskbarMessage, 0, 0));
    assert(notifications.back() == NIM_ADD);
    tray::Shutdown();
    assert(notifications.back() == NIM_DELETE);
    const auto count = notifications.size();
    tray::Shutdown();
    assert(notifications.size() == count);
    assert(!tray::HandleMessage(window, taskbarMessage, 0, 0));

    notifySuccess = false;
    assert(!tray::Init(window, nullptr, false));
    notifySuccess = true;
    assert(tray::HandleMessage(window, taskbarMessage, 0, 0));
    assert(notifications.back() == NIM_ADD);
    tray::Shutdown();
    loadSuccess = false;
    assert(!tray::Init(window, nullptr, false));
    tray::Shutdown();
}
