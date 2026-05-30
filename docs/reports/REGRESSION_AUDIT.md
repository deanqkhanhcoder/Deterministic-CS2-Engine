# REGRESSION AUDIT - V27.4 FOCUS ACTIVE-CHECK PATCH

Date: 2026-05-30

Scope: red-team review of the current patch in `src/core/input_capture.cpp`, specifically `IsTargetActive()` and `IsTargetActiveForUI()`. No code changes were made in this pass.

## Patch Surface

Reviewed functions:

- `capture::IsTargetActiveForUI()` at `src/core/input_capture.cpp:57-67`
- `capture::IsTargetActive()` at `src/core/input_capture.cpp:87-161`
- Keyboard routing at `src/core/input_capture.cpp:324-377`
- Mouse routing at `src/core/input_capture.cpp:449-455`
- Foreground event handling at `src/core/input_capture.cpp:464-473`
- Cached foreground reader `capture::GetActiveWindowFast()` at `src/core/input_capture.cpp:540-541`

## Regression Matrix

| Regression Vector | Source Path | Worst Case | Evidence Status | Risk |
|---|---|---|---|---|
| `GetForegroundWindow()` on every hook event | `KeyboardProc()` -> `IsTargetActive()` (`src/core/input_capture.cpp:324`, `src/core/input_capture.cpp:87-95`) | A high-frequency key stream now pays a Win32 foreground query per event. If the call stalls under desktop/overlay contention, hook latency can rise. | Current debug log still shows heavy route=1 activity, but no latency histogram was analyzed for this pass. | MEDIUM |
| Resolver retry spam | `IsTargetActive()` queues `ResolveTargetAsync()` every inactive check when the foreground HWND is unchanged but publication is not active (`src/core/input_capture.cpp:109-112`) | Typing while a non-target foreground window is active can repeatedly enqueue resolver work. Queue is bounded but background churn can increase. | Source-proven; runtime volume not measured. | MEDIUM |
| Resolver publication lag still creates transient route=0 | `IsTargetActive()` compares sampled foreground to current target publication (`src/core/input_capture.cpp:99-112`); routing uses that result immediately (`src/core/input_capture.cpp:324-377`) | Game is foreground, but `target_platform` has not published it yet. First key/Space edge routes as inactive, then later resolver publish repairs state. | Source-proven transient path. Runtime proof that this still causes gameplay failure is INSUFFICIENT EVIDENCE. | MEDIUM |
| Duplicate focus transitions / rebuild spam | `WinEventProc()` calls `IsTargetActive()` (`src/core/input_capture.cpp:464-473`), and hook events also call it. Focus gain calls `engine::RebuildState()` (`src/core/input_capture.cpp:137-161`) | Rapid Alt-Tab/settings/overlay oscillation can repeatedly clear held keys, flush forensics, and rebuild state. | Current debug log shows 24 focus lost/regained pairs and repeated rebuilds in one session. | MEDIUM |
| Cached foreground readers remain outside patched active checks | `GetActiveWindowFast()` still returns `s_activeHwnd` only (`src/core/input_capture.cpp:540-541`); BHOP worker and resolver use it (`src/core/bhop.cpp:260-264`, `src/core/bhop.cpp:321-336`, `src/core/target_platform.cpp:185-187`) | If `WinEventProc` misses/delays a foreground update and no hook active check refreshes it, BHOP worker or resolver can make decisions from stale cached state. | Source-proven remaining risk. Runtime proof of failure is INSUFFICIENT EVIDENCE. | MEDIUM |
| BHOP stall telemetry remains noisy after patch | `BhopThreadFunc()` keeps `lastInjectionTick` outside per-sequence reset (`src/core/bhop.cpp:278-319`) | Logs keep reporting `BHOP_STALL` even when BHOP sequences start/end normally, reducing diagnostic clarity. | Current later log contains 69 `BHOP_STALL`; debug log shows many BHOP sequence starts/ends. | MEDIUM |
| Canonical log path drift persists | Runtime header reports `runtime/runtime/logs/...` | Engineers may inspect `runtime/logs/` and miss the real evidence. | Proven: `runtime/logs/` is absent while logs exist under `runtime/runtime/logs/`. | HIGH for observability, LOW for gameplay |
| Focus loss flush cost in hook-adjacent path | Focus loss/gain pushes forensic events and calls `telemetry::FlushForensicLog()` (`src/core/input_capture.cpp:122-126`, `src/core/input_capture.cpp:140-144`) | Frequent focus oscillation can flush from a focus-event path repeatedly. | Runtime logs show frequent focus transitions; hook latency impact not measured. | LOW-MEDIUM |
| First key after focus regain can be consumed by rebuild timing | Focus regain calls `RebuildState()` before returning active (`src/core/input_capture.cpp:137-161`), then `HandleKeyDown()` may see `phys` already true and return (`src/core/input_router.cpp:45-54`) | If the key edge both triggers recovery and is already sampled physically down, semantic state repair depends on rebuild injecting/logical sync. This is intentional but delicate. | Source-proven path; no failure evidence after patch. | LOW-MEDIUM |

## `route=0` After Patch

`route=0` still exists in current debug evidence:

- Count in `runtime/runtime/logs/marco_debug.log`: 74
- Sampled contexts show `route=0` after `Target focus LOST`, followed by resolver/profile publication and focus regain.

This does not break the patch by itself. `route=0` is correct when the target is not active. The red-team concern is narrower: source still permits transient `route=0` if the game foreground HWND is sampled before `target_platform` has published the matching target identity.

## Worst-Case Scenario

1. User Alt-Tabs or opens settings repeatedly.
2. Foreground returns to the game, but active target publication is still null/stale.
3. First WASD or Space event calls `IsTargetActive()`.
4. `GetForegroundWindow()` is correct, but `GetCurrentIdentity()` is not yet updated.
5. `routeThis=false`; WASD/Space edge is treated as physical-only.
6. `ResolveTargetAsync()` eventually publishes the profile.
7. A later hook event calls `IsTargetActive()` and triggers focus regain/rebuild.

Expected recovery path exists. Certification gap: there is not yet a controlled runtime log proving that this transient path cannot produce a user-visible dead period under long-session conditions.

## Audit Verdict

The patch is directionally correct and addresses the most likely stale-cache mechanism. It also introduces or exposes remaining risks around resolver retry frequency, active-publication lag, cached foreground readers, and noisy BHOP stall telemetry.

Regression status: ROOT CAUSE LIKELY BUT NOT CERTIFIED.
