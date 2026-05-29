#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — State Engine                            ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "types.h"
#include "state.h"
#include <atomic>

struct RuntimeSnapshot;  // forward decl

// Forward declare HWND
struct HWND__;
typedef HWND__* HWND;

namespace engine {

extern std::atomic<int64_t> dbgLastHookUs;
extern std::atomic<int64_t> dbgLastNotifyUs;
extern std::atomic<int64_t> dbgLastRefreshUs;
extern std::atomic<int64_t> dbgLastRenderUs;
extern std::atomic<uint32_t> dbgEventSeq;
extern std::atomic<uint32_t> dbgPublishCount;
extern std::atomic<uint32_t> dbgRefreshCount;
extern std::atomic<uint32_t> dbgRenderCount;

void Init(HWND hwnd);

// ── Key event handlers (called from hook) ──
void HandleKeyDown(Key k, bool routeSemantic);
void HandleKeyUp(Key k, bool routeSemantic);

// ── System key updates ──
void OnSysKeyChange(bool isLCtrl, bool down, bool routeSemantic);
void OnShiftChange(bool down, bool routeSemantic);
void OnSpaceDown(bool routeSemantic);
void OnSpaceUp();
bool OnLButtonDown();
void OnLButtonUp();


void RebuildState();

// ── Timer expiry callback ──
void OnTimerExpired(Key k, uint64_t timerId);

// ── Watchdog ──
void RunWatchdog();
void StartWatchdog();
void StopWatchdog();
void TriggerEmergencyFlush();

// ── Suspend/Resume ──
void ToggleSuspend();
bool IsSuspended();
void ClearHeldKeys();


// ── State access ──
State GetState();

// ── Snapshot for UI (copies state fields, minimal time) ──
void TakeSnapshot(RuntimeSnapshot& out);

// ── Hook status tracking ──
void SetHookInstalled(bool v);

void ClearStateDirty();

} // namespace engine
