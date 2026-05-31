# Technical Debt Elimination Report

Date: 2026-05-30
Branch: v27-stabilization
Scope: repository sanitation, report hygiene, path governance, telemetry sanity, and config drift documentation.

## Execution Summary

This pass did not modify gameplay behavior, input routing, state engine behavior, timer behavior, focus behavior, Counter-Strafe behavior, BHOP behavior, or runtime config behavior.

Reports generated:

- `docs/reports/DEAD_CODE_AUDIT.md`
- `docs/reports/LEGACY_INFRA_AUDIT.md`
- `docs/reports/PATH_BYPASS_AUDIT.md`
- `docs/reports/TELEMETRY_SANITY_AUDIT.md`
- `docs/reports/REPORT_ROT_AUDIT.md`
- `docs/reports/TEST_ROT_AUDIT.md`
- `docs/reports/CONFIG_DRIFT_AUDIT.md`

Index updated:

- `docs/REPORT_INDEX.md`

## Removed Items

None deleted.

No source code, gameplay code, timer code, input code, focus code, runtime config code, or telemetry behavior was removed.

## Archived Items

| Item | New Location | Reason |
| --- | --- | --- |
| `patch_capture.py` | `tools/archive/patch_capture.py` | One-off regex patch helper for old input capture hotkey code. No build, test, source, or script invocation found. Archived instead of deleted for reversibility. |

## Files Moved

| From | To |
| --- | --- |
| `patch_capture.py` | `tools/archive/patch_capture.py` |

## Reports Updated

| File | Change |
| --- | --- |
| `docs/REPORT_INDEX.md` | Added new sanitation reports to the canonical index. |
| `docs/reports/REPO_INVENTORY.md` | Updated `patch_capture.py` location. |
| `docs/reports/RELEASE_CLEANUP_REPORT.md` | Added note that `patch_capture.py` was archived after the earlier cleanup pass. |

## Remaining Technical Debt

| Item | Risk | Status |
| --- | --- | --- |
| `src/core/input_capture.cpp` still tracks `s_hkDownF8` while no active F8 handler was found | High because it is input/focus-adjacent | Documented only. NOT REMOVED. |
| `include/core/types.h` still defines `WM_TIMER_EXPIRED`; timing comments still mention message posting | High because timer behavior is out of scope | Documented only. NOT REMOVED. |
| `include/core/config.h` contains declaration-only legacy constants | Medium/high because runtime config behavior is out of scope | Documented only. NOT REMOVED. |
| `include/ui/build_config.h` defines unused `MARCO_ENABLE_LOGGING`, `MARCO_ENABLE_ASSERTS`, `MARCO_ENABLE_UI_OVERLAY` | Medium because build contracts may be misleading | Documented only. NOT REMOVED. |
| `tests/forensics/fix_includes.py` and `tests/forensics/restructure.py` are maintenance scripts under tests | Low runtime risk, unclear ownership | Documented only. NOT MOVED. |
| `tests/forensics/cs2.exe` appears to duplicate `dummy_cs2.exe` | Low runtime risk, possible manual fixture use | Documented only. NOT DELETED. |
| Historical reports include superseded statements and old F8 references | Low runtime risk | Documented in report rot audit. NOT DELETED. |

## Verification

| Command | Result | Evidence |
| --- | --- | --- |
| `make debug` | EXECUTED, exit 0 | Output: `make: Nothing to be done for 'debug'.` |
| `make profile` | EXECUTED, exit 0 | Output: `make: Nothing to be done for 'profile'.` |
| `make release` | EXECUTED, exit 0 | Output: `make: Nothing to be done for 'release'.` |
| `git diff --check` | EXECUTED, exit 0 after elevated read access | First sandbox run failed to hash `tests/forensics/chaos_simulator.py` with permission denied. Elevated rerun completed with line-ending warnings only. |
| `git status --short --branch` | EXECUTED | Working tree remains dirty with the existing infrastructure/report refactor diff plus this pass's report additions and `patch_capture.py` archival. |

## Not Executed

| Item | Reason |
| --- | --- |
| Gameplay reproduction | Out of scope. |
| Runtime gameplay validation | Out of scope. |
| In-game Counter-Strafe validation | Out of scope. |
| In-game BHOP validation | Out of scope. |
| Deletion of uncertain code/config/test items | Evidence was insufficient or the area was explicitly out of scope. |

## Risks

- This pass is an audit and sanitation pass, not a runtime behavior validation pass.
- Build targets were up to date, so the evidence is command success, not a full clean rebuild.
- Some broad `rg` searches hit access denied under `tests/forensics/` in the sandbox. `git diff --check` required elevated read access to complete.
- Existing source modifications from prior infrastructure work remain in the working tree and were not reverted.

## Future Cleanup Candidates

1. Decide whether `MARCO_ENABLE_LOGGING` should control `DebugLogger`, or remove the flag.
2. Retire or document `MARCO_ENABLE_ASSERTS` and `MARCO_ENABLE_UI_OVERLAY`.
3. Review `s_hkDownF8` and historical F8 documentation with the input/focus owner.
4. Review `WM_TIMER_EXPIRED` and stale timing comments with the timer owner.
5. Move maintenance rewrite scripts out of `tests/forensics/` after owner confirmation.
6. Delete duplicate ignored executable fixtures after forensics owner confirmation.
7. Archive superseded historical reports into a dated archive folder after release owner approval.

## Conclusion

The only safe removal-class action performed was archival of an unreferenced root patch helper. All uncertain or behavior-adjacent findings were documented and left intact.

