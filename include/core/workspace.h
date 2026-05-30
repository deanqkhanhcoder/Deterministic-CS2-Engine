#pragma once

#include <string>

namespace workspace {

// Returns the true project root directory dynamically resolved from the executable.
// Guaranteed to strip out \bin\debug\ or \build\release\ directories and resolve to the top-level project root.
// Trailing backslash is guaranteed.
std::wstring GetProjectRootW();
std::string GetProjectRootA();

// Returns canonical runtime roots. Trailing backslash is guaranteed.
std::wstring GetRuntimeRootW();
std::string GetRuntimeRootA();

std::wstring GetBinRootW();
std::string GetBinRootA();

std::wstring GetArtifactRootW();
std::string GetArtifactRootA();

std::wstring GetCaptureRootW();
std::string GetCaptureRootA();

std::wstring GetCrashRootW();
std::string GetCrashRootA();

// Returns the normalized runtime log root.
// e.g. "C:\Path\To\marco\runtime\logs\"
// Trailing backslash is guaranteed.
std::wstring GetLogRootW();
std::string GetLogRootA();

// Recursively creates canonical runtime directory structures.
void EnsureRuntimeDirectoriesExist();
void EnsureBinDirectoryExists();
void EnsureArtifactDirectoryExists();
void EnsureCaptureDirectoryExists();
void EnsureCrashDirectoryExists();
void EnsureLogDirectoryExists();

} // namespace workspace
