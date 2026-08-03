# HYPOTHESIS MATRIX - V27.4 RED TEAM PASS

Date: 2026-05-30

Scope: adversarial review of the current V27.4 focus-cache patch. This report does not try to prove the patch correct. It ranks every still-relevant hypothesis found in `PROJECT_BRAIN.md`, `docs/forensics/`, `docs/reports/`, current source, and available runtime logs.

Runtime evidence read:

- `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`
- `runtime/runtime/logs/marco_2026-05-30_13-50-53.log`
- `runtime/runtime/logs/marco_debug.log`

Important evidence limitation: canonical policy says logs belong under `runtime/logs/`, but this workspace currently has logs under `runtime/runtime/logs/`. The log path mismatch is infrastructure evidence, not gameplay evidence.

## Event Inventory

| Log | LOGICAL_PHYSICAL_DIVERGENCE | BHOP_STALL | FOCUS_LOST | FOCUS_GAINED | PROFILE_CHANGED | TIMER_REJECTED | COUNTERSTRAFE_* | BHOP_ABORTED |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `runtime/runtime/logs/marco_2026-05-30_13-13-15.log` | 123 | 1 | 14 | 14 | 10 | 0 | 0 | 0 |
| `runtime/runtime/logs/marco_2026-05-30_13-50-53.log` | 2525 | 69 | 48 | 48 | 18 | 0 | 0 | 0 |

Post-patch debug log counters from `runtime/runtime/logs/marco_debug.log`:

- `route=0`: 74
- `route=1`: 5572
- `Target focus LOST`: 24
- `Target focus REGAINED`: 24
- `SendInput FAILED`: 0

Observed `route=0` contexts sampled in the debug log occur after `Target focus LOST` lines and before later target resolution/focus regain. That does not prove active-gameplay route loss after the patch, but it also does not certify that every route-0 path is impossible.

## Ranked Matrix

