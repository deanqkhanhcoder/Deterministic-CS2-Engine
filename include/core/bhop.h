#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Bhop Engine — Native C++ Port                                  ║
// ║  State machine: IDLE → JUMP_START → AIRBORNE_LOCK → LANDING_SCAN   ║
// ║  4 modes: Legit / Aggressive / Humanized / Scroll Emulation         ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>

namespace bhop {

// ── Bhop modes ──
enum class Mode : int {
    Legit      = 1,
    Aggressive = 2,
    Humanized  = 3,
    ScrollEmu  = 4,
    COUNT      = 4
};

// ── State machine states ──
enum class State : int {
    Idle         = 0,
    JumpStart    = 1,
    AirborneLock = 2,
    LandingScan  = 3
};

// ── Lifecycle ──
void Init();
void Shutdown();   // Must be called before process exit — joins worker thread

// ── Queries (thread-safe) ──
Mode        GetMode();        // [FIX R-6] Returns atomic snapshot
State       GetState();       // [FIX R-6] Returns atomic snapshot
const char* GetModeName();
const char* GetStateName();
bool        IsWaitingForSpaceRepress();

// ── Controls (called from main thread via PostMessage) ──
void ToggleEnabled();
void CycleMode();
void OnSuspendChanged();

// ── Input signals (called from hook thread — returns immediately) ──
void OnSpaceDown();   // Signal: physical Space pressed
void OnSpaceUp();     // Signal: physical Space released

} // namespace bhop
