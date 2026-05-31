# FINAL RED TEAM CERTIFICATION - V27.4

Date: 2026-05-30

Verdict:

## 2. ROOT CAUSE LIKELY BUT NOT CERTIFIED

This pass does not certify `stale focus cache` as the only root cause.

It remains the strongest explanation for the original random Counter-Strafe/BHOP death because it explains all three observed properties:

1. Counter-Strafe and BHOP die together.
2. Hook events continue to arrive.
3. Profile/focus activity restores routing through rebuild/reconciliation.

However, source review still found non-refuted paths that can produce transient inactive routing or confusing disabled states without proving the old stale-cache mechanism is the only possible cause.

## Evidence Read

Documents:

- `PROJECT_BRAIN.md`
- `docs/forensics/*`
- `docs/reports/*`

Runtime logs:

- `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`
- `runtime/runtime/logs/marco_2026-05-30_13-50-53.log`
- `runtime/runtime/logs/marco_debug.log`

Source:

- `src/core/input_capture.cpp`
- `src/core/input_router.cpp`
- `src/core/state_engine.cpp`
- `src/core/state_reconciliation.cpp`
- `src/core/counterstrafe_controller.cpp`
- `src/core/timer_lifecycle.cpp`
- `src/core/timing.cpp`
- `src/core/bhop.cpp`
- `src/core/runtime_config.cpp`
- `src/core/target_platform.cpp`
- `src/core/injection.cpp`
- `include/core/telemetry.h`

## Why This Is Not A PASS

The current patch is not disproven by available runtime evidence, but certification requires more than "not reproduced yet."

Blocking reasons for high-confidence certification:

- Current logs still contain `route=0` events. Sampled contexts show focus loss, not active gameplay failure, but they prove route=0 remains a normal reachable state.
- `runtime/runtime/logs/` is the actual log location in this run, while canonical policy says `runtime/logs/`. Observability path drift is still present.
- `BHOP_STALL` continues to appear in the newer log, and source shows the detector can false-positive because `lastInjectionTick` is not reset per sequence.
- Source still has a transient active-publication lag path: foreground may already be the game, but `target_platform::GetCurrentIdentity()` may not yet be published, so `IsTargetActive()` returns false until resolver publication catches up.
- Cached foreground readers remain in BHOP and target resolver through `GetActiveWindowFast()`.
- No controlled 6-hour patched soak log was produced in this pass.

## Source Proof Questions

### A. Can `route=0` still occur when the game is really active?

YES, as a source-plausible transient.

Call stack:

```text
KeyboardProc()
-> IsTargetActive()
-> GetForegroundWindow() returns game HWND
-> target_platform::GetCurrentIdentity() is stale/null
-> isActive=false
-> ResolveTargetAsync(currentId)
-> routeThis=false
-> HandleKeyDown/HandleKeyUp(routeSemantic=false)
```

Source references:

- Foreground sample and publication comparison: `src/core/input_capture.cpp:87-112`
- WASD route decision: `src/core/input_capture.cpp:324-357`
- BHOP route decision: `src/core/input_capture.cpp:370-383`
- Async resolver queue: `src/core/target_platform.cpp:140-166`
- Publication worker: `src/core/target_platform.cpp:168-260`

Runtime proof that this transient still causes active gameplay failure after the patch: INSUFFICIENT EVIDENCE.

If foreground is sampled correctly, active publication is already correct, caps are present, and suspend is false, source review did not find another route-0 path for CS2/Roblox WASD routing.

### B. Can `phys != logical` still persist forever?

No permanent forever path was proven. Temporary persistence is source-proven.

Temporary paths:

- `HandleKeyDown(k, false)` sets `phys=true` and returns before logical routing (`src/core/input_router.cpp:45-54`).
- `ClearHeldKeys()` clears logical/timers but does not overwrite physical truth (`src/core/state_reconciliation.cpp:39-48`, `src/core/state_reconciliation.cpp:135-152`).
- Synthetic Counter-Strafe intentionally creates `phys=false logical=true` until timer release (`src/core/counterstrafe_controller.cpp:278-326`, `src/core/timer_lifecycle.cpp:40-52`).