| Hypothesis | Confidence | Evidence | Refuted? |
|---|---:|---|---|
| Stale foreground focus cache / active-window publication mismatch | 64% | Pre-patch debug evidence showed hook events continuing with `route=0`; source shows `KeyboardProc` routes through `IsTargetActive()` and disables both WASD and BHOP when inactive (`src/core/input_capture.cpp:324-377`). Patch re-samples `GetForegroundWindow()` in `IsTargetActive()` and `IsTargetActiveForUI()` (`src/core/input_capture.cpp:57-112`). | Not refuted. Strongest known explanation, but not certified as the only root cause. |
| Async target resolver publication lag | 18% | Even after re-sampling foreground, `IsTargetActive()` compares the sampled foreground HWND to `target_platform::GetCurrentIdentity()` (`src/core/input_capture.cpp:99-112`). If the foreground window is a valid game target but the resolver has not published it yet, the first hook event can still compute inactive and route `0`; recovery depends on `ResolveTargetAsync()` and later hook/focus processing (`src/core/target_platform.cpp:140-166`, `src/core/target_platform.cpp:168-260`). | Not refuted. Source-proven transient path. Runtime proof for active-gameplay impact is INSUFFICIENT EVIDENCE. |
| Logical/physical divergence as independent root cause | 14% | Divergence events are produced whenever `s_state.phys[i] != s_state.logical[i]` (`src/core/state_engine.cpp:68-79`). `phys=0 logical=1` is expected during synthetic Counter-Strafe brake windows; `phys=1 logical=0` is possible after inactive routing or cleanup. | Mostly refuted as sole root cause. Still useful as symptom telemetry. |
| Focus oscillation / overlay/settings focus theft | 12% | Debug logs show repeated `Target focus LOST` and `Target focus REGAINED`, often with non-target PIDs/HWNDs. Source intentionally clears held keys on focus loss and rebuilds on regain (`src/core/input_capture.cpp:119-161`). | Not refuted as a trigger. Weaker than stale publication because focus loss alone should recover. |
| Profile reload side effects | 8% | `PROFILE_CHANGED` is emitted by `rcfg::Apply()` (`src/core/runtime_config.cpp:235-240`). Source does not call `engine::RebuildState()` in `rcfg::Apply()`. Recovery observed around profile changes is more likely due adjacent focus regain/rebuild than config apply itself. | Mostly refuted as direct repair path. |
| BHOP stall detector as root cause | 7% | `BHOP_STALL` is emitted in `BhopThreadFunc()` when `lastInjectionTick` is older than 2 seconds while `s_spaceHeld` remains true (`src/core/bhop.cpp:278-319`). `lastInjectionTick` is not reset at new sequence start, so false-positive stall events are source-plausible. | Refuted as direct proof. Not refuted as noise/telemetry debt. |
| Timer race / stale timer callback | 5% | Timer lifecycle rejects stale callbacks via `TIMER_REJECTED` (`src/core/timer_lifecycle.cpp:30-37`). Current logs contain zero `TIMER_REJECTED`; debug log shows many direct `OnTimerExpired` calls and `SendInput` releases. | Mostly refuted by current evidence. |
| Timer slot leak / active timer accumulation | 5% | Forensic counters in the later log end around `created=1420 executed=1235 cancelled=159 derived_active=26`. This is not proof of a leak because active synthetic brake timers can exist transiently, but long-session bounds are not certified. | Not proven; low confidence. |
| Hook timeout or hook failure | 5% | During old failure windows, debug logs still recorded hook key events, which argues against total hook failure. Current debug log has thousands of route=1 key events. | Mostly refuted for observed failure class. |
| UI/message dispatch stall | 5% | Debug log shows `DispatchMessage stalled` messages around 100-400ms, but timer callbacks no longer depend on the UI message queue and current timer anomaly evidence is absent. | Not root for current Counter-Strafe/BHOP death; keep as UI risk. |
| Runtime config corruption | 4% | Runtime config uses a seqlock-style publication and clamps values in `rcfg::Apply()` (`src/core/runtime_config.cpp:23-69`, `src/core/runtime_config.cpp:71-241`). No log evidence of corrupt values. | Mostly refuted by available evidence. |
| Axis conflict persistence | 4% | Conflict telemetry exists (`src/core/state_engine.cpp:83-103`) and `AutoCounterStrafe()` aborts on conflict (`src/core/counterstrafe_controller.cpp:288-296`). Current logs have no `COUNTERSTRAFE_CONFLICT`. | Mostly refuted for current observed failure. |
| Counter-Strafe cancellation by intended conditions | 4% | `AutoCounterStrafe()` can abort on opposite physical key, conflict, or too-short tap (`src/core/counterstrafe_controller.cpp:278-306`). Current logs show no `COUNTERSTRAFE_CANCELLED`. | Possible normal behavior, not evidence for random death. |
| BHOP waiting-for-repress state | 4% | `ToggleEnabled()` and `OnSuspendChanged()` can set `s_waitingForSpaceRepress`, blocking the BHOP worker predicate until a fresh Space press (`src/core/bhop.cpp:431-468`). | Not refuted as a confusing user-visible state; no current runtime proof. |
| Injected key rejection / `SendInput` failure | 3% | `SendInput` return is checked and logs `SendInput FAILED` (`src/core/injection.cpp:41-80`). Current debug log has zero failures. | Mostly refuted by current log. |
| Injected event filtering bug | 3% | Hook ignores injected events by `LLKHF_INJECTED`/`LLMHF_INJECTED` and `dwExtraInfo=0x1337BEEF` (`src/core/input_capture.cpp:425-430`, keyboard path same pattern). No evidence of bad filtering. | Low confidence; not proven. |
| NKRO keyboard / lost hardware keyup | 3% | Always-track physical layer reduces damage, and `RebuildState()` samples `GetAsyncKeyState()` (`src/core/state_reconciliation.cpp:83-132`). No device-specific evidence exists. | INSUFFICIENT EVIDENCE. |
| WinEventProc ordering bug | 10% | `WinEventProc()` stores `s_activeHwnd` and calls `IsTargetActive()` (`src/core/input_capture.cpp:464-473`). Patch reduces dependence on this cache for active checks, but BHOP worker and resolver still read cached `GetActiveWindowFast()` (`src/core/input_capture.cpp:540-541`, `src/core/bhop.cpp:260-264`, `src/core/bhop.cpp:321-336`, `src/core/target_platform.cpp:185-187`). | Not fully refuted. Remaining cached-reader risk. |
| Rebuild-state bug | 4% | `RebuildState()` samples hardware and calls `ReconcileLogicalStateFromPhysical()` (`src/core/state_reconciliation.cpp:83-132`). Debug logs show rebuild following focus regain. No source proof of incorrect rebuild for WASD/Space was found. | Mostly refuted. |
| Capability/profile mismatch | 6% | CS2 and Roblox profiles both include `CAP_BHOP | CAP_CSTRAFE | CAP_SCROLL` (`src/core/target_platform.cpp:17-29`). If active publication is null/stale, `GetActiveCapabilities()` returns `CAP_NONE` (`src/core/target_platform.cpp:282-285`), which disables routing. | Same family as resolver/focus publication. Not fully refuted. |
| Suspend/safe-mode state | 3% | `shouldRoute = isActive && !isSuspended` for WASD (`src/core/input_capture.cpp:324-334`). No current log evidence that suspend caused the random death. | Mostly refuted for observed class. |
| Overlay interference | 5% | Focus transitions to non-target HWNDs/PIDs are visible. No specific overlay process evidence was mapped from logs. | INSUFFICIENT EVIDENCE. |
| Workspace/path issue hiding logs | 100% infra, 0% gameplay | Runtime header reports `LogPath: ...\runtime\runtime\logs\...`, while canonical policy is `runtime/logs/`. | Proven infrastructure drift; not a gameplay root cause. |

## Red-Team Summary

The stale focus/publication path remains the leading explanation because it uniquely explains shared Counter-Strafe and BHOP route loss with hook activity still present. It is not certified as the only root cause because source still contains transient route-0 paths tied to asynchronous target publication and cached foreground readers outside the patched active-check functions.

Certification status: ROOT CAUSE LIKELY BUT NOT CERTIFIED.
