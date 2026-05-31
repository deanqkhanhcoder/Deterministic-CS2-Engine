# V27.4 Hardening Report

Date: 2026-05-30
Branch observed: `v27-stabilization`
Scope: infrastructure, observability, telemetry correctness, path governance.

This report is the single output artifact for the hardening pass. `FILESYSTEM_WRITER_AUDIT.md` was not created separately because the final task instruction required exactly one output report. The filesystem writer audit is included below.

Runtime gameplay validation after this pass: NOT EXECUTED.

## 1. What Changed

Hardening-scoped source changes:

| File | Change | Behavior class |
| --- | --- | --- |
| `include/core/workspace.h` | Added canonical runtime root accessors for `runtime/`, `runtime/bin/`, `runtime/logs/`, `runtime/artifacts/`, `runtime/captures/`, and `runtime/crash/`. | Path governance |
| `src/core/workspace.cpp` | Fixed project-root discovery so an executable under `runtime/bin/` resolves the project root, not `runtime/`. Added marker-based upward search and explicit `runtime/bin` fallback. | Path governance |
| `src/core/config_io.cpp` | Moved `marco.ini` path construction through `workspace::GetBinRootW()` and `workspace::EnsureBinDirectoryExists()`. | Path governance |
| `tests/test_stress.cpp` | Moved generated stress report output to `workspace::GetArtifactRootA()`. | Artifact governance |
| `src/core/bhop.cpp` | Reworked `BHOP_STALL` telemetry to be per-sequence progress based. The detector now resets at sequence start, runs after the focus-abort check, and uses a threshold derived from the active BHOP config with a 2000 ms floor. | Telemetry only |
| `include/core/input_capture.h` | Updated `GetActiveWindowFast()` contract comment to match current behavior. | Documentation in header |
| `src/core/input_capture.cpp` | Centralized foreground sampling in `SampleForegroundWindow()`. `PollTarget()`, `IsTargetActive()`, `IsTargetActiveForUI()`, and `GetActiveWindowFast()` now refresh the shared foreground cache from `GetForegroundWindow()`. Removed runtime F8 state remnants from the source path. | Focus observability and stale-cache reduction |
| `src/ui/ui_main.cpp` | Removed stale `F8:Exit` text from the status bar. | UI accuracy |

Runtime artifact cleanup executed:

- Existing files from `runtime/runtime/logs/` were moved into `runtime/logs/`.
- Empty nested `runtime/runtime/` directories were removed.
- Final observed runtime directories are only `runtime/artifacts/`, `runtime/bin/`, `runtime/crash/`, and `runtime/logs/`.

## 2. What Was Intentionally Not Changed

- Counter-Strafe gameplay logic was not intentionally changed.
- BHOP gameplay timing, mode selection, jump dispatch, and injection behavior were not intentionally changed. Only `BHOP_STALL` telemetry semantics were changed.
- `tools/forensics/deterministic_simulator.cpp` still accepts an explicit caller-provided output path. This is a tool export path, not automatic runtime output.
- `src/core/analysis_toolkit.cpp` still writes to caller-provided export paths. These are explicit export destinations, not autonomous runtime paths.
- Historical docs were not rewritten to erase old evidence that mentioned `runtime/runtime`, `FORENSIC_CAPTURE.log`, or F8.
- `tools/archive/patch_capture.py` was not deleted. It is archived historical tooling and still contains F8 patch strings, but it is not runtime source and is not built.
- No runtime launch or gameplay soak was performed after the builds. Live log creation from the newly built binaries is therefore INSUFFICIENT EVIDENCE until a runtime smoke test is executed.

## 3. Path Drift Status

Path drift root cause:

