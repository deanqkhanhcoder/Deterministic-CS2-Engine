# Profile Recovery Analysis

Date: 2026-05-30
Runtime evidence: `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`, `runtime/runtime/logs/marco_debug.log`

## Observation

The user observed that switching profile restores functionality.

The forensic log records `PROFILE_CHANGED` events around recovery windows. The debug log shows the actual route recovery sequence:

```text
Target focus LOST
movement keys route=0
config/BHOP/profile action
Target focus REGAINED
Rebuilding semantic state from physical truth...
movement keys route=1
```

## What PROFILE_CHANGED Means

`PROFILE_CHANGED` is emitted by `rcfg::Apply()` in `src/core/runtime_config.cpp`.

Fields:

- `reason` = `validated.activeBrakeProfileIndex`
- `data1` = `validated.bhopMode`
- `data2` = `validated.bhopEnabled`

This event is broader than a Counter-Strafe profile switch. It also appears for BHOP enable/mode/config changes because they all call `rcfg::Apply()`.

## Profile Switch Path

F3 hotkey path:

```text
input_capture KeyboardProc
-> WM_CYCLE_PROFILE
-> main MsgWndProc
-> rcfg::GetMutable()
-> cfg.activeBrakeProfileIndex = (index % 4) + 1
-> rcfg::Apply(cfg)
-> config_io::Save(cfg)
-> ui::OnStateChanged()
```

Settings UI path:

```text
ui_settings button
-> rcfg::GetMutable()
-> activeBrakeProfileIndex assignment
-> config_io::Save(cfg)
-> rcfg::Apply(cfg)
```

## What Actually Repairs State

`rcfg::Apply()` does not call `engine::RebuildState()`, does not clear held keys, does not update `s_state.phys[]`, and does not update `s_state.logical[]`.

The repair function is:

```text
engine::RebuildState()
```

called from:

```text
input_capture::IsTargetActive()
```

inside the target focus regained branch.

## State Repaired

`engine::RebuildState()`:

- reads hardware truth via `GetAsyncKeyState()` for Space/W/S/A/D/Shift/Ctrl/C;
- writes `s_state.spacePhys`;
- writes `s_state.phys[W/S/A/D]`;
- writes system modifier state;
- calls `ReconcileLogicalStateFromPhysical()`;
- calls `bhop::ForceSpaceSync(s_state.spacePhys)`;
- cancels stale timers for keys whose physical state is false;
- releases logical keys that should no longer be down;
- resolves X/Y axes from physical truth.

## Broken State Before Repair

The debug log shows the broken operational state as routing inactive:

```text
Key DOWN ... route=0
```

That means:

- Counter-Strafe semantic routing was disabled.
- BHOP semantic routing was disabled.
- Physical tracking still occurred.
- Hook was not dead.
- Timer subsystem was not dead.

## Verdict

Profile/config changes correlate with recovery, but the source code does not support `rcfg::Apply()` as the repair. The exact repair is focus recognition followed by `engine::RebuildState()`.

