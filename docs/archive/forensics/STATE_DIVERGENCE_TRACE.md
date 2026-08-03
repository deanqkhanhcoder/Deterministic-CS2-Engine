# State Divergence Trace

Date: 2026-05-30
Runtime evidence: `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`, `runtime/runtime/logs/marco_debug.log`

## Complete Path

```text
Physical input
-> src/core/input_capture.cpp low-level hook
-> engine::HandleKeyDown/HandleKeyUp or engine::OnSpaceDown/OnSpaceUp
-> State phys/logical arrays
-> ResolveAxis / Counter-Strafe / BHOP
-> timing::ScheduleTimerUs
-> engine::OnTimerExpired
-> injection::SendInput
```

## Divergence Locations

| Location | Function | Trigger condition | Persistence condition | Recovery condition |
| --- | --- | --- | --- | --- |
| Focus inactive physical tracking | `engine::HandleKeyDown(k, false)` | key down while `routeSemantic=false` | `phys=true`, `logical=false` until key up or rebuild | key up with `routeSemantic=false`, focus regain `RebuildState()` |
| Focus inactive release | `engine::HandleKeyUp(k, false)` | key up while inactive | clears `phys`, does not alter semantic state | prior `ClearHeldKeys()` or later `RebuildState()` |
| Normal routed key down before injection publication | `engine::HandleKeyDown(k, true)` | routed key down | brief, until logical set and state published | same function sets logical and publishes |
| Axis conflict | `ResolveAxis()` / `NeutralizeAxis()` | both keys on one axis physically down | `phys=true`, `logical=false` while conflict is active | release conflict key; `ResolveAxis()` selects survivor |
| Counter-Strafe brake | `AutoCounterStrafe()` / `ApplyOverlapCounterStrafe()` | physical key released, synthetic opposite key injected | `phys=false`, `logical=true` until timer expiry | `engine::OnTimerExpired()` releases logical key |
| Timer release path | `engine::OnTimerExpired()` | scheduled brake expires | clears logical if timer id matches | normal timer execution |
| Focus regain reconciliation | `engine::RebuildState()` | target becomes active | reconciles all WASD and Space from `GetAsyncKeyState()` | `ReconcileLogicalStateFromPhysical()` |
| BHOP space state | `bhop::ForceSpaceSync()` | focus regain rebuild | syncs BHOP `s_spaceHeld` with engine `spacePhys` | focus regain, suspend, explicit Space edge |

## Runtime Failure Path

Debug log evidence:

```text
Target focus LOST
Key DOWN: D (route=0)
Key DOWN: A (route=0)
Key DOWN: W (route=0)
...
Target focus REGAINED
Rebuilding semantic state from physical truth...
Key DOWN: D (route=1)
```

Source proof:

- `src/core/input_capture.cpp` computes `shouldRoute = isActive && !isSuspended`.
- WASD semantic routing uses `routeThis = shouldRoute && CAP_CSTRAFE`.
- BHOP routing requires `isActive && bhopEnabled && CAP_BHOP`.
- When `isActive` is false, Counter-Strafe and BHOP are both disabled even though the hook still receives physical input.

## Persistence Condition

Before the patch, `IsTargetActive()` and `IsTargetActiveForUI()` trusted `s_activeHwnd`, which was updated by `WinEventProc(EVENT_SYSTEM_FOREGROUND)`. If the foreground cache stayed on a non-target HWND, all subsequent hook input remained `route=0` until a later foreground event/resolution repaired it. On Space events, the same stale cache could also make `ReconcileSwallow()` release BHOP ownership before the main route check.

## Recovery Condition

Recovery occurs when target focus is recognized again:

```text
input_capture::IsTargetActive()
-> focus regained branch
-> engine::RebuildState()
-> ReconcileLogicalStateFromPhysical()
-> bhop::ForceSpaceSync()
-> route=1 resumes
```

## Conclusion

`phys != logical` is a symptom class, not the root cause by itself. The source-backed root failure path is stale/inactive focus routing causing `route=0`, which disables the semantic paths for both Counter-Strafe and BHOP.
