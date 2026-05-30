#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Input Capture (LL Hooks)                ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>

namespace capture {

// Install keyboard and mouse low-level hooks
bool Install(HWND hwnd);

// Uninstall hooks
void Uninstall();

// Reinstall hooks if uninstalled, using the saved message window handle
bool Reinstall();

// Check if target is active (thread-safe for UI)
bool IsTargetActiveForUI();

bool IsHookInstalled();

// Samples the foreground window and refreshes the shared foreground cache.
HWND GetActiveWindowFast();

void PollTarget();

} // namespace capture
