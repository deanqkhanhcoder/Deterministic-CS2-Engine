#include <windows.h>

#include <cwchar>

namespace {

struct DesktopIsolationGuard {
    DesktopIsolationGuard() noexcept {
        constexpr DWORD kDesktopNameCapacity = 128;
        wchar_t expected[128]{};
        const DWORD expectedLength = GetEnvironmentVariableW(
            L"MARCO_EXPECTED_DESKTOP", expected, kDesktopNameCapacity);
        if (expectedLength == 0) return;
        if (expectedLength >= kDesktopNameCapacity) ExitProcess(125);

        wchar_t actual[128]{};
        DWORD needed = 0;
        const HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
        if (!desktop || !GetUserObjectInformationW(
                desktop, UOI_NAME, actual, sizeof(actual), &needed) ||
            std::wcscmp(actual, expected) != 0) {
            ExitProcess(125);
        }
    }
};

DesktopIsolationGuard g_desktopIsolationGuard;

} // namespace
