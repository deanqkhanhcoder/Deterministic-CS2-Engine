# File Audit Report B

## Changes Made to Remove Fire Delay Architecture

### 1. `src/core/autofire_controller.cpp`
- **Instant Firing**: Modified `OnLButtonDown()` to remove the calculation and scheduling of `maxBrakeUs` for `Key::Mouse1`. The function now triggers the necessary counter-strafe braking logic (via `InjectAutoFireBrake()`) without returning `true` or swallowing the physical left click. The return type was also simplified to `void`.
- **Removed Stale Functions**: Completely removed `CancelPendingShot()` and `CancelPendingShotLocked()` since the engine no longer delays and swallows mouse clicks. Similarly, `OnLButtonUp()` was eliminated as there are no pending shots to cancel.
- **Brake Refactoring**: Simplified `InjectAutoFireBrake()` by removing all references to `s_state.autoFire.suspendedMovementMask` and `s_state.autoFire.injectedCounterMask`. The function still logically releases the originally held key and logically injects the counter key.

### 2. `src/core/timer_lifecycle.cpp`
- **Removed Mouse Timer Support**: Removed the `if (k == Key::Mouse1)` block inside `OnTimerExpired()`, as `Mouse1` injections are no longer scheduled or executed by the timer.
- **Movement Key Restoration**: Added robust logic to `OnTimerExpired()` to handle the restoration of movement keys after an autofire counter-strafe brake completes. 
  - If a counter-strafe brake timer expires and the logically injected counter key is no longer physically held, it is safely released.
  - The script checks the opposite key (the originally held movement key). If it remains physically held, it is logically pressed again, seamlessly restoring movement without requiring the player to re-press the key. 
  - Axis states (`Positive`, `Negative`, `Conflict`, `None`) are updated accurately during this restoration to correctly interface with the `ResolveAxis` system.

### 3. `src/core/input_capture.cpp`
- **Removed Mouse Event Swallowing**: Updated `MouseProc` to pass `WM_LBUTTONDOWN` through directly without checking the return value of `engine::OnLButtonDown()`, as we no longer swallow this physical input. 
- **Removed Event Handling**: Dropped `engine::OnLButtonUp()` invocation during `WM_LBUTTONUP`.

### 4. `include/core/state.h` & `include/core/state_engine.h`
- **State Cleanup**: Removed the `autoFire` struct and its internal fields (`active`, `suspendedMovementMask`, `injectedCounterMask`, `expectedShotId`) from the `State` struct since this entire state-tracking mechanism is obsolete.
- **Header Definitions**: Cleaned up `OnLButtonDown()` to return `void`, and removed `OnLButtonUp()` and `CancelPendingShotLocked()` from `state_engine.h` and `engine_internal.h`.

### Race Condition & Stability Mitigations
- **Locking & Synchronization**: Maintained the `s_stateMutex` locks across the input capture pipeline. By relying directly on the `phys` and `logical` keys array instead of a separate bitmask, the logic flawlessly leverages existing physics reconstruction checks, eliminating state desync bugs during rapid spam clicking or overlap scenarios.
- **Conflict Handling**: When `OnTimerExpired()` restores an opposite key, it rigorously computes `s_state.axisState` directly from `s_state.logical`. If both keys are active (e.g. Snap Tap interactions mid-brake), `AxisState::Conflict` correctly triggers default game engine behaviors, making the restoration robust against unexpected player input.
