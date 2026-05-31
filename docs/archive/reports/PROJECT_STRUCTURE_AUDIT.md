# Project Structure Audit

Date: 2026-05-30
Scope: repository organization, runtime folders, report folders, forensic folders, tests, generated artifacts, and build outputs.

## Canonical Structure

```text
project-root/
  src/
  include/
  docs/
    forensics/
    reports/
  tests/
  tools/
  scripts/
  runtime/
    bin/
    logs/
    artifacts/
    captures/
    crash/
  build/
```

## Directory Inventory

| Directory | Purpose | Owner | Runtime or Source | Decision |
|---|---|---|---|---|
| `.agent/` | Local agent metadata | Tooling | Runtime/local | Keep ignored |
| `.git/` | Git metadata | Git | Source control | Keep |
| `build/` | Object files and intermediate build output | Build system | Build output | Keep ignored |
| `build/obj/` | Per-config object files | Build system | Build output | Keep ignored |
| `docs/` | Stable project documentation | Engineering | Source | Keep |
| `docs/forensics/` | Forensic/audit/investigation history | Engineering/forensics | Source docs | Keep |
| `docs/reports/` | Release, structure, governance, cleanup reports | Release engineering | Source docs | Keep |
| `include/` | Public/internal C++ headers | C++ engineering | Source | Keep |
| `runtime/` | Local runtime output root | Application/tooling | Runtime output | Keep ignored children |
| `runtime/bin/` | Built executables and local runtime config | Build/runtime | Build/runtime output | Keep ignored |
| `runtime/logs/` | Runtime logs, forensic logs, debug logs, UI watchdog logs | Observability | Runtime output | Keep ignored |
| `runtime/artifacts/` | Generated traces, ETW files, CSVs, graphs, temporary analysis outputs | Tooling/forensics | Runtime output | Keep ignored |
| `runtime/captures/` | Screenshots and non-crash captures | Diagnostics | Runtime output | Keep ignored |
| `runtime/crash/` | Crash logs, dumps, crash snapshots | Crash diagnostics | Runtime output | Keep ignored |
| `scripts/` | Build/test/automation scripts | Release engineering | Source | Keep |
| `src/` | C++ implementation | C++ engineering | Source | Keep |
| `tests/` | Unit, stress, and forensic test assets | Test engineering | Source/test | Keep |
| `tests/forensics/` | Forensic simulators and validators | Test/forensics | Source/test | Keep, with artifact outputs redirected |
| `tools/` | Standalone tools | Tooling | Source | Keep |

## Root File Policy

Root keeps only repository entrypoints and policy files:

- build definitions
- top-level README/license/security/contribution docs
- source utility scripts explicitly intended as root commands
- no generated logs
- no runtime captures
- no reports other than stable top-level project docs

Current root report files were moved to `docs/reports/`.

## Findings

- Source and runtime output are now separated by the `runtime/` root.
- Report output is split between `docs/forensics/` and `docs/reports/`.
- Build output is isolated under `build/` and `runtime/bin/`.
- Remaining generated output risk is primarily from ad hoc scripts; those reviewed in this pass now write graphs to `runtime/artifacts/`.

## Status

EXECUTED: directory audit.
EXECUTED: canonical structure definition.
EXECUTED: local creation of `runtime/crash/`.
