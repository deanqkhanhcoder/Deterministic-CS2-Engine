# V27.6 MATURITY REPORT
*Date: 2026-06-01*

## 1. Removed Components
- Watchdog subsystem (heartbeats, fail-safe triggers) from Engine (`state_engine.cpp`)
- UI Watchdog and Freeze Logger from Diagnostics (`ui_diagnostics.cpp`)
- Obsolete Autofire subsystem leftovers (`autofire_controller.cpp` was confirmed absent)
- Obsolete F8 tracking bindings (confirmed absent)
- Deprecated `WM_TIMER_EXPIRED` Windows message

## 2. Removed Flags
- `MARCO_ENABLE_HEARTBEATS`: Removed from `build_config.h` and source code.
- `MARCO_ENABLE_DIAGNOSTICS`: Disabled where appropriate; watchdog usage stripped out.
- `MARCO_ENABLE_LOGGING`: Checked and removed (if any remnants).

## 3. Logging Matrix
| Build Type | TRACE | INFO | WARN | ERROR | FATAL |
|---|---|---|---|---|---|
| DEBUG | YES | YES | YES | YES | YES |
| PROFILE | YES | YES | YES | YES | YES |
| RELEASE | NO | NO | YES | YES | YES |

## 4. Observability Matrix
- **Production Telemetry**: `ForensicRingBuffer` tracks critical events (`FOCUS_LOST`, `FOCUS_GAINED`, `PROFILE_CHANGED`) independently of `MARCO_ENABLE_FORENSIC`. Available in all builds.
- **Developer Forensics**: Micro-events (`BHOP_STALL`, `COUNTERSTRAFE_CONFLICT`, `LOGICAL_PHYSICAL_DIVERGENCE`) remain gated by `MARCO_ENABLE_FORENSIC`.

## 5. Documentation Cleanup
- Synced `PROJECT_BRAIN.md` with completed goals.
- Removed Watchdog and Autofire references from `ARCHITECTURE_MAP.md`.
- Cleared obsolete debt lists and updated known bug tracking in `MARCO_KNOWLEDGE_BASE.md`.
- Added V27.6 highlights to `RELEASE_HISTORY.md`.

## 6. Verification Results
- Source cleanup successfully compiled across all environments.
- Verified absence of Watchdog overhead.
- No changes made to Counter-Strafe, BHOP, state reconciliation, or timer behaviors.
- Validation steps (Build tests) pass without regression.

## 7. Remaining Deferred Issues
The following deferred issues remain unchanged and are out of scope for V27.6:
1. Movement LUT synchronization review (BUG-002)
2. Space Swallow focus-regain review (BUG-003)
3. Resolver Starvation (BUG-004)
4. Emergency Unhook Blackhole (BUG-005)
5. Cross-Thread UI Blocking (BUG-006)
6. Test Infrastructure Rot (BUG-007)
