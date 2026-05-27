#pragma once

#include "build_config.h"
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace ui_diagnostics {

#if MARCO_ENABLE_DIAGNOSTICS

// Global Watchdog State
extern std::atomic<uint64_t> g_uiHeartbeatUs;
extern thread_local int g_paintDepth;

// Diagnostics API
void StartWatchdog();
void StopWatchdog();

// Timers
void StartPaint();
void EndPaint();

void StartDispatch();
void EndDispatch();

void TrackMeasureLayout(uint64_t durationUs);
void TrackScroll();
void TrackInvalidate(HWND hwnd, const RECT* lpRect, BOOL bErase);

void SetDashboardScrollState(int scrollY, int viewportH, int totalH);

// Live UI Overlay
void PaintOverlay(HDC hdc, RECT rc);

#else

// Inlined stubs for zero overhead and to avoid linking issues
inline std::atomic<uint64_t> g_uiHeartbeatUs{0};
inline thread_local int g_paintDepth{0};

inline void StartWatchdog() {}
inline void StopWatchdog() {}
inline void StartPaint() {}
inline void EndPaint() {}
inline void StartDispatch() {}
inline void EndDispatch() {}
inline void TrackMeasureLayout(uint64_t) {}
inline void TrackScroll() {}
inline void TrackInvalidate(HWND, const RECT*, BOOL) {}
inline void SetDashboardScrollState(int, int, int) {}
inline void PaintOverlay(HDC, RECT) {}

#endif

} // namespace ui_diagnostics