- `workspace::GetProjectRootW()` could treat `...\runtime\bin\marco.exe` as if `...\runtime\` were the project root.
- Downstream roots then became `...\runtime\runtime\logs\`, `...\runtime\runtime\crash\`, etc.

Fix:

- `workspace::GetProjectRootW()` now first walks upward looking for project markers: `PROJECT_BRAIN.md`, or `Makefile` plus `src/`.
- If markers are unavailable, it explicitly recognizes `\runtime\bin\` and returns the parent project directory.
- Runtime roots now derive from a single project root:
  - `workspace::GetRuntimeRoot*()`
  - `workspace::GetBinRoot*()`
  - `workspace::GetLogRoot*()`
  - `workspace::GetArtifactRoot*()`
  - `workspace::GetCaptureRoot*()`
  - `workspace::GetCrashRoot*()`

Executed checks:

| Check | Result |
| --- | --- |
| `Test-Path runtime/runtime` | `False` |
| `Test-Path logs` | `False` |
| `Test-Path FORENSIC_CAPTURE.log` | `False` |
| Recursive `FORENSIC_CAPTURE.log` search | No files found |
| `rg "FORENSIC_CAPTURE|runtime/runtime|runtime\\runtime" src include tools tests scripts` | No matches |

Status: source-level path drift fixed and existing nested runtime logs migrated. Post-build live runtime path generation is NOT EXECUTED.

## 4. Filesystem Writer Audit

Search executed:

```powershell
rg -n "\b(fopen|ofstream|CreateFile[A-Za-z]*|CreateDirectory[A-Za-z]*|std::filesystem::create_director(?:y|ies)|filesystem::create_director(?:y|ies))\b" src include tools tests scripts
```

Findings after patch:

| Producer | Current path source | Action |
| --- | --- | --- |
| `src/core/debug_logger.cpp` `fopen` | `workspace::GetLogRootA()` | Keep |
| `src/core/telemetry.cpp` default forensic `fopen` | `workspace::GetLogRootA()` via default path | Keep |
| `src/core/telemetry.cpp` explicit `InitForensics(path)` `fopen` | Caller-provided path, currently supplied by startup using workspace log root | Keep with debt note |
| `src/core/main.cpp` crash `fopen` | `workspace::GetCrashRootA()` | Keep |
| `src/ui/ui_diagnostics.cpp` watchdog log `fopen` | `workspace::GetLogRootA()` | Keep |
| `src/ui/ui_diagnostics.cpp` screenshot `fopen` | Caller passes `workspace::GetCaptureRootA()` path | Keep |
| `src/ui/ui_diagnostics.cpp` minidump `CreateFileA` | `workspace::GetCrashRootA()` | Keep |
| `src/core/workspace.cpp` `CreateDirectoryW` | Internal implementation of workspace path service | Keep |
| `tests/test_stress.cpp` `ofstream` | Changed to `workspace::GetArtifactRootA()` | Fixed |
| `tools/forensics/deterministic_simulator.cpp` `ofstream(outPath)` | Explicit tool output argument | Keep |
| `src/core/analysis_toolkit.cpp` `ofstream(filePath)` | Explicit export/caller path | Keep |

No autonomous runtime writer outside the workspace path service was found in `src/`, `include/`, `tools/`, `tests/`, or `scripts/` after the patch. Future misuse remains possible through APIs that accept explicit paths.

## 5. BHOP_STALL Status

Pre-patch telemetry issue:

- The detector could report `BHOP_STALL` using stale cross-sequence timing.
- The check could run before the focus abort path, allowing focus-related aborts to appear as stalls.
- A fixed threshold did not account for current BHOP config timing.

Patch:

- Removed the cross-sequence `lastInjectionTick` style dependency.
- Added per-sequence `lastProgressTick`, initialized at sequence start.
- Moved focus-change handling before stall detection.
- Counted progress only when the BHOP state machine actually advances: jump dispatch, successful airborne wait, and landing scan transition.
- Added `StallThresholdTicks(const RuntimeConfig&)`, derived from current config timing plus margin with a 2000 ms minimum.

Status: telemetry false-positive debt addressed at source level. Runtime evidence from a live session is NOT EXECUTED, so final detector quality remains INSUFFICIENT EVIDENCE until logs from gameplay confirm useful signal.

## 6. F8 Remnants

Runtime source search:

```powershell
rg -n "s_hkDownF8|VK_F8|F8:" src include
```

Result: no matches.

Remaining historical references:

- `tools/archive/patch_capture.py` contains old F8 patch strings.
- Historical docs under `docs/forensics/` and `docs/reports/` still mention F8.

Status: no active runtime-source F8 remnants found in `src/` or `include/`. Historical/archive references remain by design.

## 7. Focus Architecture Current State

Authoritative live focus sample:

- `GetForegroundWindow()` sampled through `capture::SampleForegroundWindow()`.

Shared cache:

- `s_activeHwnd` remains as a cache and fallback only.
- `SampleForegroundWindow()` updates `s_activeHwnd` whenever `GetForegroundWindow()` returns a handle.
- `WinEventProc()` still publishes foreground events into `s_activeHwnd` and triggers focus reconciliation.

Current readers:

- `PollTarget()` samples foreground through `SampleForegroundWindow()`.
- `IsTargetActive()` samples foreground through `SampleForegroundWindow()`.
- `IsTargetActiveForUI()` samples foreground through `SampleForegroundWindow()`.
- `GetActiveWindowFast()` now samples foreground through `SampleForegroundWindow()` instead of returning a stale cache-only value.
- `bhop.cpp` and `target_platform.cpp` call `GetActiveWindowFast()` and therefore no longer read a cache-only focus value through that API.

Status: focus reads are simplified around live foreground sampling. `s_activeHwnd` is not removed because it remains useful as a fallback when `GetForegroundWindow()` returns null and as the publication point for `WinEventProc()`.

## 8. Final Runtime Output Map

Canonical runtime outputs:

| Output class | Path |
| --- | --- |
| Binaries | `runtime/bin/` |
| Runtime config | `runtime/bin/marco.ini` |
| Logs | `runtime/logs/` |
| Forensic logs | `runtime/logs/marco_YYYY-MM-DD_HH-MM-SS.log` |
| Debug log | `runtime/logs/marco_debug.log` |
| Crash logs and dumps | `runtime/crash/` |
| Captures | `runtime/captures/` |
| Generated artifacts | `runtime/artifacts/` |

Observed runtime tree after cleanup:

```text
runtime/
  artifacts/
  bin/
  crash/
  logs/
