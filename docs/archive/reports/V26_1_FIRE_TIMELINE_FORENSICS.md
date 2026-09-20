# V26.1 Deterministic Fire Timeline Forensics

## Executive Summary
This document outlines the architectural changes made during the V26.1 timeline rebuild, specifically targeting the non-deterministic subtick artifacts caused by OS-level injection latency. 

The core issue was that the stabilization timer was scheduled *before* the counter-strafe brake inputs were physically flushed to the OS via `SendInput`. This caused the timer to count down while the OS was still processing the batch injection, leading to sluggish peeks and inconsistent shot delays.

## Timeline Architecture Changes

### Old Timeline (Non-Deterministic)
1. Detect Fire Intention
2. Schedule Timer (`now + preFireUs`)
3. Inject Brake Batch
4. Execute `batch.flush()` (Takes 0.5ms - 2.0ms depending on OS scheduler)
5. Timer Expires (Effective wait time is `preFireUs - flushLatency`)
6. Dispatch Shot

### New Timeline (Deterministic)
1. Detect Fire Intention
2. Calculate Stabilization Need (Competitive Heuristics)
3. Inject Brake Batch
4. Execute `batch.flush()`
5. Timestamp `flushDoneUs`
6. Calculate `shotDeadlineUs = flushDoneUs + preFireUs`
7. Schedule Timer
8. Dispatch Shot (Effective wait time is exactly `preFireUs`)

## Competitive Feel Safety Heuristics
To preserve crisp, responsive gameplay, we implemented strict heuristic overrides that prevent sluggishness:

- **Immediate Dispatch:** If `brakeUs` (the amount of braking force needed) is less than the configured `minStopMs` threshold, the stabilization phase is completely bypassed.
- **Dynamic Calculation:** The subtick timer is only engaged when there is a significant movement vector that requires hard stabilization.
- **Clock Domain Unification:** All timing uses the `timing::NowUs()` monotonic QPC source, eliminating drift between `chrono` limits and OS sleep quanta.

## Safety Invariants Asserted
We successfully instrumented a strict invariant asserting that a `RESTORE_PHASE` on any given fire generation must **never** precede its `SHOT_DISPATCH`. If the OS delays the timer thread excessively and causes a race condition where movement restoration is scheduled, the engine will trigger a hard `RESTORE_OVERLAP_VIOLATION` warning to log the snapshot and dump the physical state.

## Telemetry & Validation
The following trace points were added to the pipeline to measure micro-latencies at every stage:
- `BATCH_FLUSH_BEGIN`
- `SENDINPUT_DISPATCH_BEGIN`
- `SENDINPUT_DISPATCH_END`
- `BATCH_FLUSH_COMPLETE`
- `SHOT_TIMER_ARMED`
- `SHOT_DISPATCH`

All profiles (`debug`, `profile`, `release`) have been rebuilt successfully and integrated.
