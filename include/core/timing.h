#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — High-Precision Timing Engine            ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "types.h"
#include <cstdint>
#include <atomic>
#include <windows.h>

namespace timing {

// ── QPC ──
void   Init();
int64_t NowUs();   // Microseconds since boot
int64_t NowMs();   // Milliseconds since boot

// ── Timer Thread ──
// Starts dedicated timer thread. Triggers engine::OnTimerExpired direct callbacks.
void   StartTimerThread(HWND hwnd);
void   StopTimerThread();

// Schedule a one-shot timer. On expiry, triggers a direct callback.
// wParam = (WPARAM)key, lParam = (LPARAM)timerId
uint64_t ScheduleTimerUs(Key key, int64_t durationUs);
uint64_t ScheduleTimer(Key key, int durationMs);
void   CancelTimer(Key key);
bool   AreTimersActive();

} // namespace timing