Recovery paths:

- Routed key-up/key-down sequence.
- `engine::RebuildState()` and `ReconcileLogicalStateFromPhysical()` (`src/core/state_reconciliation.cpp:50-132`).
- Timer expiration (`src/core/timer_lifecycle.cpp:17-75`).

Permanent stuck proof after the patch: NOT FOUND.

### C. Can Counter-Strafe disable without focus logic?

YES, but current runtime evidence does not identify these as the old random death.

Source paths:

- Suspend disables WASD routing: `shouldRoute = isActive && !isSuspended` (`src/core/input_capture.cpp:324-334`).
- Missing capabilities disable routing: `CAP_CSTRAFE` check (`src/core/input_capture.cpp:327-334`), capabilities return `CAP_NONE` if no active profile (`src/core/target_platform.cpp:282-285`).
- `AutoCounterStrafe()` intentionally aborts on opposite physical key, axis conflict, or tap too short (`src/core/counterstrafe_controller.cpp:278-306`).
- `SendInput` failure can prevent effective injection, but current debug log has zero `SendInput FAILED` lines (`src/core/injection.cpp:41-80`).

Current logs have zero `COUNTERSTRAFE_CANCELLED`, zero `COUNTERSTRAFE_CONFLICT`, and zero `TIMER_REJECTED`, so these are not supported as the observed root cause.

### D. Can BHOP disable without focus logic?

YES.

Source paths:

- `rcfg::Get().bhopEnabled` must be true for worker wake and input routing (`src/core/input_capture.cpp:376-378`, `src/core/bhop.cpp:285-287`).
- Capability check requires `CAP_BHOP` (`src/core/input_capture.cpp:376-378`).
- `s_waitingForSpaceRepress` blocks worker wake and loop continuation (`src/core/bhop.cpp:285-287`, `src/core/bhop.cpp:431-468`).
- BHOP worker aborts when cached active HWND changes or mismatches current target identity (`src/core/bhop.cpp:321-336`).
- `SendInput` failure would be logged by injection wrappers (`src/core/injection.cpp:41-80`).

Current runtime evidence does not prove these as the original shared Counter-Strafe/BHOP failure. The shared focus/route path remains stronger.

## Hypothesis Ranking

Full matrix: `docs/reports/HYPOTHESIS_MATRIX.md`.

Top explanations after red-team review:

1. Stale focus/publication path: strongest explanation, not refuted.
2. Async resolver publication lag: source-proven transient route-0 risk.
3. Cached foreground readers outside patched active checks: remaining BHOP/resolver risk.
4. Logical/physical divergence: useful symptom, weak independent root cause.
5. Timer subsystem: weak; current logs have zero `TIMER_REJECTED`.
6. Hook failure: weak; debug logs show hook events continuing.
7. Profile reload side effects: weak as direct cause; `rcfg::Apply()` does not rebuild input state.
8. BHOP_STALL: weak as root cause because detector is noisy/source-suspect.

## Certification Decision

Selected option: `2. ROOT CAUSE LIKELY BUT NOT CERTIFIED`.

Reason:

- The patch targets the best-supported source path.
- Available gameplay report says the old bug has not reproduced after the patch.
- Available logs do not prove an alternate root cause.
- Red-team source review still found unclosed route/publication/cache risks.
- The current runtime evidence is not a controlled long-session certification run.

## Evidence Required For Future Certification

To move from option 2 to option 3, collect a patched runtime session that shows:

- Long-session gameplay with Counter-Strafe and BHOP active.
- No user-visible Counter-Strafe/BHOP death.
- `route=0` only when foreground is provably non-target.
- No `SendInput FAILED`.
- No `TIMER_REJECTED` clusters.
- BHOP stall events either absent or proven unrelated to user-visible BHOP behavior.
- Logs written to the canonical `runtime/logs/` path or a documented resolved path.

Until then, do not claim PASS, FIXED, or PRODUCTION READY.
