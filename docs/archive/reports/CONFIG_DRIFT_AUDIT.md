# Config Drift Audit

Date: 2026-05-30
Scope: build config, runtime config, profile config, and feature flags. No config behavior was changed.

## Build Flag Matrix

Defined in `include/ui/build_config.h`.

| Flag | Debug | Profile | Release | Observed Consumers |
| --- | --- | --- | --- | --- |
| `MARCO_ENABLE_ETW` | 1 | 1 | 0 | Active. |
| `MARCO_ENABLE_WATCHDOG` | 1 | 0 | 0 | Active. |
| `MARCO_ENABLE_HEARTBEATS` | 1 | 1 | 1 | Active. |
| `MARCO_ENABLE_DIAGNOSTICS` | 1 | 1 | 0 | Active. |
| `MARCO_ENABLE_UI_OVERLAY` | 1 | 0 | 0 | No observed consumers outside definition/docs. |
| `MARCO_ENABLE_FORENSIC` | 1 | 1 | 1 | Active; gates forensic and debug logging code. |
| `MARCO_ENABLE_LOGGING` | 1 | 0 | 0 | No observed consumers outside definition/docs. |
| `MARCO_ENABLE_ASSERTS` | 1 | 0 | 0 | No observed consumers outside definition/docs. |
| `MARCO_ENABLE_UI` | 1 | 1 | 1 | Active. |
| `MARCO_ENABLE_FORENSIC_UI` | 1 | 0 | 0 | Active. |
| `MARCO_ENABLE_RELEASE_DASHBOARD` | 0 | 0 | 1 | Active. |

## Drift Findings

| Item | Evidence | Risk | Recommendation |
| --- | --- | --- | --- |
| `MARCO_ENABLE_LOGGING` unused | Debug logger uses `MARCO_ENABLE_FORENSIC`, not `MARCO_ENABLE_LOGGING`. | Medium. Build docs imply separate logging control that does not exist. | Decide whether to wire logger to the flag or remove the flag in a dedicated build config cleanup. |
| `MARCO_ENABLE_ASSERTS` unused | No source consumers found. | Low to medium. Misleads engineers about assert policy. | Remove or implement in dedicated build config cleanup. |
| `MARCO_ENABLE_UI_OVERLAY` unused | No source consumers found. | Low to medium. Implies a disabled overlay feature without implementation. | Remove or document as reserved only. |
| Legacy constants in `include/core/config.h` | Several constants have declaration-only references. | Medium to high because runtime config defaults are behavior-adjacent. | Do not remove in sanitation pass. Review with runtime config owner. |
| Build config header banner encoding | Header comments display mojibake in terminal output. | Low runtime risk; repository readability issue. | Optional comment-only cleanup later. |

## Runtime Config Notes

Runtime configuration has migrated many values into getters and profile state. That creates two classes of constants:

- Active defaults still used through runtime config construction or getters.
- Declaration-only legacy constants that may be stale.

Because runtime config behavior is explicitly out of scope, this pass documents the drift and does not change any value or removal.

## Result

No config changes were made. The most important cleanup candidate is reconciling `MARCO_ENABLE_LOGGING` with the actual logger gate.

