#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Main UI Window                                   ║
// ║  Tabbed interface with dark theme, system tray, dashboard/settings   ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>

#include "build_config.h"

namespace ui {

#if MARCO_ENABLE_UI
// Create and show the main application window. Returns the message HWND.
HWND Create(HINSTANCE hInst, HWND msgHwnd);

// Destroy the UI and clean up all resources
void Destroy();

// Refresh dashboard data (called by UI timer)
void RefreshDashboard();

// Notify UI of state changes (call from main thread after state change)
void OnStateChanged();

// Show/hide window
void Show();
void Hide();
bool IsVisible();
#else
inline HWND Create(HINSTANCE, HWND) { return NULL; }
inline void Destroy() {}
inline void RefreshDashboard() {}
inline void OnStateChanged() {}
inline void Show() {}
inline void Hide() {}
inline bool IsVisible() { return false; }
#endif

} // namespace ui
