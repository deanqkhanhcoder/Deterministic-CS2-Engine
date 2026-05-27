# V26 Deterministic Core Invariants

This document outlines the strict mathematical and architectural guarantees that must NEVER be violated by any future contributor.

## 1. Concurrency & Locking
* **`s_stateMutex` (State Engine):** Must be acquired for any physical, logical, or axis state modification.
* **`s_spinlock` (Timing Engine):** Must be acquired to schedule, cancel, or evaluate timers.
* **Lock Ordering Rule:** `s_stateMutex` -> `s_spinlock`. `s_spinlock` is strictly a leaf lock. It must NEVER attempt to acquire `s_stateMutex`.

## 2. SendInput Lifecycle (OS Isolation)
* **NEVER** call `SendInput` while holding `s_stateMutex`.
* All SendInput operations must be queued into an `InjectionBatch`.
* The `InjectionBatch::flush()` method must be invoked strictly **after** unlocking all mutexes. This prevents message queue starvation and ring-0/user-mode deadlock.

## 3. Mathematical Physics Determinism
* The logical output state (`s_state.logical`) must perfectly mirror the mathematical resolution of `s_state.axisState`.
* If `axisState` is `Conflict`, both POS and NEG keys MUST logically evaluate to `false`.
* The offline regression suite (`deterministic_simulator.exe`) guarantees that the C++ logic perfectly replicates the Python mathematical baseline (LUT). Any deviation > 0.0 drift is a fatal regression.

## 4. Timer Lifecycle
* A timer callback is considered **STALE** if its `expectedTimerId` does not match the actual `id` stored in the state. Stale timers must return immediately without mutation.
* AutoFire ownership dictates that the `Mouse1` timer operates independently of WASD timers, but utilizes the exact same `InjectionBatch` deferral mechanism to restore suspended keys.

## 5. Walk/Shift Modifiers
* Walk accumulation (`s_state.walk.accumUs`) must be deterministically applied before CounterStrafe evaluation to preserve frame-perfect overlaps.
