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

static bool PathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool DirectoryExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring ParentDirectory(std::wstring dir) {
    if (dir.empty()) return dir;
    std::replace(dir.begin(), dir.end(), L'/', L'\\');
    while (!dir.empty() && dir.back() == L'\\') {
        dir.pop_back();
    }
    size_t slash = dir.find_last_of(L'\\');
    if (slash == std::wstring::npos) return L"";
    return dir.substr(0, slash + 1);
}

static bool HasProjectRootMarkers(const std::wstring& dir) {
    return PathExists(dir + L"PROJECT_BRAIN.md") ||
           (PathExists(dir + L"Makefile") && DirectoryExists(dir + L"src"));
}

std::wstring GetProjectRootW() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return L"";
    }

    std::wstring fullPath(path);
    // Normalize slashes
    std::replace(fullPath.begin(), fullPath.end(), L'/', L'\\');

    size_t lastSlash = fullPath.rfind(L'\\');
    std::wstring exeDir = (lastSlash != std::wstring::npos)
        ? fullPath.substr(0, lastSlash + 1)
        : L".\\";

    std::wstring probe = exeDir;
    for (int i = 0; i < 8 && !probe.empty(); ++i) {
        if (HasProjectRootMarkers(probe)) {
            return probe;
        }
        probe = ParentDirectory(probe);
    }

    std::wstring lowerPath = ToLower(fullPath);

    // Runtime builds live under <project-root>\runtime\bin\.
    size_t runtimeBinPos = lowerPath.rfind(L"\\runtime\\bin\\");
    if (runtimeBinPos != std::wstring::npos) {
        return fullPath.substr(0, runtimeBinPos + 1); // keep trailing slash
    }

    // Look for \bin\ or \build\ boundaries to step back to the true workspace root.
    size_t binPos = lowerPath.rfind(L"\\bin\\");
    if (binPos != std::wstring::npos) {
        return fullPath.substr(0, binPos + 1); // keep trailing slash
    }
    
    size_t buildPos = lowerPath.rfind(L"\\build\\");
    if (buildPos != std::wstring::npos) {
        return fullPath.substr(0, buildPos + 1); // keep trailing slash
    }

    return exeDir;
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

std::wstring GetRuntimeRootW() {
    return GetProjectRootW() + L"runtime\\";
}

std::string GetRuntimeRootA() {
    return GetProjectRootA() + "runtime\\";
}

std::wstring GetBinRootW() {
    return GetRuntimeRootW() + L"bin\\";
}

std::string GetBinRootA() {
    return GetRuntimeRootA() + "bin\\";
}

std::wstring GetArtifactRootW() {
    return GetRuntimeRootW() + L"artifacts\\";
}

std::string GetArtifactRootA() {
    return GetRuntimeRootA() + "artifacts\\";
}

std::wstring GetCaptureRootW() {
    return GetRuntimeRootW() + L"captures\\";
}

std::string GetCaptureRootA() {
    return GetRuntimeRootA() + "captures\\";
}

std::wstring GetCrashRootW() {
    return GetRuntimeRootW() + L"crash\\";
}

std::string GetCrashRootA() {
    return GetRuntimeRootA() + "crash\\";
}

std::wstring GetLogRootW() {
    return GetRuntimeRootW() + L"logs\\";
}

std::string GetLogRootA() {
    return GetRuntimeRootA() + "logs\\";
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

void EnsureBinDirectoryExists() {
    CreateDirectoryRecursiveW(GetBinRootW());
}

void EnsureArtifactDirectoryExists() {
    CreateDirectoryRecursiveW(GetArtifactRootW());
}

void EnsureCaptureDirectoryExists() {
    CreateDirectoryRecursiveW(GetCaptureRootW());
}

void EnsureCrashDirectoryExists() {
    CreateDirectoryRecursiveW(GetCrashRootW());
}

void EnsureRuntimeDirectoriesExist() {
    CreateDirectoryRecursiveW(GetRuntimeRootW());
    EnsureBinDirectoryExists();
    EnsureLogDirectoryExists();
    EnsureArtifactDirectoryExists();
    EnsureCaptureDirectoryExists();
    EnsureCrashDirectoryExists();
}

} // namespace workspace
