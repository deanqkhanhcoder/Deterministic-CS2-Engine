# BHOP Stall Root Cause

Date: 2026-05-30
Runtime evidence: `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`

## Producer

`BHOP_STALL` is produced in `src/core/bhop.cpp`, inside `BhopThreadFunc()`.

Event fields:

- `reason=0`
- `data1 = s_spaceHeld`
- `data2 = s_injectedSpaceState`
- `focus = true`

Runtime event:

```text
event=BHOP_STALL type=7 ... reason=0 data1=1 data2=0 focus=1
```

## Detection Logic

The detector fires when:

```text
lastInjectionTick > 0
and (QpcNow() - lastInjectionTick) * s_qpcToMs > 2000.0
```

inside the BHOP worker loop while:

```text
s_spaceHeld == true
s_running == true
s_waitingForSpaceRepress == false
```

## Source Finding

`lastInjectionTick` is declared before the outer worker loop and is not reset at the start of a new BHOP sequence. Therefore a new sequence can report `BHOP_STALL` before its first dispatch if more than 2 seconds elapsed since the previous injection.

This makes the observed `BHOP_STALL` insufficient as proof of a live BHOP injection deadlock.

## Can BHOP_STALL Be Caused By The Same Divergence?

Answer: NO, not directly from the source evidence.

Source proof:

- BHOP state is not driven by `s_state.phys[W/S/A/D]` or `s_state.logical[W/S/A/D]`.
- BHOP uses its own `s_spaceHeld`, `s_waitingForSpaceRepress`, `s_injectedSpaceState`, and active-window checks.
- The `BHOP_STALL` producer does not inspect WASD physical/logical divergence.

Shared cause possibility:

- A stale focus/active-window state can disable BHOP routing because `input_capture.cpp` only calls `bhop::OnSpaceDown()` when `isActive` is true.
- The same stale focus state also disables Counter-Strafe routing.

## Verdict

The logged `BHOP_STALL` is not source-proof of the same WASD physical/logical divergence. The common failure path for BHOP and Counter-Strafe is focus routing becoming inactive (`route=0` / `isActive=false`), not the BHOP stall event itself.

