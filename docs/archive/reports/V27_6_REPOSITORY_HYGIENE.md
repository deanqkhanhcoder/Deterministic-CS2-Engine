# MARCO V27.6 REPOSITORY HYGIENE REPORT

## 1. Directory Ownership Map

| Directory | Primary Owner | Purpose |
|---|---|---|
| `build/` | Build System | Intermediate object files, grouped by profile (debug/profile/release). |
| `docs/` | Documentation | Source of Truth (SSOT), knowledge base, release history, and report archives. |
| `include/` | C++ Compiler | Core C++ headers categorized into core engine and UI subsystems. |
| `runtime/` | Marco Executable | Local runtime paths for binaries, configurations, captures, logs, and crash dumps. |
| `scripts/` | Developer / CI | Batch and Python scripts for orchestrating builds and test runs. |
| `src/` | C++ Compiler | Core C++ implementation files. |
| `tests/` | QA / Testing | Test suites spanning unit, regression, and forensic tests. |
| `tools/` | QA / Forensics | Diagnostic, profiling, and simulation tools (primarily Python). |

## 2. Dead File Candidates

The following files have no active role in the codebase and are safe to **DELETE** or **ARCHIVE**:

*   `runtime/artifacts/*` (11 files including `trace_output.txt`, `current_output.csv`, etc.) — **DELETE**. Leftover experiments from V27.4/V27.5 profiling.
*   `test_run.ps1` (Project root) — **DELETE / MOVE**. Abandoned or out-of-place execution script.
*   `parse_trace.py` (Project root) — **MOVE**. Should belong in `tools/forensics/` or be deleted if redundant.
*   `docs/RELEASE_HISTORY.md` — **DELETE**. Duplicate. The canonical release history is located at `docs/reports/RELEASE_HISTORY.md` (or vice versa, needs consolidation).

## 3. Empty Directory Candidates

The following directories exist without files, but belong to runtime governance. They should be classified as **KEEP** for structure integrity, but are currently empty:

*   `runtime/captures/`
*   `runtime/config/`
*   `runtime/crash/`

## 4. Legacy Artifact Candidates

The following reports were used extensively during recent sprints but are no longer active reference points. They should be moved to **ARCHIVE**:

*   `docs/DOCUMENTATION_CLEANUP_REPORT.md` — **ARCHIVE** to `docs/archive/reports/`
*   `docs/REPORT_INDEX.md` — **ARCHIVE** to `docs/archive/reports/`
*   `docs/reports/V27_4_FINAL_DEEP_SCAN.md` — **ARCHIVE**
*   `docs/reports/V27_5_POST_HARDENING_REVIEW.md` — **ARCHIVE**
*   `docs/reports/V27_5_RELEASE_CANDIDATE.md` — **ARCHIVE**
*   `docs/reports/V27_6_FINAL_AUDIT.md` — **ARCHIVE**
*   `docs/reports/V27_6_RC_CLEANUP_REPORT.md` — **ARCHIVE**
*   `docs/reports/V27_6_TELEMETRY_REPAIR.md` — **ARCHIVE**
*   `docs/reports/FINAL_PROJECT_SIGNOFF.md` — **ARCHIVE**
*   `docs/reports/METRIC_PIPELINE_AUDIT.md` — **ARCHIVE**
*   `docs/reports/V27_6_RELEASE_GATE.md` — **ARCHIVE**

*Evidence:* These reports document the journey of previous hardening and stabilization phases. They are not active documentation of the system's current architecture (which is governed entirely by `MARCO_KNOWLEDGE_BASE.md`).

## 5. Recent Change Audit

**Scope:** Commits between V27.5 Hardening and V27.6 Telemetry Repair.
**Target:** Accidental debug code, temporary diagnostics, leftover forensic experiments.

*   **Debug Macros:** Verified that `MARCO_ENABLE_DIAGNOSTICS`, `autofire` overrides, and temporary `printf` traces were strictly purged during the RC Cleanup Pass.
*   **Release Cleanliness:** Evaluated differences across the core engine (e.g., `state_engine.cpp`, `telemetry.cpp`, `timing.cpp`). No orphaned `OutputDebugStringA` or debug UI overlay remnants remain active in the `Release` build path.
*   **Result:** **PASS**. No accidental debug leakage detected.

## 6. Recommended Deletes

Actionable summary for the subsequent deletion pass:

1.  **DELETE:** Entire contents of `runtime/artifacts/`
2.  **DELETE:** `test_run.ps1` from root (if not actively used by local QA).
3.  **CONSOLIDATE/DELETE:** Duplicate `RELEASE_HISTORY.md` from either `docs/` or `docs/reports/`.
4.  **ARCHIVE:** Move all transient V27.4/V27.5/V27.6 markdown reports from `docs/` and `docs/reports/` into `docs/archive/reports/`.
5.  **MOVE:** `parse_trace.py` from root into `tools/forensics/`.
