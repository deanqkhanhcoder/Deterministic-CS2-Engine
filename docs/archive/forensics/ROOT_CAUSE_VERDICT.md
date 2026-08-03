# Root Cause Verdict

Date: 2026-05-30
Runtime evidence: `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`, `runtime/runtime/logs/marco_debug.log`

## Evidence Summary

Observed from logs:

- Repeated focus lost/gained events.
- Movement input continues to reach the hook after focus loss.
- During the failure window, debug log shows repeated movement keys with `route=0`.
- After focus regained and `engine::RebuildState()`, subsequent movement keys show `route=1`.
- Timer counters continue advancing; debug log shows timers executing and injected key releases completing.
- `LOGICAL_PHYSICAL_DIVERGENCE` mostly reports `phys=0 logical=1`, which is normal during synthetic Counter-Strafe brake windows.
- `BHOP_STALL` is not direct proof of WASD divergence and has a stale `lastInjectionTick` false-positive path.

## Ranked Hypotheses

| Hypothesis | Confidence | Verdict |
| --- | ---: | --- |
| Focus oscillation / stale foreground focus cache | 72% | Best explanation. Source and debug log prove `isActive=false` causes `route=0`, disabling both Counter-Strafe and BHOP while hooks still receive input. |
| State divergence | 12% | Present in logs, but event is too broad and includes intended synthetic logical states. Not sufficient as primary cause. |
| Profile reload side effects | 8% | Correlates with recovery, but `rcfg::Apply()` does not repair engine state. Recovery is focus regain plus `RebuildState()`. |
| Timer subsystem | 4% | Timer counters and debug execution are healthy. Not the primary cause. |
| Hook failure | 3% | Hook is receiving keys in the failure window. Not the primary cause. |
| Other | 1% | No stronger source-backed candidate found. |

## Root Cause

`input_capture::IsTargetActive()` and `input_capture::IsTargetActiveForUI()` relied on `s_activeHwnd`, a cached foreground HWND updated by `WinEventProc(EVENT_SYSTEM_FOREGROUND)`. When that cache remained on a non-target HWND, the hook continued to receive keys but computed `isActive=false`, producing `route=0`.

That disables:

- Counter-Strafe routing: `routeThis = shouldRoute && CAP_CSTRAFE`
- BHOP routing: `isActive && bhopEnabled && CAP_BHOP`

This single path explains:

1. Counter-Strafe death.
2. BHOP death.
3. Recovery after focus/profile interaction, because target focus regain triggers `engine::RebuildState()`.

## Patch Decision

Root cause is sufficiently proven from source and runtime logs to patch. The patch must be limited to focus-active detection and must not change gameplay timing, Counter-Strafe behavior, BHOP behavior, or runtime config behavior.
