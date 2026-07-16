# V27.6 FINAL AUDIT REPORT
*Date: 2026-06-01*

## 1. Executive Summary
This document presents the findings of the final deep audit of the Marco Engine V27.6 Release Candidate. The audit verified the execution of the Engine Maturity Pass and Observability Slimdown. While the primary systems (Watchdog, F8 tracking, Autofire) were successfully dismantled without affecting the core gameplay features, the codebase still harbors significant remnants of dead code, configuration drift, and documentation mismatch. **V27.6 is not approved for stabilization.**

## 2. Findings & Evidence

### A. Dead Macro & Diagnostic Bloat
* **Files:** `src/ui/ui_main.cpp`, `src/ui/ui_dashboard.cpp`, `src/core/main.cpp`, `include/ui/ui_diagnostics.h`, `src/ui/ui_diagnostics.cpp`, `Makefile`
* **Issue:** The `MARCO_ENABLE_DIAGNOSTICS` macro was removed from `build_config.h`, meaning it implicitly evaluates to `0`. However, the source tree still contains over 15 `#if MARCO_ENABLE_DIAGNOSTICS` blocks spanning multiple UI and Core files. These blocks are now permanently dead code. Furthermore, `src/ui/ui_diagnostics.cpp` was stripped of its implementation but remains in the build pipeline (`Makefile`) as a virtually empty file.
* **Evidence:** Grep reveals 11 usages in `ui_main.cpp` (e.g., lines 29, 248, 257). `ui_diagnostics.cpp` contains only `#include` headers and an empty namespace.

### B. Watchdog Configuration Drift
* **Files:** `include/core/runtime_config.h`, `src/core/config_io.cpp`, `include/core/debug_logger.h`
* **Issue:** Although the Watchdog thread and logic were purged, the config parser still loads and saves Watchdog settings, and the logging subsystem still defines a Watchdog category.
* **Evidence:** `runtime_config.h:47` defines `int watchdogIntervalMs = 10000;`. `config_io.cpp:142` calls `ReadInt(A, L"WatchdogIntervalMs")`. `debug_logger.h:16` defines `Subsystem::Watchdog`.

### C. Documentation Stagnation
* **Files:** `include/core/timing.h`
* **Issue:** The V27.6 Maturity Report and Knowledge Base proudly state that the deprecated `WM_TIMER_EXPIRED` message was removed. However, the official header documentation for the Timer subsystem still claims it relies on this message.
* **Evidence:** `timing.h:19` states `// Starts dedicated timer thread. hwnd receives WM_TIMER_EXPIRED messages.`

## 3. Risk Assessment

| Issue | Impact | Likelihood | Risk Level |
|---|---|---|---|
| Dead Macro Bloat | Increases binary size marginally and severely harms code maintainability. | 100% | **MEDIUM** |
| Config Drift | Confuses end-users configuring `WatchdogIntervalMs` thinking it does something. | 100% | **LOW** |
| Docs Stagnation | Misleads developers regarding the Timer architecture. | 100% | **LOW** |

## 4. Release Verdict

**OUTCOME B: V27.6 NOT APPROVED**

**Reasoning:** The Engine Maturity Pass explicitly demanded the removal of obsolete diagnostic infrastructure and dead build flags. The massive scattering of dead `#if MARCO_ENABLE_DIAGNOSTICS` blocks and leftover Watchdog configuration keys prove that the cleanup was incomplete.

**Targeted Fix Recommendations before next RC:**
1. Manually strip all `#if MARCO_ENABLE_DIAGNOSTICS` branches from `ui_main.cpp`, `ui_dashboard.cpp`, and `main.cpp` (retaining the `#else` fallback paths where applicable, such as `InvalidateRect`).
2. Delete `ui_diagnostics.cpp` and `ui_diagnostics.h` entirely, and remove them from the `Makefile`.
3. Remove `watchdogIntervalMs` from `RuntimeConfig` and `config_io.cpp`.
4. Remove `Subsystem::Watchdog` from `debug_logger.h`.
5. Update comments in `timing.h` to mention `engine::OnTimerExpired` instead of `WM_TIMER_EXPIRED`.
