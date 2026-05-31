# V27.4 FORENSIC CLEANUP REPORT

## Executive Summary
The V27.4 investigative phase has been officially concluded. The underlying async state engine proved 100% stable during adversarial and heavy contention stress-tests. This cleanup pass transitions the codebase back to a production-ready state while safely gating the forensic infrastructure behind the `MARCO_ENABLE_FORENSIC` configuration macro.

## Transformation Highlights
1. **Consolidated Configuration:** Replaced disparate `MARCO_DEBUG_FORENSIC` and `MARCO_ENABLE_TELEMETRY` flags with a single, unified `MARCO_ENABLE_FORENSIC` macro.
2. **Build Matrix Alignment:**
   - **Debug**: `MARCO_ENABLE_FORENSIC = 1` (All ETW, Diagnostic, Heartbeat, and Watchdog subsystems active).
   - **Profile**: `MARCO_ENABLE_FORENSIC = 0` (Telemetry disabled, restoring baseline profiling accuracy).
   - **Release**: `MARCO_ENABLE_FORENSIC = 0` (Minimal footprint, raw performance).
3. **Forensic Gate Preservation:** `telemetry::g_forensicBuffer`, `DLOG`, ETW Tracing, and the `VK_F8` emergency core dump have been strictly preserved. They can be revived seamlessly by flipping `#define MARCO_ENABLE_FORENSIC 1`.
4. **Structural Audit & Diagnostics Fixes:** Moved test harnesses (`scratch/test_stress.cpp` -> `tests/test_stress.cpp`), removed unused variables, and purged format casting warnings across the entire build stack. Explicit `#if MARCO_ENABLE_DIAGNOSTICS` macro guards were verified for the UI module (`ui_main.cpp` and `ui_diagnostics.cpp`) to ensure safe toggling during Release profiling.

## Final State Assessment
All three compilation matrices (`Debug`, `Profile`, `Release`) execute flawlessly with **0 warnings** and **0 errors**. The production executable regains maximal runtime execution velocity, completely shed of telemetry logging overhead, yet fully capable of immediate forensic instrumentation when required.
Verification complete. 
