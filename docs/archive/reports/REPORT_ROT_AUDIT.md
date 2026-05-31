# Report Rot Audit

Date: 2026-05-30
Scope: `docs/reports/` and `docs/forensics/`.

## Keep

| Report | Reason |
| --- | --- |
| `docs/REPORT_INDEX.md` | Canonical report index. |
| `docs/reports/PROJECT_STRUCTURE_AUDIT.md` | Current structure ownership record. |
| `docs/reports/LOGGING_ARCHITECTURE.md` | Current logging policy and producer map. |
| `docs/reports/PATH_GOVERNANCE_REPORT.md` | Current path service governance. |
| `docs/reports/OBSERVABILITY_MAP.md` | Current observability ownership model. |
| `docs/reports/INFRASTRUCTURE_REFACTOR_REPORT.md` | Latest executed infrastructure refactor evidence. |
| `docs/reports/REPO_INVENTORY.md` | Current repository inventory, but needs update after script archival. |
| `docs/reports/RELEASE_CLEANUP_REPORT.md` | Historical release cleanup record, but root file list is now stale. |
| `docs/forensics/FORENSIC_INFRA_AUDIT.md` | Current forensic logging audit. |
| `docs/forensics/FORENSIC_REWORK_PLAN.md` | Current forensic rework plan. |
| `docs/forensics/LOG_LOCATION_REPORT.md` | Current log location documentation. |

## Archive Candidates

These reports are useful history but are likely superseded by current architecture reports or final release summaries.

| Report | Recommendation |
| --- | --- |
| `docs/forensics/FILE_AUDIT_REPORT_B.md` | Archive after confirming no open action remains. |
| `docs/forensics/FILE_AUDIT_REPORT_D.md` | Archive after confirming no open action remains. |
| `docs/forensics/FILE_AUDIT_REPORT_E.md` | Archive after confirming no open action remains. |
| `docs/forensics/CLEANUP_REPORT.md` | Archive; contains historical F8 statement that appears stale. |
| `docs/forensics/FINAL_STABILIZATION_VERDICT.md` | Archive or relabel as historical. Avoid treating as current readiness evidence. |
| `docs/forensics/FINAL_V26_REPORT.md` | Archive as previous-version historical report. |
| `docs/forensics/V26_STABILIZATION_COMPLETE.md` | Archive as previous-version historical report. |
| `docs/forensics/STRESS_TEST_REPORT.md` | Archive or merge into a stress category summary. |
| `docs/forensics/STRESS_ANALYSIS.md` | Archive or merge into a stress category summary. |

## Merge Candidates

| Reports | Target |
| --- | --- |
| `BUILD_MODE_AUDIT_REPORT.md`, `CONFIG_DRIFT_AUDIT.md` | Merge build flag facts into current config drift report after review. |
| `PERFORMANCE_BASELINE_REPORT.md`, `PERFORMANCE_ROLLBACK_AUDIT.md`, `REGRESSION_REPORT.md` | Merge into a performance/regression history index. |
| `HOTKEY_AUDIT_REPORT.md`, `FORENSIC_INVENTORY.md`, `FORENSIC_INFRA_AUDIT.md` | Keep current forensic infra report canonical; mark older hotkey inventory as historical. |

## Delete Candidates

None. Reports are cheap historical evidence. The safer cleanup is indexing, archiving, and marking superseded documents as historical.

