# Dead Code Audit

Date: 2026-05-30
Branch: v27-stabilization
Scope: repository sanitation only. Gameplay, input routing, state engine, timer behavior, focus behavior, Counter-Strafe, BHOP, and runtime config behavior were treated as out of scope for removal.

## Method

- Searched the repository with `rg --files`.
- Searched references for known stale symbols and old hotkey paths.
- Searched unused-looking build flags and config constants.
- Inspected source, tests, scripts, and current reports.

Note: `rg` reported access denied for several files under `tests/forensics/` during one broad search. Those files were still visible in directory inventory and are classified in `TEST_ROT_AUDIT.md`. This audit does not claim exhaustive static reachability.

## Findings

| Item | Location | Evidence | Risk | Recommendation |
| --- | --- | --- | --- | --- |
| One-off capture patcher | `patch_capture.py`, archived to `tools/archive/patch_capture.py` | Only repository references were documentation inventory entries. Script rewrites old F-key hook blocks with regex and is not invoked by `Makefile`, `CMakeLists.txt`, scripts, tests, or source. | Low runtime risk. It is not part of build or runtime. | Archived. Keep out of root. Delete later after one stabilization cycle if no engineer needs it. |
| Stale F8 hotkey state tracking | `src/core/input_capture.cpp` `s_hkDownF8` | Active source only initializes, clears, and syncs F8 state. No active `VK_F8` handler remains in source. Historical docs still mention emergency F8 capture. | High because file is input/focus-adjacent and current task forbids input/focus changes. | Do not remove in this pass. Review with hotkey ownership separately. |
| Timer message constant after direct callback migration | `include/core/types.h` `WM_TIMER_EXPIRED`; comments in `include/core/timing.h` | No source handler or poster was found. Comments still describe `WM_TIMER_EXPIRED`, while timer architecture now uses direct callbacks. | High because timer behavior is explicitly out of scope. Public shared type removal can also create integration churn. | Document only. Update comments or remove constant in a dedicated timer documentation cleanup after runtime evidence. |
| Legacy static config constants with no observed references | `include/core/config.h` values: `TAP_DELAY_MS`, `SPRAY_DELAY_MS`, `BURST_THRESHOLD`, `SPACE_DELAY_MS`, `DEBUG_MODE`, `CLICK_HISTORY_MAX`, `CLICK_HISTORY_WINDOW_MS`, `QUICK_TAP_MS`, `NOISE_MIN`, `NOISE_MAX` | Search found declarations only for these names. Some nearby constants remain active through runtime config getters or physics defaults. | Medium to high because runtime config behavior is forbidden in this pass and constants may define compatibility defaults. | Do not delete. Move to config ownership review. |
| Unused build flags | `include/ui/build_config.h`: `MARCO_ENABLE_LOGGING`, `MARCO_ENABLE_ASSERTS`, `MARCO_ENABLE_UI_OVERLAY` | Search found definitions but no consumers outside docs. `debug_logger` is gated by `MARCO_ENABLE_FORENSIC`, not `MARCO_ENABLE_LOGGING`. | Medium because build matrix semantics may rely on flags as documented contract even if not consumed. | Do not delete in this pass. Resolve in build config cleanup with release owner. |
| Commented-out ETW provider definitions | `src/core/etw_controller.cpp` | Legacy provider comments are not compiled. | Low. | Safe future removal, but not required for this pass. |
| Maintenance rewrite scripts living under tests | `tests/forensics/fix_includes.py`, `tests/forensics/restructure.py` | Names and behavior indicate repository rewrite helpers, not runnable tests. No invocation from build/test scripts was found. | Low runtime risk; moderate repository history value. | Archive or move to `tools/maintenance/` after test owner review. |
| Duplicate local executable fixture | `tests/forensics/cs2.exe` | Earlier inventory found it duplicates `dummy_cs2.exe` by hash and `*.exe` is ignored. No source references were found. | Low runtime risk; possible local manual test dependency. | Delete or move to artifact storage after forensics owner confirms. Not removed in this pass. |

## Delete Recommendations

Safe action executed:

- Archived `patch_capture.py` to `tools/archive/patch_capture.py`.

Document-only candidates:

- `s_hkDownF8` and `VK_F8` historical path: do not touch without input/focus review.
- `WM_TIMER_EXPIRED`: do not touch without timer architecture review.
- `include/core/config.h` unused constants: do not touch without runtime config review.
- `MARCO_ENABLE_*` unused flags: do not touch without build matrix review.
- `tests/forensics/*.exe` duplicate fixture: remove only after forensics owner confirms it is not used manually.

