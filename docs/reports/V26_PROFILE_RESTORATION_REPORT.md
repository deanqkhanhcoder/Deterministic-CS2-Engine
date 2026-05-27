# V26 Profile Build Restoration Report

## 1. Executive Summary
Following the V26 Open Source Release, a regression was identified in the `PROFILE` build tier. The aggressive "freeze pass" inadvertently purged the Analysis tab, forensic dashboards, and ETW integrations by stripping the requisite compile flags and source files. 

This restoration pass successfully re-established the `PROFILE` build as the premier environment for deterministic performance benchmarking and telemetry analysis, without re-introducing the massive spam inherent to the `DEBUG_FORENSIC` tier.

## 2. Root Cause Analysis
During the initial freeze pass, the following misconfigurations were introduced:
1. **Feature Gate Starvation:** In `build_config.h`, `MARCO_ENABLE_ETW` and `MARCO_ENABLE_DIAGNOSTICS` were set to `0` for the `PROFILE` tier. Since the UI rendering routing (`ui_main.cpp`) gates the Analysis tab behind `#if MARCO_ENABLE_ETW`, the entire subsystem was dead-code eliminated.
2. **Build Exclusion Overreach:** In `Makefile`, `PROFILE_EXCLUDE` aggressively blocked `etw_controller.cpp`, `ui_diagnostics.cpp`, and `analysis_toolkit.cpp` from compiling, breaking linkage and preventing any forensic features from executing.

## 3. The New "Profile Build" Philosophy

The project now formally recognizes three distinct build tiers with strict subsystem ownership:

### A. Debug Build (`DEBUG_FORENSIC`)
**Purpose:** Deep system forensics, race condition debugging, validation, and UB detection.
**Allowed:** Slow execution, verbose logging, intrusive asserts, heavy tracing.

### B. Release Build (`RELEASE`)
**Purpose:** Production gameplay, minimal overhead, stable deterministic deployment.
**Prohibited:** Heavy telemetry, ETW, intrusive tracing, expensive diagnostics.

### C. Profile Build (`PROFILE`)
**Purpose:** Telemetry, ETW, latency/timing analysis, hook latency monitoring, mutex profiling, and forensic visualizers (The Analysis Tab).
**Required:** Optimized (`-O2`), deterministic, profiling-enabled.
**Prohibited:** Verbose debug spam, heavy asserts, deep watchdog logging.

## 4. Feature Matrix Alignment

The configuration flags have been restored to reflect the philosophy above:

| Feature Gate | DEBUG_FORENSIC | PROFILE | RELEASE |
| :--- | :--- | :--- | :--- |
| `MARCO_ENABLE_ETW` | `1` | `1` | `0` |
| `MARCO_ENABLE_DIAGNOSTICS` | `1` | `1` | `0` |
| `MARCO_ENABLE_TELEMETRY` | `1` | `1` | `0` |
| `MARCO_ENABLE_FORENSIC_UI` | `1` | `1` | `0` |
| `MARCO_ENABLE_LOGGING` | `1` | **`0`** | `0` |
| `MARCO_ENABLE_ASSERTS` | `1` | **`0`** | `0` |
| `MARCO_ENABLE_WATCHDOG` | `1` | **`0`** | `0` |

## 5. Architectural Cleanups
1. **`Makefile` Refactored:** `PROFILE_EXCLUDE` has been removed. The profile build now natively compiles all telemetry, ETW, and diagnostic modules.
2. **Compile-Time Routing Fixed:** By enabling the correct `#define` gates, `ui_main.cpp` automatically registers and routes rendering calls to `ui_analysis::Paint` and mounts the `TAB_ANALYSIS` button. 

## 6. Validation Results
1. **Debug Build:** Re-compiled successfully with full diagnostics.
2. **Profile Build:** Re-compiled successfully. The Analysis Tab, Timing Graphs, Hook Latency Monitor, and ETW pipelines are fully restored and functional. Deterministic execution remains intact (no deep watchdog stalls).
3. **Release Build:** Re-compiled successfully with zero overhead.

**Status:** RESTORATION COMPLETE. CLEAR FOR GITHUB DEPLOYMENT.
