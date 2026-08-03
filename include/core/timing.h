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
// Production passes message-window HWND so expiry mutation and synthetic input
// run on hook-owner thread. nullptr keeps direct callback for isolated tests.
void   StartTimerThread(HWND expiryWindow);
void   StopTimerThread();
// Called by expiryWindow owner when WM_TIMER_EXPIRED arrives.
void   DrainExpiredTimers();

// Schedule one-shot timer. Production posts WM_TIMER_EXPIRED to expiryWindow.
uint64_t ScheduleTimerUs(Key key, int64_t durationUs);
uint64_t ScheduleTimer(Key key, int durationMs);
void   CancelTimer(Key key);
bool   AreTimersActive();
#ifdef MARCO_TIMING_TESTING
void SetForceWaitableTimerFailureForTesting(bool force);
void SetNextTimerIdForTesting(uint64_t nextId);
#endif

} // namespace timing
