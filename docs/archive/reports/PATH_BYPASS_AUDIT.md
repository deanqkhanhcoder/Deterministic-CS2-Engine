# Path Bypass Audit

Date: 2026-05-30
Policy: runtime outputs should originate from the `workspace::` path service. Modules should not independently decide log, crash, capture, or artifact locations.

## Search Scope

Searched source, headers, tools, scripts, and tests for:

- `fopen`
- `fopen_s`
- `std::ofstream`
- `ofstream`
- `CreateFile`
- `CreateDirectory`

Note: one broad search reported access denied for several `tests/forensics/*.py` files. The C++ runtime producers were still found and audited.

## Runtime Producers

| Producer | API | Current Path Source | Trigger | Lifetime | Format | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Debug logger | `fopen` in `src/core/debug_logger.cpp` | `workspace::GetLogRootA() / "marco_debug.log"` | Debug logger init | Process lifetime | text | Compliant |
| Forensic log | `fopen` in `src/core/telemetry.cpp` | `workspace::GetLogRootA() / "forensic.log"` via `InitForensics` | telemetry init, periodic flush, shutdown | Process lifetime | text | Compliant |
| Crash marker | `fopen` in `src/core/main.cpp` | `workspace::GetCrashRootA() / "startup_crash.log"` | startup crash path | per crash | text | Compliant |
| UI freeze screenshot | `fopen` in `src/ui/ui_diagnostics.cpp` | caller path under `workspace::GetCaptureRootA()` | watchdog diagnostics | per capture | bmp | Compliant |
| UI freeze log | `fopen` in `src/ui/ui_diagnostics.cpp` | `workspace::GetLogRootA() / "ui_freeze_diagnostics.log"` | watchdog diagnostics | append | text | Compliant |
| UI minidump | `CreateFileA` in `src/ui/ui_diagnostics.cpp` | `workspace::GetCrashRootA() / "ui_freeze_dump.dmp"` | watchdog diagnostics | per dump | dmp | Compliant |
| Workspace service | `CreateDirectoryW` in `src/core/workspace.cpp` | central project-root derived runtime paths | startup/path ensure | process | directories | Intended central authority |

## Non-Runtime Or Caller-Owned Writes

| Producer | Location | Path Source | Classification |
| --- | --- | --- | --- |
| Analysis toolkit export | `src/core/analysis_toolkit.cpp` | caller-provided `filePath` | Caller-owned export, not a runtime log path. Should be documented if exposed in UI. |
| Deterministic simulator output | `tools/forensics/deterministic_simulator.cpp` | CLI-provided output path | Tool artifact. Acceptable if caller writes under `runtime/artifacts/`. |
| Stress harness output | `tests/test_stress.cpp` | CLI-provided output path | Test artifact. Acceptable if test command writes under `runtime/artifacts/`. |

## Bypasses

No confirmed runtime log, crash, capture, or artifact producer was found writing to root or to an ambiguous current working directory path after the previous infrastructure refactor.

Remaining governance issue:

- Tool and test writers still accept arbitrary CLI or caller-provided paths. That is acceptable for tools, but CI commands should standardize on `runtime/artifacts/`.

