# DEAD CODE AFTER V27.4 FOCUS PATCH

Date: 2026-05-30

Scope: documentation-only red-team audit for code and telemetry that may be obsolete after the stale focus cache patch. No deletion was performed.

Classification meanings:

- SAFE TO DELETE: no active source/build/runtime reference found in this pass.
- RISKY TO DELETE: appears stale or duplicated, but touches input/focus/observability or historical debugging.
- KEEP: still active or still useful for current diagnosis.

## Findings

| Item | Evidence | Classification | Recommendation |
|---|---|---|---|
| `s_activeHwnd` foreground cache | Still written by `WinEventProc()` and patched active checks; still read by `GetActiveWindowFast()` (`src/core/input_capture.cpp:472`, `src/core/input_capture.cpp:540-541`). | KEEP | Do not delete. The cache is still part of BHOP/resolver behavior. |
| `GetActiveWindowFast()` | Used by BHOP sequence checks and target resolver stale-task discard (`src/core/bhop.cpp:260-264`, `src/core/bhop.cpp:321-336`, `src/core/target_platform.cpp:185-187`). | KEEP, but high audit priority | Not dead. It remains a cached-reader risk after the patch. |
| `WinEventProc()` focus hook | Still updates cache and actively calls `IsTargetActive()` (`src/core/input_capture.cpp:464-473`). | KEEP | Not obsolete. Patch reduced exclusive dependence but did not replace foreground event handling. |
| `PollTarget()` | Reads cached `s_activeHwnd` and queues resolver at install (`src/core/input_capture.cpp:49-55`, `src/core/input_capture.cpp:493`). | RISKY TO DELETE | It is startup/resolver-adjacent. Do not delete without a startup target-resolution test. |
| `s_hkDownF8` | Declared/synced/cleared but no active F8 handler was found in the current source (`src/core/input_capture.cpp:47`, `src/core/input_capture.cpp:135`, `src/core/input_capture.cpp:156`). Historical reports also mark F8 docs stale. | RISKY TO DELETE | Input/focus-adjacent. Leave until hotkey ownership is reviewed. |
| Historical F8 emergency capture docs | `docs/forensics/FORENSIC_INFRA_AUDIT.md`, `docs/reports/LEGACY_INFRA_AUDIT.md`, and `docs/reports/DEAD_CODE_AUDIT.md` identify stale F8 references. | RISKY TO DELETE | Archive/update later; do not remove during red-team pass. |
| `LOGICAL_PHYSICAL_DIVERGENCE` telemetry | Produced broadly in `PublishEngineState()` on every phys/logical mismatch (`src/core/state_engine.cpp:68-79`). It creates noisy logs because synthetic brake windows are valid divergence. | KEEP, refine later | Not dead. Needs semantic refinement, not deletion. |
| `BHOP_STALL` telemetry | Current source can false-positive because `lastInjectionTick` is not reset per sequence (`src/core/bhop.cpp:278-319`). Current later log has 69 events while debug shows sequences continue. | KEEP, refine later | Do not delete. Fix semantics separately if requested. |
| `FOCUS_LOST`/`FOCUS_GAINED` events inside `RebuildState()` | Rebuild emits these names before/after reconciliation, even when not a literal foreground transition (`src/core/state_reconciliation.cpp:111-128`). | RISKY TO DELETE | Event naming is misleading. Keep until telemetry schema can be migrated. |
| Watchdog code | Wrapped in `MARCO_ENABLE_WATCHDOG`; current audits state Release disables it, but source remains active in enabled builds (`src/core/state_engine.cpp:162-260`). | KEEP | Not dead across build modes. |
| `COUNTERSTRAFE_CONFLICT` stall producer | Emits if axis conflict persists beyond 500 ms (`src/core/state_engine.cpp:83-103`). Current logs have none. | KEEP | Useful anomaly. |
| `COUNTERSTRAFE_CANCELLED` producers | `AutoCounterStrafe()` emits cancellation reasons for opposite physical key and too-short tap (`src/core/counterstrafe_controller.cpp:278-306`). | KEEP | Useful anomaly. |
| `TIMER_REJECTED` producer | Stale timer rejection emits in `OnTimerExpired()` (`src/core/timer_lifecycle.cpp:30-37`). Current logs have none. | KEEP | Useful to refute timer hypotheses. |
| `tools/archive/patch_capture.py` | Previously identified as archived one-off rewrite helper in `docs/reports/DEAD_CODE_AUDIT.md`. | SAFE TO DELETE later | No deletion in this pass. Remove only after owner confirms archive retention policy. |
| `MARCO_ENABLE_UI_OVERLAY` | Prior config drift report marks it unused. | RISKY TO DELETE | Build/config cleanup, not red-team scope. |
| Root/canonical runtime path mismatch | Actual logs under `runtime/runtime/logs/`; canonical docs say `runtime/logs/`. | KEEP issue open | Not code deadness, but observability debt remains active. |

## Dead-Code Verdict

No gameplay-adjacent code is safe to delete in this pass. The clearest stale items are F8 remnants and archived one-off scripts, but input/focus ownership risk makes them inappropriate for deletion during certification.

Deletion executed: none.
