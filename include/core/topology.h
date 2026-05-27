#pragma once

#include <windows.h>
#include <stdint.h>

namespace topology {

// Initialize the topology manager. Maps P-cores and E-cores.
void Init();

// Pin the current thread to a high-performance P-core, isolating it from SMT siblings
// if possible, and avoiding Core 0. Returns true if successful.
// Registers with MMCSS using the specified profile (e.g. "Pro Audio").
bool PinCriticalThread(const wchar_t* mmcssProfile);

// Pin the main hook thread to a dedicated P-core and elevate it to TIME_CRITICAL.
// Registers with MMCSS using the specified profile (e.g. "Games").
bool PinHookThread(const wchar_t* mmcssProfile);

// Pin the UI thread to a P-core or balanced core to ensure responsiveness.
bool PinUIThread();

// Pin the current thread to an E-core or lower priority logical processor to prevent
// it from interfering with critical threads. Returns true if successful.
bool PinBackgroundThread();

// Mark the main thread (usually floats, but gets high priority).
bool SetMainThreadPriority();

// Check if two processor cores share the same physical core (SMT siblings)
bool AreSmtSiblings(uint16_t groupA, uint8_t cpuA, uint16_t groupB, uint8_t cpuB);

} // namespace topology
