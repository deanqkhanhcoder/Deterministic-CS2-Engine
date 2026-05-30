# Path Governance Report

Date: 2026-05-30

## Rule

Runtime filesystem locations are owned by `workspace`.

Modules may request:

- project root
- runtime root
- log root
- artifact root
- capture root
- crash root

Modules must not invent their own runtime root or write generated files into repository root/source directories.

## Path Service

Authoritative API:

- `workspace::GetProjectRootW/A()`
- `workspace::GetRuntimeRootW/A()`
- `workspace::GetLogRootW/A()`
- `workspace::GetArtifactRootW/A()`
- `workspace::GetCaptureRootW/A()`
- `workspace::GetCrashRootW/A()`
- `workspace::EnsureRuntimeDirectoriesExist()`
- `workspace::EnsureLogDirectoryExists()`
- `workspace::EnsureArtifactDirectoryExists()`
- `workspace::EnsureCaptureDirectoryExists()`
- `workspace::EnsureCrashDirectoryExists()`

## Current Runtime Roots

```text
runtime/logs/
runtime/artifacts/
runtime/captures/
runtime/crash/
runtime/bin/
```

## Findings And Actions

| Finding | Action |
|---|---|
| `GetLogRoot*()` resolved to `<project-root>\logs\` | Changed to `<project-root>\runtime\logs\` |
| Severe crash marker used log root | Changed to crash root |
| UI watchdog screenshot/dump used log root | Split screenshot to capture root and dump to crash root |
| ETW trace used log root | Changed to artifact root |
| Stress script deleted root `marco_debug.log` | Changed to `runtime/logs/marco_debug.log` |
| Forensic Python scripts wrote graphs into `scratch/` | Changed graph outputs to `runtime/artifacts/` |
| Quantization heatmap wrote generated PNG into test source directory | Changed output to `runtime/artifacts/` |

## Remaining Allowed Cases

- UI export dialogs allow user-selected paths. These are explicit user exports, not implicit runtime output.
- CLI tools accept output paths from command-line arguments. These are explicit invocation contracts.
- Build definitions still output executables to `runtime/bin/`; this is canonical and ignored.

## Search Evidence

Searches performed:

- `rg` over `src`, `include`, `scripts`, `tools`, and `tests` for `fopen`, `ofstream`, `CreateFile`, `CreateDirectory`, `runtime`, `logs`, `artifacts`, `captures`, and known file extensions.
- `rg "FORENSIC_CAPTURE" include src` returned no source matches.

## Status

EXECUTED: hardcoded path search.
EXECUTED: path service expansion.
EXECUTED: path cleanup for runtime producers and generated graphs.
