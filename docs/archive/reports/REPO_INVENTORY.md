# Repository Inventory

Date: 2026-05-30
Scope: root-level files present before cleanup moves in this pass.

## Source

- `.clang-format` - formatter configuration.
- `.editorconfig` - editor configuration.
- `.gitattributes` - git attributes configuration.
- `.gitignore` - ignore rules.
- `CMakeLists.txt` - CMake build definition.
- `Makefile` - MinGW build definition.
- `parse_trace.py` - trace parsing utility.
- `tools/archive/patch_capture.py` - archived one-off patch/debug utility.
- `test_run.ps1` - local test runner script.

## Documentation

- `README.md`
- `LICENSE`
- `CODE_OF_CONDUCT.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `AXIS_AUDIT_REPORT.md`
- `BUILD_MODE_AUDIT_REPORT.md`
- `CLEANUP_REPORT.md`
- `FILE_AUDIT_REPORT_B.md`
- `FILE_AUDIT_REPORT_D.md`
- `FILE_AUDIT_REPORT_E.md`
- `FINAL_FORENSIC_VERDICT.md`
- `FINAL_STABILIZATION_VERDICT.md`
- `FORENSIC_INFRA_AUDIT.md`
- `FORENSIC_INVENTORY.md`
- `FORENSIC_REWORK_PLAN.md`
- `HARDCODE_AUDIT.md`
- `HOTKEY_AUDIT_REPORT.md`
- `LOG_LOCATION_REPORT.md`
- `PERFORMANCE_AUDIT_REPORT.md`
- `REGRESSION_REPORT.md`
- `ROLLBACK_INTEGRITY_REPORT.md`
- `ROLLBACK_TARGET_REPORT.md`
- `STRESS_BHOP_REPORT.md`
- `STRESS_CHAOS_REPORT.md`
- `STRESS_COUNTERSTRAFE_REPORT.md`
- `STRESS_FOCUS_REPORT.md`
- `STRESS_PROFILE_REPORT.md`
- `THREAD_AUDIT_REPORT.md`

## Generated Artifact

- `build_output.log` - captured compiler command output.
- `fire_stability_hist.png` - generated stability histogram image.
- `FORENSIC_DUMP_setup.txt` - generated/placeholder forensic dump setup marker.

## Temporary Debug Output

- `dequeued.txt` - extracted trace sequence data.
- `dispatched.txt` - extracted trace sequence data.
- `enqueued.txt` - extracted trace sequence data.
- `forensic_output.txt` - forensic parser/audit output.
- `raw.txt` - extracted trace sequence data.
- `trace_output.txt` - captured runtime trace output.
- `trace_output_utf8.txt` - captured runtime trace output.

## Runtime Output

- `marco_debug.log` - runtime debug log.
- `marco_debug_utf8.log` - runtime debug log conversion/copy.

## Test Artifact

- None at repository root before cleanup, except `test_run.ps1` classified as source/test utility because it is an executable script rather than generated output.

## Unknown

- None identified from root file names and sampled content.

## Notes

- Directories were not classified in this inventory because the task requested root files.
- Existing dirty worktree changes outside this cleanup pass were not reverted.