```

Observed files in `runtime/logs/` after migration:

- `marco_2026-05-30_13-13-15.log`
- `marco_2026-05-30_13-50-53.log`
- `marco_debug.log`

Forbidden paths observed after cleanup:

| Path | Observed |
| --- | --- |
| `runtime/runtime/` | No |
| root `logs/` | No |
| root `FORENSIC_CAPTURE.log` | No |

## 9. Verification

Builds executed after source changes:

| Command | Result |
| --- | --- |
| `make debug` | exit code 0, linked `runtime/bin/marco_debug.exe` |
| `make profile` | exit code 0, linked `runtime/bin/marco_profile.exe` |
| `make release` | exit code 0, linked `runtime/bin/marco.exe` |

Repository checks:

| Command | Result |
| --- | --- |
| `git diff --check` | exit code 0; line-ending warnings only |
| `git status --short --branch` | executed; working tree contains many pre-existing modifications, deletions, and untracked docs/tools |

Important status note:

- The working tree was already dirty. This pass did not revert or normalize unrelated files.
- Because no runtime smoke test was launched after the build, runtime creation of fresh logs from the patched binaries is NOT EXECUTED.

## 10. Remaining Technical Debt

- `telemetry::InitForensics(std::string path)` can still accept any caller-provided path. Current startup uses workspace paths, but future callers could bypass policy unless the API is narrowed.
- Historical docs still contain old F8 and legacy path references. They are evidence records, not runtime code.
- `tools/archive/patch_capture.py` still contains F8 patch text. It is archived, but it can confuse future text searches.
- Existing source files contain mojibake comment text from earlier history. This was not changed because it is cosmetic and broad.
- Live runtime validation is still required to prove fresh output creation under `runtime/logs/`, `runtime/crash/`, `runtime/captures/`, and `runtime/artifacts/`.

## 11. Release Recommendation

Recommendation: proceed to runtime smoke and soak validation before any release claim.

Evidence supports the source-level hardening changes and successful Debug/Profile/Release builds. It does not support a `PASS`, `FIXED`, or `PRODUCTION READY` claim because post-build live runtime logging and gameplay validation were NOT EXECUTED.
