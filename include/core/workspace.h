#pragma once

#include <string>

namespace workspace {

// Returns the true project root directory dynamically resolved from the executable.
// Guaranteed to strip out \bin\debug\ or \build\release\ directories and resolve to the top-level project root.
// Trailing backslash is guaranteed.
std::wstring GetProjectRootW();
std::string GetProjectRootA();

// Returns the normalized workspace log root for the current build flavor.
// e.g. "C:\Path\To\marco\logs\debug\"
// Trailing backslash is guaranteed.
std::wstring GetLogRootW();
std::string GetLogRootA();

// Recursively creates the directory structure for the current log root.
void EnsureLogDirectoryExists();

} // namespace workspace
