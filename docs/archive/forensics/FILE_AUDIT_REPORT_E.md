# FILE AUDIT REPORT E - "AutoFire" & "Subtick" Dead Code Cleanup

## 1. Overview
This audit focuses on finalizing the rollback of the **"Fire Delay" (AutoFire)** and **"Subtick"** architectures. The goal was to remove all remaining dashboard components, configuration UI rows, and dead code paths related to these systems across the codebase.

## 2. Scope of Audit & Changes

### A. Sub-tick Architecture Removal
- **`src/ui/ui_dashboard.cpp`**: Removed the "Sub-Tick Compression" status row from the dashboard UI.
- **`include/core/runtime_state.h`**: Removed the `subTickCompressionActive` boolean field from `RuntimeSnapshot`.
- **`src/core/state_engine.cpp`**: Removed the initialization and copying logic for `subTickCompressionActive`.
- **`src/core/counterstrafe_controller.cpp`**: Removed the `AlignToSubtick` function and replaced its usage in `CalculateTrueBrakeUs` with the unaligned duration, effectively bypassing subtick quantization logic.

### B. Fire Delay (AutoFire) Dead Code Removal
- **`src/core/autofire_controller.cpp`**: File deleted entirely, removing the `OnLButtonDown` hook and `InjectAutoFireBrake` functionality.
- **`src/ui/ui_settings.cpp`**: Removed the "Tap Delay" and "Spray Delay" configuration rows from the settings UI.
- **`src/core/engine_internal.h`**: Removed `InjectAutoFireBrake` declaration, as well as getter inline functions for `SPACE_DELAY_MS`, `BURST_THRESHOLD`, `SPRAY_DELAY_MS`, and `TAP_DELAY_MS`.
- **`include/core/state_engine.h`**: Removed the declaration for `void OnLButtonDown();`.
- **`src/core/input_capture.cpp`**: Removed the semantic routing call to `engine::OnLButtonDown();` inside the mouse hook (`MouseProc`).

### C. Internal State Cleanup (Fire Delay Context Variables)
- **`include/core/state.h`**: 
  - Removed circular buffer variables for click history tracking (`clickHistory`, `clickHistoryHead`, `clickHistoryCount`).
  - Removed state variables `lastSpaceTimeMs` and `lastCounterMs`.
  - Removed helper methods `PushClick`, `PruneClicks`, and `TrimClicks`.
- **`src/core/input_router.cpp`**: Removed the logic assigning `s_state.lastSpaceTimeMs` on semantic space down events.
- **`src/core/state_reconciliation.cpp`**: Removed reset logic for `lastCounterMs`, `lastSpaceTimeMs`, `clickHistoryCount`, and `clickHistoryHead`.
- **`src/core/state_engine.cpp` & `include/core/runtime_state.h`**: Removed public propagation of `lastCounterMs`.

### D. Telemetry & Indicators Verification
- **UI Files (`src/ui/*`)**: Confirmed no remaining mentions of "AutoFire", "Stabilizing", or "Fire Delay".
- **Telemetry (`telemetry.h`, `telemetry.cpp`)**: Audited telemetry source and confirmed no events named `EVENT_AUTOFIRE_XXX` exist.

## 3. Conclusion
The "Fire Delay" and "Subtick" architectures have been successfully scrubbed from the active codebase. All associated UI elements, dead code logic, and backing variables have been safely removed. No architectural orphans were found during compilation verification.
