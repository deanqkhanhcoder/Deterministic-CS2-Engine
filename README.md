# Marco Engine

Marco is a Windows C++20 input-processing engine for FPS games, focused on Counter-Strike 2. It captures hardware input, tracks physical and logical state, routes semantic movement intent, runs Counter-Strafe and BHOP controllers, schedules precise timer callbacks, and injects output through `SendInput`.

The project is currently in V27.4 stabilization. Recent work focused on focus-state correctness, state reconciliation, runtime observability, forensic logging, path governance, and repository organization.

## Current Status

Version: `v27.4.0-stable`

Status: Stabilization Candidate

Highlights:

- Fixed Focus Desync / stale focus cache failure mode.
- Unified runtime log, crash, capture, artifact, and binary folder architecture.
- Added automatic forensic infrastructure.
- Added `PROJECT_BRAIN.md` as the mandatory first-read knowledge base.
- Added path governance through `workspace::` runtime path helpers.

Known State:

- Stable in recent gameplay sessions.
- Long-session certification is still in progress.
- Not a production release claim.
- Long-term 3-7 day runtime evidence is still required before release certification.

## Architecture Overview

Primary runtime flow:

```text
Hardware Input
  -> Keyboard/Mouse Hook
  -> Physical State
  -> Semantic Router
  -> Logical State
  -> Axis Resolution
  -> Counter-Strafe / BHOP
  -> Timer System
  -> SendInput
```

Core modules:

- `Input Capture`: low-level Windows keyboard/mouse hooks, focus tracking, physical state capture.
- `Input Router`: routes physical key events into semantic engine events when the target game is active.
- `State Engine`: owns logical movement state, snapshots, suspend behavior, reconciliation, and watchdog integration.
- `Counter-Strafe`: computes and schedules neutralizing movement key taps.
- `BHOP`: manages space input state, jump dispatch, and BHOP worker lifecycle.
- `Timer Engine`: handles precise delayed callbacks outside the Windows message queue path.
- `Runtime Config`: manages profile and settings updates.
- `Telemetry / Forensics`: records anomaly-focused runtime evidence.
- `Workspace Path Service`: centralizes runtime output paths under `runtime/`.

## Counter-Strafe

Counter-Strafe is the movement correction subsystem. It observes the resolved movement axis state and schedules short opposing input when a release or direction transition needs braking.

Current stabilization notes:

- Do not change Counter-Strafe gameplay behavior without runtime evidence.
- Focus/state desync was the dominant recent failure mode, not a proven timer failure.
- Profile switching recovered the bug because it forced a state rebuild and reconciliation path.

## BHOP

BHOP is the space-input subsystem. It tracks physical Space state, owns synthetic Space release safety, and runs a worker loop for configured BHOP behavior.

Current stabilization notes:

- Do not change BHOP gameplay behavior without runtime evidence.
- Recent hardening changed only `BHOP_STALL` telemetry semantics, not BHOP timing or jump behavior.
- BHOP failures observed during V27.4 investigation were consistent with focus/routing desync, not a proven BHOP timing defect.

## Runtime Folders

Runtime output must stay out of repository root. Canonical paths:

```text
runtime/
  bin/        compiled executables and marco.ini
  logs/       marco_debug.log and marco_YYYY-MM-DD_HH-MM-SS.log
  crash/      crash logs and dumps
  captures/   screenshots and runtime captures
  artifacts/  generated reports, stress outputs, graphs, traces
```

Forbidden runtime output locations:

- root `logs/`
- root `FORENSIC_CAPTURE.log`
- `runtime/runtime/`
- build outputs committed as source files

All runtime paths should originate from the `workspace::` path service.

## Project Brain

`PROJECT_BRAIN.md` is mandatory reading before any future AI or engineer changes the repository.

It contains:

- project purpose and version history;
- high-level architecture;
- directory and file maps;
- subsystem ownership;
- focus, profile, Counter-Strafe, BHOP, timer, and forensic flows;
- known risks and current investigations;
- debugging playbooks;
- rules for future AI agents.

Start here:

```text
PROJECT_BRAIN.md
docs/REPORT_INDEX.md
```

## Reports And Forensics

Reports live under:

```text
docs/reports/
```

Forensic investigations live under:

```text
docs/forensics/
```

Do not place new investigation reports in repository root. Add or update an index entry when adding durable documentation.

## Build

Prerequisites:

- Windows 10/11 x64
- MinGW-w64 with C++20 support
- GNU Make

Build commands:

```powershell
make debug
make profile
make release
```

Expected binaries:

```text
runtime/bin/marco_debug.exe
runtime/bin/marco_profile.exe
runtime/bin/marco.exe
```

## Development Rules

- Evidence first; no speculative gameplay patches.
- Do not change Counter-Strafe, BHOP, timing, input routing, focus, or runtime config behavior as part of documentation or infrastructure cleanup.
- Do not claim `PASS`, `FIXED`, `STABLE`, `PRODUCTION READY`, or release certification without execution evidence.
- Mock stress tests are not gameplay proof.
- Runtime output belongs under `runtime/`.
- Repository documentation is part of the source of truth.

## License

This project is licensed under the MIT License. See `LICENSE`.

## Disclaimer

This software processes and injects input. Use responsibly and understand the rules and risks of any target environment before running it.
