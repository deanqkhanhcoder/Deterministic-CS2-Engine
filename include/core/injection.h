#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Input Injection Engine                  ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "types.h"

namespace injection {

// Single key injection (uses scan codes for game compatibility)
void KeyDown(Key k);
void KeyUp(Key k);

// Batch injection (multiple events in single SendInput call)
void KeyDownUp(Key k);  // Down then Up in one call

// Raw SendInput wrapper for advanced batching
void SendBatch(INPUT* inputs, int count);

void Mouse1Down();
void Mouse1Up();

} // namespace injection
