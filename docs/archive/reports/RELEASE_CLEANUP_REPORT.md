# Release Cleanup Report

Date: 2026-05-30
Branch: v27-stabilization
Scope: repository cleanup and release preparation only.

## Execution Summary

- Phase 1 Inventory: EXECUTED
- Phase 2 Root Cleanup: EXECUTED
- Phase 3 Generated File Cleanup: EXECUTED
- Phase 4 Build Output Cleanup: EXECUTED
- Phase 5 Forensics Sanity: EXECUTED
- Phase 6 Test Asset Review: EXECUTED
- Phase 7 Release Sanity Check: EXECUTED

Runtime gameplay testing: NOT EXECUTED.
Counter-Strafe/BHOP bug validation: NOT EXECUTED.

## Files Moved

Documentation moved to `docs/forensics/`:

- `AXIS_AUDIT_REPORT.md` -> `docs/forensics/AXIS_AUDIT_REPORT.md`
- `BUILD_MODE_AUDIT_REPORT.md` -> `docs/forensics/BUILD_MODE_AUDIT_REPORT.md`
- `CLEANUP_REPORT.md` -> `docs/forensics/CLEANUP_REPORT.md`
- `FILE_AUDIT_REPORT_B.md` -> `docs/forensics/FILE_AUDIT_REPORT_B.md`
- `FILE_AUDIT_REPORT_D.md` -> `docs/forensics/FILE_AUDIT_REPORT_D.md`
- `FILE_AUDIT_REPORT_E.md` -> `docs/forensics/FILE_AUDIT_REPORT_E.md`
- `FINAL_FORENSIC_VERDICT.md` -> `docs/forensics/FINAL_FORENSIC_VERDICT.md`
- `FINAL_STABILIZATION_VERDICT.md` -> `docs/forensics/FINAL_STABILIZATION_VERDICT.md`
- `FORENSIC_INFRA_AUDIT.md` -> `docs/forensics/FORENSIC_INFRA_AUDIT.md`
- `FORENSIC_INVENTORY.md` -> `docs/forensics/FORENSIC_INVENTORY.md`
- `FORENSIC_REWORK_PLAN.md` -> `docs/forensics/FORENSIC_REWORK_PLAN.md`
- `HARDCODE_AUDIT.md` -> `docs/forensics/HARDCODE_AUDIT.md`
- `HOTKEY_AUDIT_REPORT.md` -> `docs/forensics/HOTKEY_AUDIT_REPORT.md`
- `LOG_LOCATION_REPORT.md` -> `docs/forensics/LOG_LOCATION_REPORT.md`
- `PERFORMANCE_AUDIT_REPORT.md` -> `docs/forensics/PERFORMANCE_AUDIT_REPORT.md`
- `REGRESSION_REPORT.md` -> `docs/forensics/REGRESSION_REPORT.md`
- `ROLLBACK_INTEGRITY_REPORT.md` -> `docs/forensics/ROLLBACK_INTEGRITY_REPORT.md`
- `ROLLBACK_TARGET_REPORT.md` -> `docs/forensics/ROLLBACK_TARGET_REPORT.md`
- `STRESS_BHOP_REPORT.md` -> `docs/forensics/STRESS_BHOP_REPORT.md`
- `STRESS_CHAOS_REPORT.md` -> `docs/forensics/STRESS_CHAOS_REPORT.md`
- `STRESS_COUNTERSTRAFE_REPORT.md` -> `docs/forensics/STRESS_COUNTERSTRAFE_REPORT.md`
- `STRESS_FOCUS_REPORT.md` -> `docs/forensics/STRESS_FOCUS_REPORT.md`
- `STRESS_PROFILE_REPORT.md` -> `docs/forensics/STRESS_PROFILE_REPORT.md`
- `THREAD_AUDIT_REPORT.md` -> `docs/forensics/THREAD_AUDIT_REPORT.md`

Generated/debug outputs moved to `runtime/artifacts/`:

- `build_output.log` -> `runtime/artifacts/build_output.log`
- `dequeued.txt` -> `runtime/artifacts/dequeued.txt`
- `dispatched.txt` -> `runtime/artifacts/dispatched.txt`
- `enqueued.txt` -> `runtime/artifacts/enqueued.txt`
- `fire_stability_hist.png` -> `runtime/artifacts/fire_stability_hist.png`
- `FORENSIC_DUMP_setup.txt` -> `runtime/artifacts/FORENSIC_DUMP_setup.txt`
- `forensic_output.txt` -> `runtime/artifacts/forensic_output.txt`
- `raw.txt` -> `runtime/artifacts/raw.txt`
- `trace_output.txt` -> `runtime/artifacts/trace_output.txt`
- `trace_output_utf8.txt` -> `runtime/artifacts/trace_output_utf8.txt`

Runtime logs moved to `runtime/logs/`:

- `marco_debug.log` -> `runtime/logs/marco_debug.log`
- `marco_debug_utf8.log` -> `runtime/logs/marco_debug_utf8.log`

## Files Deleted

None.

Deletion justification: not applicable. No automatic deletion was performed.

## Files Preserved

- Root project files preserved at the time of that cleanup: `.clang-format`, `.editorconfig`, `.gitattributes`, `.gitignore`, `CMakeLists.txt`, `Makefile`, `README.md`, `LICENSE`, `CODE_OF_CONDUCT.md`, `CONTRIBUTING.md`, `SECURITY.md`, `parse_trace.py`, `patch_capture.py`, `test_run.ps1`.
- Current note: `patch_capture.py` was later archived to `tools/archive/patch_capture.py` during the technical debt elimination pass.
- Generated artifacts were preserved locally under `runtime/artifacts/`.
- Runtime logs were preserved locally under `runtime/logs/`.
- Forensic/audit documentation content was preserved under `docs/forensics/`.

## Generated Artifact Evidence

- `build_output.log`: first lines are compiler invocations; generated build capture.
- `forensic_output.txt`: begins with parsed event/audit summary; generated forensic parser output.
- `raw.txt`, `dequeued.txt`, `dispatched.txt`, `enqueued.txt`: numeric sequence extracts; generated trace slices.
- `trace_output.txt`, `trace_output_utf8.txt`: runtime trace/debug output.
- `FORENSIC_DUMP_setup.txt`: placeholder forensic dump marker.
- `fire_stability_hist.png`: generated histogram image artifact.
- `marco_debug.log`, `marco_debug_utf8.log`: runtime debug logs.

## Updated `.gitignore` Entries

Already present before this pass:

- `build/`
- `*.o`
- `*.obj`
- `*.pdb`
- `*.ilk`
- `*.log`

Added in this pass:

- `build/obj/`
- `runtime/bin/`

Build output tracking evidence:

- `git ls-files build build/obj runtime/logs runtime/artifacts` returned no tracked files.

## Forensics Sanity

Forensic write path evidence:

