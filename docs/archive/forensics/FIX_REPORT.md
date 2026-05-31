# Fix Report

Date: 2026-05-30
Runtime evidence: `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`, `runtime/runtime/logs/marco_debug.log`

## Root Cause

The hook was still receiving keyboard input, but movement events were routed with `route=0` after a target focus loss:

```text
Target focus LOST
Key DOWN: D (route=0)
Key DOWN: A (route=0)
Key DOWN: W (route=0)
```

In source, `route=0` disables both affected systems:

- Counter-Strafe: WASD semantic routing requires `isActive && !IsSuspended() && CAP_CSTRAFE`.
- BHOP: Space/BHOP routing requires `isActive && bhopEnabled && CAP_BHOP`.

`input_capture::IsTargetActive()` depended on `s_activeHwnd`, which was updated by `WinEventProc(EVENT_SYSTEM_FOREGROUND)`. If that foreground cache stayed stale on a non-target window, the hook continued to receive input but computed `isActive=false`, leaving both Counter-Strafe and BHOP inactive.

## Files Modified

| File | Function | Change |
| --- | --- | --- |
| `src/core/input_capture.cpp` | `IsTargetActiveForUI()` | Re-sample foreground HWND with `GetForegroundWindow()` before the UI/swallow active check so Space/BHOP swallow reconciliation does not use stale foreground state. |
| `src/core/input_capture.cpp` | `IsTargetActive()` | Re-sample foreground HWND with `GetForegroundWindow()` on each active check, refresh `s_activeHwnd`, and retry target resolution when the cached foreground has not changed but active publication still does not match. |

## Why The Patch Works

Before:

```text
IsTargetActive()
-> read cached s_activeHwnd
-> compare to target_platform active publication
-> stale non-target cache can keep isActive=false
-> route=0 persists
```

After:

```text
IsTargetActive()
-> read real foreground via GetForegroundWindow()
-> refresh s_activeHwnd
-> compare real foreground to target publication
-> retry ResolveTargetAsync while not active
-> focus regained path can call engine::RebuildState()
-> route=1 resumes
```

This does not change Counter-Strafe math, BHOP timing, timer values, runtime config values, or injected gameplay behavior. It only repairs active-window truth feeding the existing routing and Space swallow gates.

## Related Findings

- `LOGICAL_PHYSICAL_DIVERGENCE` is not sufficient root-cause proof by itself because `phys=false logical=true` is expected during synthetic Counter-Strafe brake windows.
- `PROFILE_CHANGED` correlates with recovery, but `rcfg::Apply()` does not repair engine state. Recovery is performed by `engine::RebuildState()` on focus regain.
- `BHOP_STALL` is not direct proof of the same WASD divergence. Its current detector can fire from a stale `lastInjectionTick` across sequences.
- Timer subsystem appeared healthy in the runtime evidence and was not modified.

## Verification

| Command | Result |
| --- | --- |
| `make debug` | EXECUTED, exit 0 |
| `make profile` | EXECUTED, exit 0 |
| `make release` | EXECUTED, exit 0 |

## Regression Risk

| Risk | Assessment |
| --- | --- |
| Hook latency | Low to medium. `GetForegroundWindow()` now runs inside active checks. This is a Win32 foreground read, not filesystem or UI work. |
| Resolver queue traffic | Low. While inactive, repeated target resolution can be retried. The resolver queue is bounded and already deduplicates consecutive duplicate tasks. |
| Gameplay behavior | Low. No Counter-Strafe, BHOP, timer, or config parameters changed. |
| Focus behavior | Intended change. Stale cached focus should no longer keep routing disabled after foreground truth changes. |

## Not Tested

- Live gameplay reproduction after patch: NOT TESTED.
- Runtime forensic confirmation that route remains `1` after the previously observed failure window: NOT TESTED.
- Long-session field stability: NOT TESTED.
