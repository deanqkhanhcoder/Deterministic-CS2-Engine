# Infrastructure Refactor Report

Date: 2026-05-30
Branch: v27-stabilization
Scope: infrastructure, repository organization, path governance, logging/crash/artifact architecture.

Gameplay validation: NOT EXECUTED.
Counter-Strafe behavior changes: NOT EXECUTED.
BHOP behavior changes: NOT EXECUTED.

## Phase Status

- Phase 1 Project Structure Audit: EXECUTED
- Phase 2 Single Source Of Truth: EXECUTED
- Phase 3 Logging Architecture: EXECUTED
- Phase 4 Artifact Architecture: EXECUTED
- Phase 5 Crash Architecture: EXECUTED
- Phase 6 Report Architecture: EXECUTED
- Phase 7 Path Governance: EXECUTED
- Phase 8 Observability Governance: EXECUTED
- Phase 9 Implementation: EXECUTED
- Phase 10 Verification: EXECUTED

## Documents Created

- `docs/reports/PROJECT_STRUCTURE_AUDIT.md`
- `docs/reports/LOGGING_ARCHITECTURE.md`
- `docs/reports/PATH_GOVERNANCE_REPORT.md`
- `docs/reports/OBSERVABILITY_MAP.md`
- `docs/reports/INFRASTRUCTURE_REFACTOR_REPORT.md`
- `docs/REPORT_INDEX.md`

## Files Moved

- `REPO_INVENTORY.md` -> `docs/reports/REPO_INVENTORY.md`
- `RELEASE_CLEANUP_REPORT.md` -> `docs/reports/RELEASE_CLEANUP_REPORT.md`

Previous cleanup pass moves preserved:

- Root forensic/audit reports remain under `docs/forensics/`.
- Root generated artifacts remain under `runtime/artifacts/`.
- Legacy runtime logs remain under `runtime/logs/`.

## Files Deleted

None.

## Paths Changed

Path service:

- Added `workspace::GetRuntimeRootW/A()`.
- Added `workspace::GetArtifactRootW/A()`.
- Added `workspace::GetCaptureRootW/A()`.
- Added `workspace::GetCrashRootW/A()`.
- Added `workspace::EnsureRuntimeDirectoriesExist()`.
- Added `workspace::EnsureArtifactDirectoryExists()`.
- Added `workspace::EnsureCaptureDirectoryExists()`.
- Added `workspace::EnsureCrashDirectoryExists()`.
- Changed `workspace::GetLogRootW/A()` from `<project-root>\logs\` to `<project-root>\runtime\logs\`.

Runtime producers:

- Debug logger: `runtime/logs/marco_debug.log`.
- Forensic session logs: `runtime/logs/marco_YYYY-MM-DD_HH-MM-SS.log`.
- Startup crash marker: `runtime/crash/startup_crash.log`.
- UI watchdog log: `runtime/logs/ui_watchdog.log`.
- UI freeze screenshot: `runtime/captures/freeze_capture.bmp`.
- UI freeze dump: `runtime/crash/ui_freeze.dmp`.
- ETW trace: `runtime/artifacts/performance_trace.etl`.
- Stress script cleanup target: `runtime/logs/marco_debug.log`.
- Forensic Python generated graphs: `runtime/artifacts/*.png`.

## Folders Created

- `docs/reports/`
- `runtime/crash/`

## Folders Removed

None.

## Folders Merged

- Root reports were merged into `docs/reports/`.
- Generated graph outputs from forensic scripts were redirected into `runtime/artifacts/`.
- Crash-specific outputs were split from logs into `runtime/crash/`.

## `.gitignore` Changes

Added:

- `runtime/crash/`

Already present and preserved:

- `build/`
- `build/obj/`
- `runtime/logs/`
- `runtime/captures/`
- `runtime/artifacts/`
- `runtime/bin/`
- `*.log`
- `*.csv`
- `*.png`
- `*.o`
- `*.obj`
- `*.pdb`
- `*.ilk`

## Build Results

- `make debug`: EXECUTED, exit code 0.
- `make profile`: EXECUTED, exit code 0.
- `make release`: EXECUTED, exit code 0.

## Check Results

- `git diff --check`: EXECUTED, exit code 0.
- Warning: line-ending normalization warnings were printed for existing mixed CRLF/LF state.
- `git status --short --branch`: EXECUTED.
- `rg "FORENSIC_CAPTURE" include src`: EXECUTED, no source matches.

## Current Answer Map

- Logs: `runtime/logs/`
- Crash dumps and crash logs: `runtime/crash/`
- Forensic captures/logs: `runtime/logs/marco_YYYY-MM-DD_HH-MM-SS.log`
- Reports: `docs/reports/` and indexed by `docs/REPORT_INDEX.md`
- Forensic/audit history: `docs/forensics/`
- Generated artifacts: `runtime/artifacts/`
- Runtime screenshots/captures: `runtime/captures/`
- Executables: `runtime/bin/`
- Build intermediates: `build/`

## Remaining Technical Debt

- Worktree still contains pre-existing modified runtime source files and pre-existing `scratch/` deletions from earlier passes.
- Historical reports in `docs/forensics/` still mention old paths in historical context; current architecture reports supersede them.
- UI/user export paths are intentionally user-selected and not forced into runtime directories.
- CLI tools still accept caller-provided output paths by design.
- Runtime launch verification was NOT EXECUTED; only compile/link verification was performed.

## Release Readiness Notes

Infrastructure architecture is now documented and centralized enough for engineers to locate logs, crash outputs, forensic logs, reports, and generated artifacts quickly.

This report does not claim gameplay readiness.
