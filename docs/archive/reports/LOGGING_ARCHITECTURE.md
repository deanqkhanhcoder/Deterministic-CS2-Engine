# Logging Architecture

Date: 2026-05-30

## Policy

All application-owned runtime logs go under:

```text
runtime/logs/
```

Crash-specific logs and dumps go under:

```text
runtime/crash/
```

Generated traces and analysis outputs go under:

```text
runtime/artifacts/
```

Captures that are not crash dumps go under:

```text
runtime/captures/
```

No module should derive a runtime filesystem location directly. Runtime paths must originate from `workspace`.

## Producers

| Producer | Current Path | Current Trigger | Current Lifetime | Current Format |
|---|---|---|---|---|
| Debug logger (`dlog`) | `workspace::GetLogRootA() + "marco_debug.log"` -> `runtime/logs/marco_debug.log` | `dlog::Init()` and `DLOG_*` calls | Process lifetime | Plain text lines |
| Forensic logger | `workspace::GetLogRootA() + "marco_YYYY-MM-DD_HH-MM-SS.log"` -> `runtime/logs/` | Startup, auto flush, focus/profile/shutdown/crash flush | Process lifetime | Session header + anomaly event lines |
| UI watchdog log | `workspace::GetLogRootA() + "ui_watchdog.log"` -> `runtime/logs/ui_watchdog.log` | UI stall watchdog | Stall events | Plain text block |
| UI freeze screenshot | `workspace::GetCaptureRootA() + "freeze_capture.bmp"` -> `runtime/captures/freeze_capture.bmp` | UI stall watchdog | Last stall capture | BMP |
| UI freeze dump | `workspace::GetCrashRootA() + "ui_freeze.dmp"` -> `runtime/crash/ui_freeze.dmp` | UI stall watchdog | Last stall dump | Minidump |
| Startup/severe crash marker | `workspace::GetCrashRootA() + "startup_crash.log"` -> `runtime/crash/startup_crash.log` | Vectored exception handler | Crash event | Plain text |
| ETW kernel trace | `workspace::GetArtifactRootW() + "performance_trace.etl"` -> `runtime/artifacts/performance_trace.etl` | ETW diagnostics start/stop | ETW session | ETL |
| Analysis Toolkit exports | Caller-provided path | UI/export caller | Explicit export | CSV/JSON/baseline |
| Standalone deterministic simulator | CLI-provided path | Tool invocation | Explicit run | CSV |

## Unified Names

Recommended stable names:

- `runtime/logs/marco_debug.log`
- `runtime/logs/marco_YYYY-MM-DD_HH-MM-SS.log`
- `runtime/logs/ui_watchdog.log`
- `runtime/artifacts/performance_trace.etl`
- `runtime/captures/freeze_capture.bmp`
- `runtime/crash/startup_crash.log`
- `runtime/crash/ui_freeze.dmp`

## Verification Evidence

- Source search for `FORENSIC_CAPTURE` in `include` and `src`: no matches.
- `workspace::GetLogRoot*()` now resolves through `GetRuntimeRoot*()`.
- Crash marker uses `workspace::GetCrashRootA()`.

## Status

EXECUTED: producer audit.
EXECUTED: path policy definition.
EXECUTED: path centralization changes.
