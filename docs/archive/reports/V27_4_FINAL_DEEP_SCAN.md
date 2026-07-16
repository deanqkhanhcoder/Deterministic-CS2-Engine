# MARCO V27.4 FINAL DEEP SCAN REPORT

## Overview
This is the final deep scan of V27.4 prior to locking the architecture for V27.5. The focus was strictly on Memory Safety, Threading, and Long Session Stability.

All findings below are 100% verified via static code analysis and do not overlap with any previously registered bugs in the Knowledge Base (Focus Desync, LUT Data Race, Resolver Starvation, etc.). 

**Scan #1 (Memory Safety)** returned clean. No dangling pointers, lifetime bugs, or unsafe cache pointers were found. 

**Scan #2 (Threading) & Scan #3 (Long Session Failure)** identified 3 severe architectural flaws that threaten the stability of the engine during extended gameplay.

---

## FINDING 1: ForensicRingBuffer Disk I/O Spinlock Starvation
* **Severity**: CRITICAL
* **Category**: THREADING / LONG SESSION FAILURE (Priority Inversion)
* **File**: `src/core/telemetry.cpp` and `include/core/telemetry.h`
* **Function**: `ForensicRingBuffer::FlushToFile()` and `ForensicRingBuffer::Push()`
* **Line Range**: `telemetry.cpp:80-120`, `telemetry.h:110-120`
* **Exact Reasoning**:
  The forensic logging mechanism utilizes a background thread (`s_forensicThread`) that wakes up every 1000ms to call `FlushToFile()`. Inside `FlushToFile()`, it acquires an atomic spinlock (`lock.test_and_set`) and subsequently performs synchronous disk I/O (`fprintf` and `fclose`) while holding the lock.
  Simultaneously, `Push()` implements a strict, infinite spin-wait (`while (lock.test_and_set) { _mm_pause(); }`) to acquire the same lock. `Push()` is heavily utilized by `PublishEngineState()` (via `LOGICAL_PHYSICAL_DIVERGENCE` and `COUNTERSTRAFE_CONFLICT` traps), which executes directly on the OS Hook Thread (`KeyboardProc`).
  If a user inputs a keystroke that triggers an anomaly precisely when the background thread is flushing to disk, the Critical-Priority Hook Thread will infinite-spin waiting for the disk I/O to complete. Disk I/O can easily block for 50-500ms. Windows strictly enforces a `LowLevelHooksTimeout` (default 300ms); exceeding this will cause the OS to silently terminate the engine's hooks, leading to permanent input capture death during long sessions.
* **Reproduction Possibility**: High. Guaranteed to occur inevitably during long competitive sessions due to the frequent 1-second auto-flush overlapping with rapid player inputs.

---

## FINDING 2: Watchdog Self-Deadlock (Watchdog Masking)
* **Severity**: HIGH
* **Category**: THREADING (Lock Inversion / Deadlock)
* **File**: `src/core/state_engine.cpp`
* **Function**: `StartWatchdog()` (Watchdog Thread Lambda)
* **Line Range**: `state_engine.cpp:180-210`
* **Exact Reasoning**:
  The primary objective of the Watchdog is to detect thread starvation (e.g., if the Hook Thread or Timer Thread hangs) and execute `TriggerEmergencyFlush()` to unhook and save the process. 
  However, the Watchdog's routine health-check loop attempts to acquire `std::lock_guard<std::mutex> lock(s_stateMutex);` to verify logical/physical corruption. Because `s_stateMutex` is the central lock acquired by the Hook Thread and Timer Thread for all input routing and state publishing, any thread that hangs while processing inputs will inevitably be holding `s_stateMutex`.
  Consequently, the Watchdog thread itself will deadlock permanently on `lock(s_stateMutex)` before it can even evaluate the heartbeat timestamps or execute the emergency flush. The fail-safe system is completely neutralized by the exact deadlock scenario it was built to recover from.
* **Reproduction Possibility**: 100% guaranteed. If the engine stalls in the core input pipeline, the Watchdog will silently hang with it.

---

## FINDING 3: Synchronous Disk I/O Inside Low-Level Hook Callback
* **Severity**: HIGH
* **Category**: LONG SESSION FAILURE (Hook Drop / Thread Blocking)
* **File**: `src/core/input_capture.cpp`
* **Function**: `IsTargetActive()`
* **Line Range**: `input_capture.cpp:115-125`
* **Exact Reasoning**:
  When a focus loss event occurs (e.g., the user Alt-Tabs out of the game), the target validation logic executes `if (s_wasTargetActive && !isActive)`. Inside this block, the engine explicitly calls `telemetry::FlushForensicLog()`.
  Crucially, `IsTargetActive()` is invoked directly from `KeyboardProc` on every keystroke to determine semantic routing. Therefore, if `KeyboardProc` is the first to detect the focus loss, it will synchronously execute `FlushForensicLog()` -> `fopen` -> `fprintf` -> `fclose` directly on the OS Hook Thread.
  Performing direct file system I/O within a `WH_KEYBOARD_LL` callback is a severe violation of Win32 threading constraints and is a direct vector for hook timeout termination.
* **Reproduction Possibility**: Guaranteed to occur on the first keystroke immediately following an Alt-Tab or focus loss event.

## Conclusion
The architecture is memory-safe, but suffers from severe threading and architectural constraints regarding Disk I/O overlapping with the low-level hook execution context. These 3 findings require immediate refactoring in V27.5 to decouple I/O from critical execution paths and to make the Watchdog truly lock-free.