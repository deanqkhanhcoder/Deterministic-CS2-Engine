# Test Rot Audit

Date: 2026-05-30
Scope: `tests/`.

## Keep

| Item | Reason |
| --- | --- |
| `tests/unit/test_hybrid.cpp` | Unit test source. |
| `tests/unit/test_physics.cpp` | Unit test source. |
| `tests/regression/golden_outputs/physics_v26_baseline.csv` | Regression baseline fixture. |
| `tests/test_stress.cpp` | Mock stress harness. Keep, but do not treat as gameplay evidence. |
| `tests/forensics/simulator.py` | Forensic simulator; currently useful as analysis tooling. |
| `tests/forensics/chaos_simulator.py` | Forensic stress/chaos simulator; currently useful as analysis tooling. |
| `tests/forensics/forensic_sweep.py` | Forensic sweep tool; currently useful as analysis tooling. |
| `tests/forensics/queue_race_simulator.py` | Race-focused simulator; currently useful as analysis tooling. |
| `tests/forensics/stale_timer_fuzzer.py` | Timer-focused fuzzer; currently useful as analysis tooling. |
| `tests/forensics/replay_diff_test.py` | Replay comparison utility. |
| `tests/forensics/replay_heatmap_generator.py` | Replay artifact generator. |
| `tests/forensics/quantization_stability_heatmap.py` | Artifact generator already routed to `runtime/artifacts/`. |

## Archive Candidates

| Item | Evidence | Recommendation |
| --- | --- | --- |
| `tests/forensics/fix_includes.py` | Maintenance rewrite helper, not a test. No invocation found. | Move to `tools/maintenance/` or archive after owner review. |
| `tests/forensics/restructure.py` | Maintenance rewrite helper, not a test. No invocation found. | Move to `tools/maintenance/` or archive after owner review. |

## Delete Candidates

| Item | Evidence | Recommendation |
| --- | --- | --- |
| `tests/forensics/cs2.exe` | Ignored executable. Earlier inventory found it duplicates `dummy_cs2.exe` by hash. No source references found. | Delete after forensics owner confirms it is not a manually launched local fixture. Not removed in this pass. |

## Notes

- Mock stress harnesses are not gameplay proof. They can detect deterministic internal regressions, but they must not be cited as evidence that intermittent in-game Counter-Strafe or BHOP failures are fixed.
- Binary fixtures should live under an explicit fixture/artifact policy. Ignored executables inside `tests/` make repository state hard to audit.

