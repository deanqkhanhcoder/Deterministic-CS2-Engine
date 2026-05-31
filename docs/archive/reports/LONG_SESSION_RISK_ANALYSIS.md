# LONG SESSION RISK ANALYSIS - V27.4 RED TEAM PASS

Date: 2026-05-30

Scope: source-based risk analysis for long sessions: 6 hours gameplay, repeated Alt-Tab, repeated profile switching, Settings open/close, BHOP spam, and Counter-Strafe spam.

Actual 6-hour runtime soak: NOT EXECUTED.

This report uses source inspection and available logs only. Any claim requiring a controlled 6-hour run is marked INSUFFICIENT EVIDENCE.

## High-Risk State Machines

| State Machine | Source Path | Stuck/Degraded Condition | Recovery Condition | Evidence Level |
|---|---|---|---|---|
| Active target routing | `KeyboardProc()` -> `IsTargetActive()` -> route flags (`src/core/input_capture.cpp:324-377`) | `GetForegroundWindow()` is correct but `target_platform::GetCurrentIdentity()` is stale/null, producing transient `route=0`. | `ResolveTargetAsync()` publishes target, then a later `IsTargetActive()` triggers focus regain and `engine::RebuildState()`. | Source-proven path; active-gameplay failure after patch is INSUFFICIENT EVIDENCE. |
| Resolver queue | `IsTargetActive()` -> `ResolveTargetAsync()` (`src/core/input_capture.cpp:102-112`, `src/core/target_platform.cpp:140-166`) | Repeated inactive checks can enqueue foreground identities; queue drops oldest when full. | Resolver worker processes latest identities and publishes matched profile. | Source-proven churn path; no runtime failure proof. |
| Cached foreground consumers | `GetActiveWindowFast()` (`src/core/input_capture.cpp:540-541`) read by BHOP and resolver (`src/core/bhop.cpp:260-264`, `src/core/bhop.cpp:321-336`, `src/core/target_platform.cpp:185-187`) | Cache can be stale if foreground event delivery lags and no hook active check refreshes it. | `WinEventProc()` or patched active checks refresh `s_activeHwnd`. | Source-proven remaining cache dependency. |
| Focus cleanup/rebuild | Focus loss clears held keys (`src/core/input_capture.cpp:119-135`); focus gain rebuilds (`src/core/input_capture.cpp:137-161`, `src/core/state_reconciliation.cpp:83-132`) | Rapid oscillation can repeatedly cancel timers, release logical keys, and rebuild from `GetAsyncKeyState()`. | Stable foreground target and a successful rebuild. | Runtime logs show frequent focus transitions; no permanent stuck proven. |
| Physical/logical reconciliation | `ClearHeldKeys()` and `ReconcileInternal(true)` (`src/core/state_reconciliation.cpp:39-48`, `src/core/state_reconciliation.cpp:135-152`) | Cleanup clears logical/timers but preserves physical truth; while inactive, `phys=true logical=false` can persist until key-up or rebuild. | Focus regain `RebuildState()` or a routed key-up/key-down sequence. | Source-proven temporary divergence; permanent stuck not proven. |
| Counter-Strafe timer lifecycle | `AutoCounterStrafe()` schedules timers (`src/core/counterstrafe_controller.cpp:278-326`); `OnTimerExpired()` releases/restores state (`src/core/timer_lifecycle.cpp:17-75`) | Stale timer callbacks are rejected when IDs mismatch; active timer counters can remain nonzero if timers are cancelled or pending. | Expected timer release or cancel/rebuild path. | Current logs show zero `TIMER_REJECTED`; long soak still NOT EXECUTED. |
| Axis conflict | `ResolveAxis()` and `NeutralizeAxis()` (`src/core/counterstrafe_controller.cpp:17-101`) | Holding both keys on an axis intentionally enters conflict and neutralizes logical keys. | Release one key; `ResolveAxis()` restores survivor logical state. | Source-backed expected state. Current logs have no conflict anomaly. |
| BHOP worker predicate | `BhopThreadFunc()` wait predicate (`src/core/bhop.cpp:281-287`) | Worker sleeps while `s_spaceHeld=false`, `bhopEnabled=false`, or `s_waitingForSpaceRepress=true`. | Fresh `bhop::OnSpaceDown()`, config enable, or `ForceSpaceSync()` during rebuild. | Source-proven; current random death not proven from this. |
| BHOP waiting-for-repress | `ToggleEnabled()` and `OnSuspendChanged()` (`src/core/bhop.cpp:431-468`) | Toggling/suspend while Space is held intentionally requires a release/repress. It can look like BHOP death. | Release and press Space again, or `ForceSpaceSync(false)` during rebuild. | Source-proven expected behavior; runtime user confusion possible. |
| BHOP cached-HWND abort | `BhopThreadFunc()` compares cached active window to sequence HWND (`src/core/bhop.cpp:321-336`) | Stale cache can abort or fail to abort sequence incorrectly. | Cache refresh via foreground event or hook active check; next Space sequence. | Source-proven remaining cached-reader risk. |
| BHOP stall telemetry | Stall check uses `lastInjectionTick` across sequences (`src/core/bhop.cpp:278-319`) | `BHOP_STALL` can fire before the first dispatch of a new sequence if enough time elapsed since previous injection. | It self-resets `lastInjectionTick`; sequence may still continue. | Current log has 69 stalls; debug log shows normal sequence starts/ends. |
| Runtime config publication | `rcfg::Apply()` seqlock and clamping (`src/core/runtime_config.cpp:71-241`) | Profile spam repeatedly rebuilds LUTs and emits `PROFILE_CHANGED`; it does not rebuild input state. | Config publication completes; focus regain/rebuild handles state if focus changed. | Source-proven; no corruption evidence. |

