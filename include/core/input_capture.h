#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Input Capture (LL Hooks)                ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>

#include "target_platform.h"

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

// Re-samples Win32 foreground state and returns a coherent HWND/PID/TID identity.
target_platform::TargetIdentity GetActiveIdentityFast();

void PollTarget();

// Applies focus-loss/regain reconciliation on the message thread after the
// asynchronous target resolver publishes a coherent result.
void ReconcileTargetFocus();

// Drains target-bound physical WASD edges on the message thread. Low-level
// hook callbacks only enqueue these records.
void DrainRoutedInputEvents();

} // namespace capture
