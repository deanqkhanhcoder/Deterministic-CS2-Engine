# ROLLBACK TARGET REPORT

## Selected Target
- **Commit Hash:** `3acc01162c06247a55bdc3bec5191298e722c8c1` (V26 Profile Build Restoration + Analysis Recovery)
- **Base Architecture Commit:** `35032858c2e8cf329279615e3151fec25f97f397` (V26 Production - Open Source Release)

## Justification
Git history analysis reveals that the `FireState` and 1-tick delay stabilization architecture was initially introduced in commit `19041d9` (`V26.1 Stable Release - Deterministic Competitive Architecture`). 

The last commit cleanly preceding this experimental phase is `3acc011`. This commit represents the last state of the engine where:
- BHOP, Axis, Hotkeys, and Focus gating functioned correctly in production.
- The `FireState` enum and associated asynchronous firing dispatch logic did not exist.
- Counter-strafe logic triggered shooting immediately without timer-driven scheduling.

## Features Present at Target
- V26 Production Architecture (State Engine, Config IO, Topologies).
- Synchronous counter-strafe to firing pipeline.
- Fully functional forensic telemetry suite (ETW, UI dashboards).
- Working UI overlay and hotkey processing.

## Known Bugs/Characteristics at Target
- The hotkeys did not yet have the self-healing timestamp logic (introduced later during troubleshooting). We will need to re-apply the timestamp-based hotkey self-healing fix to ensure 100% hotkey stability.
- Some minor telemetry reporting quirks existed, but they did not impact gameplay stability.
- "1-tick delay" competitive advantages are absent (by design of this rollback).

## Next Steps
The rollback will be performed by branch creation, not a hard reset. This preserves the historical context while allowing us to branch off from `3acc011` into a new `v27-stabilization` branch.