## Scenario Walkthroughs

### 6 Hours Gameplay

Possible long-session risks:

- Timer counters can grow and derived active counters can remain nonzero during active play.
- Focus cache and target publication can drift across many foreground changes.
- BHOP stall telemetry can accumulate false-positive-looking events.

Source proof of permanent stuck after the current patch: NOT FOUND.

Runtime certification from an actual 6-hour patched session: NOT EXECUTED.

### Repeated Alt-Tab

Call stack:

```text
WinEventProc(EVENT_SYSTEM_FOREGROUND)
-> IsTargetActive()
-> focus lost: ClearHeldKeys(), bhop::OnSpaceUp(), clear swallow state
-> focus gained: sample GetAsyncKeyState(), engine::RebuildState()
-> ReconcileLogicalStateFromPhysical()
```

Risk:

- Duplicate or rapid transitions can repeatedly clear/rebuild.
- If active target publication lags, first events after regain can still route `0`.

Permanent stuck proof: INSUFFICIENT EVIDENCE.

### Repeated Profile Switching

Call stack:

```text
UI/hotkey
-> rcfg::Apply()
-> movement::InitLUT()
-> telemetry PROFILE_CHANGED
```

Risk:

- `rcfg::Apply()` does not call `engine::RebuildState()`.
- Observed recovery around profile changes is likely adjacent focus/rebuild, not profile apply itself.

Permanent stuck proof: NOT FOUND.

### Settings Open/Close

Call stack is focus-driven if Settings steals foreground:

```text
foreground changes to Marco/settings/non-target
-> IsTargetActive() false
-> ClearHeldKeys()
foreground returns to game
-> IsTargetActive() true
-> RebuildState()
```

Risk:

- If Settings causes repeated foreground churn, route can repeatedly flip.
- Current debug logs show many focus transitions and rebuilds.

Permanent stuck proof: NOT FOUND.

### BHOP Spam

Call stack:

```text
KeyboardProc Space
-> IsTargetActive()
-> engine::OnSpaceDown(shouldRoute)
-> bhop::OnSpaceDown()
-> BhopThreadFunc()
-> DispatchJump()
```

Risk:

- If the Space down edge routes inactive, BHOP may not start until rebuild or repress.
- `s_waitingForSpaceRepress` can intentionally block the worker.
- `BHOP_STALL` telemetry remains noisy due stale `lastInjectionTick`.

Permanent stuck proof: INSUFFICIENT EVIDENCE.

### Counter-Strafe Spam

Call stack:

```text
KeyboardProc WASD
-> IsTargetActive()
-> engine::HandleKeyDown/HandleKeyUp(routeSemantic)
-> ResolveAxis()
-> AutoCounterStrafe()
-> timing::ScheduleTimerUs()
-> engine::OnTimerExpired()
```

Risk:

- If a key-down edge routes inactive, physical state can be true while logical remains false until key-up or rebuild.
- AutoCounterStrafe intentionally aborts on opposite physical key, axis conflict, or too-short tap.
- Timer path appears healthy in current logs, but no long soak was executed.

Permanent stuck proof after patch: NOT FOUND.

## Long-Session Verdict

No source path was found that proves a permanent dead state after the current patch. Several temporary disabled/degraded paths remain source-plausible, especially around async target publication, cached foreground readers, and BHOP waiting/stall telemetry.

Certification status: ROOT CAUSE LIKELY BUT NOT CERTIFIED.
