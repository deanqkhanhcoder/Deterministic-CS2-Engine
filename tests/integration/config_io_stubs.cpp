#include "workspace.h"

#include <windows.h>
#include <string>

namespace workspace {

static std::wstring TestRoot() {
    wchar_t temp[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    if (length == 0 || length >= MAX_PATH) return L"";
    std::wstring root(temp);
    root += L"marco_config_io_test\\";
    return root;
}

std::wstring GetProjectRootW() { return TestRoot(); }
std::wstring GetBinRootW() { return TestRoot(); }
void EnsureBinDirectoryExists() {
    const std::wstring root = TestRoot();
    CreateDirectoryW(root.c_str(), nullptr);
}

} // namespace workspace
