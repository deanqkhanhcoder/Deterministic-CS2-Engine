# Legacy Infrastructure Audit

Date: 2026-05-30
Scope: infrastructure and repository sanitation only.

## Search Terms

Searched for:

- `legacy`
- `deprecated`
- `obsolete`
- `old`
- `migration`
- `temporary`
- `TODO remove`
- `FIXME remove`
- `remove after`

## Classification

### Safe To Remove Or Archive

| Item | Location | Evidence | Action |
| --- | --- | --- | --- |
| One-off patch helper | `patch_capture.py` | Regex patcher for old input capture hotkey code. No build/test/runtime references. | Archived to `tools/archive/patch_capture.py`. |
| Commented ETW provider scaffolding | `src/core/etw_controller.cpp` | Comment-only historical provider definitions. | Safe future source comment cleanup. Not changed in this pass. |

### Needs Review

| Item | Location | Reason |
| --- | --- | --- |
| Old F8 emergency capture references | `docs/forensics/CLEANUP_REPORT.md`, `docs/forensics/HOTKEY_AUDIT_REPORT.md`, `docs/forensics/FORENSIC_INVENTORY.md`, `src/core/input_capture.cpp` | Documentation still describes `VK_F8` emergency dump, while current source no longer has an active F8 handler. Input/focus code is out of scope. |
| Timer message terminology | `include/core/timing.h`, `include/core/types.h`, historical thread audit docs | Comments and constant mention `WM_TIMER_EXPIRED`, but current timer path uses direct callbacks. Timer behavior is out of scope. |
| Runtime config migration leftovers | `include/core/config.h`, `src/core/runtime_config.cpp`, forensic reports | Several constants look unreferenced, but config defaults are behavior-adjacent. |
| Historical forensic and final verdict reports | `docs/forensics/` | Some reports are superseded by architecture reports but still useful as investigation history. |
| Maintenance rewrite scripts in tests | `tests/forensics/fix_includes.py`, `tests/forensics/restructure.py` | These are not tests. They may be useful as historical migration tooling. |

### Still Active Or False Positive

| Match | Location | Reason |
| --- | --- | --- |
| Core migration telemetry | `src/core/input_capture.cpp`, `src/core/timing.cpp` | `migration` is an active telemetry term, not legacy infrastructure. |
| `old` GDI variables | UI source | Common Win32 pattern for restoring selected objects, not obsolete code. |
| Golden baseline | `tests/regression/golden_outputs/physics_v26_baseline.csv` | `baseline`/old-version naming is expected for regression fixtures. |
| Historical audit text | `docs/forensics/*.md`, `docs/reports/*.md` | Reports intentionally preserve prior decisions and should not be edited merely for legacy keywords. |

## Result

Only one legacy item met the safe archival threshold: `patch_capture.py`. All gameplay-adjacent, timer-adjacent, input-adjacent, and config-adjacent findings remain documented only.

