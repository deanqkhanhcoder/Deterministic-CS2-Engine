#include "workspace.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace workspace {

static std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) {
        c = std::towlower(c);
    }
    return out;
}

std::wstring GetProjectRootW() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return L"";
    }

    std::wstring fullPath(path);
    // Normalize slashes
    std::replace(fullPath.begin(), fullPath.end(), L'/', L'\\');
    
    std::wstring lowerPath = ToLower(fullPath);
    
    // Look for \bin\ or \build\ boundaries to step back to the true workspace root
    size_t binPos = lowerPath.rfind(L"\\bin\\");
    if (binPos != std::wstring::npos) {
        return fullPath.substr(0, binPos + 1); // keep trailing slash
    }
    
    size_t buildPos = lowerPath.rfind(L"\\build\\");
    if (buildPos != std::wstring::npos) {
        return fullPath.substr(0, buildPos + 1); // keep trailing slash
    }

    // Fallback: Just return the directory containing the executable
    size_t lastSlash = fullPath.rfind(L'\\');
    if (lastSlash != std::wstring::npos) {
        return fullPath.substr(0, lastSlash + 1);
    }

    return L".\\";
}

std::string GetProjectRootA() {
    std::wstring rootW = GetProjectRootW();
    if (rootW.empty()) return "";
    
    int size = WideCharToMultiByte(CP_UTF8, 0, rootW.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    
    std::string out(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, rootW.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring GetLogRootW() {
    return GetProjectRootW() + L"logs\\";
}

std::string GetLogRootA() {
    return GetProjectRootA() + "logs\\";
}

static void CreateDirectoryRecursiveW(const std::wstring& path) {
    size_t pos = 0;
    do {
        pos = path.find_first_of(L"\\/", pos + 1);
        std::wstring subdir = path.substr(0, pos);
        CreateDirectoryW(subdir.c_str(), nullptr);
    } while (pos != std::wstring::npos);
}

void EnsureLogDirectoryExists() {
    CreateDirectoryRecursiveW(GetLogRootW());
}

} // namespace workspace