- `src/core/telemetry.cpp` builds the default session name as `marco_YYYY-MM-DD_HH-MM-SS.log`.
- The path is `workspace::GetLogRootA() + name`.
- `workspace::GetLogRootA()` resolves to `<project-root>\runtime\logs\` by source inspection.
- Runtime debug log path is `workspace::GetLogRootA() + "marco_debug.log"`.

Search evidence:

- `rg "FORENSIC_CAPTURE" include src` returned no matches.
- No relative fallback to `FORENSIC_CAPTURE.log` remains in source.

Current runtime log directory:

- `runtime/logs/marco_debug.log`
- `runtime/logs/marco_debug_utf8.log`

Note: this report predates the infrastructure refactor that canonicalized runtime logs under `runtime/logs/`.

## Test Asset Review

- `tests/forensics/autofire_restore_validator.py`: KEEP. Small Python validator.
- `tests/forensics/chaos_simulator.py`: KEEP. Forensic simulator.
- `tests/forensics/cs2.exe`: DELETE recommendation only. Untracked binary, identical SHA256 to `dummy_cs2.exe`, risky real-game-like name.
- `tests/forensics/dummy_cs2.exe`: MOVE recommendation only. Keep as a test fixture if needed, but move to a fixture path such as `tests/fixtures/dummy_cs2.exe`.
- `tests/forensics/fix_includes.py`: MOVE recommendation only. Maintenance rewrite utility, not a forensic test.
- `tests/forensics/forensic_sweep.py`: KEEP. Forensic sweep script.
- `tests/forensics/mutex_contention_graph.py`: KEEP. Forensic visualization script.
- `tests/forensics/mutex_contention_profiler.py`: KEEP. Forensic profiler script.
- `tests/forensics/quantization_stability_heatmap.py`: KEEP. Forensic heatmap script.
- `tests/forensics/queue_race_simulator.py`: KEEP. Queue race simulator.
- `tests/forensics/replay_drift_detector.py`: KEEP. Replay drift detector.
- `tests/forensics/replay_drift_heatmap.py`: KEEP. Replay drift visualization.
- `tests/forensics/restructure.py`: MOVE recommendation only. Repository restructuring utility, not a test.
- `tests/forensics/sendinput_interleave_simulator.py`: KEEP. SendInput interleave simulator.
- `tests/forensics/sendinput_interleave_visualizer.py`: KEEP. SendInput visualization script.
- `tests/forensics/simulator.py`: KEEP. General forensic simulator.
- `tests/forensics/stale_timer_fuzzer.py`: KEEP. Timer fuzzer.

Binary evidence:

- `cs2.exe` SHA256: `6313AC8D3CCEA1BDF2EB9C3C8978B4787FBCDB4DAB15BE3B1FA0D1C623E19EB4`
- `dummy_cs2.exe` SHA256: `6313AC8D3CCEA1BDF2EB9C3C8978B4787FBCDB4DAB15BE3B1FA0D1C623E19EB4`

No test asset was deleted or moved in Phase 6.

## Build Results

- `make debug`: EXECUTED, exit code 0.
- `make profile`: EXECUTED, exit code 0.
- `make release`: EXECUTED, exit code 0.

The first run after cleanup rebuilt touched objects. The final command outputs completed successfully.

## Diff And Status Results

- `git diff --check`: EXECUTED, exit code 0.
- `git status --short --branch`: EXECUTED.

Warnings:

- `git diff --check` printed line-ending normalization warnings for existing CRLF/LF state.
- `git status` still shows a dirty worktree with many pre-existing modified runtime files and pre-existing `scratch/` deletions outside this cleanup task.
- `runtime/bin/`, `runtime/artifacts/`, and `runtime/logs/` are ignored, so build/runtime/generated outputs are preserved locally but not staged by default.

## Risks

- The repository is structurally cleaner, but not fully release-clean until unrelated pre-existing source modifications are reviewed.
- Ignored artifact moves mean generated outputs are removed from root/tracking, but their new copies are local ignored files.
- Test fixture binaries are still present under `tests/forensics/`; recommendations were documented only.
- No gameplay runtime validation was performed.

## Recommended Next Actions

1. Review pre-existing dirty runtime source changes separately from this cleanup pass.
2. Decide whether ignored generated artifacts should be kept locally only or archived externally.
3. Move/delete recommended test fixtures in a separate test-assets pass.
4. Run a clean clone release build after staging the intended cleanup changes.

## Scores

REPOSITORY CLEANLINESS SCORE: 78/100

Evidence: root now contains only project source/config/policy files plus the requested inventory and release report; forensic/audit docs were moved; generated/debug outputs were removed from root. Score is limited by pre-existing dirty source files, ignored local artifacts, and test fixture binaries.

RELEASE READINESS SCORE: 70/100

Evidence: Debug/Profile/Release builds executed successfully and `git diff --check` returned 0. Score is limited by dirty worktree state, unreviewed pre-existing runtime changes, unexecuted runtime/gameplay validation, and unresolved test asset recommendations.
