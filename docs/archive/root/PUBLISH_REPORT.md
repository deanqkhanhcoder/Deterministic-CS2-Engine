# Publish Report

Date: 2026-05-30

## Branch

`v27-stabilization`

## Commit

Commit hash:

`dac4248e21e1a8bbc2717dbc4210a2e1e743ea2c`

Commit message:

`docs(readme): refresh project status for v27.4`

Files committed:

- `README.md`
- `PROJECT_BRAIN.md`

## Tag

Tag:

`v27.4.0-stable`

Annotated tag object hash:

`dadb5439a7d665a51f7d39b951c27642fc8bd7f6`

Tag target commit:

`dac4248e21e1a8bbc2717dbc4210a2e1e743ea2c`

Tag message:

`Marco V27.4 stabilization candidate`

## Audit Before Publish

Executed:

- `git status --short --branch`
- `git log --oneline -20`
- `README.md` read
- `PROJECT_BRAIN.md` read
- `git ls-files runtime logs FORENSIC_CAPTURE.log *.log *.dmp *.exe *.o`
- `git ls-remote --tags origin v27.4.0-stable`
- `git diff --check`

Findings:

- Working tree was clean before the README/PROJECT_BRAIN documentation edit.
- No local `v27.4.0-stable` tag existed before tagging.
- No remote `v27.4.0-stable` tag existed before tagging.
- No tracked runtime/log/crash/build outputs were found by the publish audit.
- No root `logs/` directory was found.
- No `runtime/runtime/` directory was found.
- No `FORENSIC_CAPTURE.log` file was found.
- `gh` CLI is not installed in this environment. Git push/tag push were performed with `git`.

## README Changes

`README.md` was refreshed from stale V26 wording to current V27.4 stabilization status.

Added or updated:

- Marco project summary.
- Current Status section:
  - Version: `v27.4.0-stable`
  - Status: Stabilization Candidate
  - Focus Desync fix highlight
  - runtime/log/crash/artifact architecture highlight
  - forensic infrastructure highlight
  - `PROJECT_BRAIN.md` entrypoint highlight
  - path governance highlight
- Architecture overview.
- Counter-Strafe section.
- BHOP section.
- Runtime folder map.
- `PROJECT_BRAIN.md` mandatory entrypoint note.
- `docs/reports/` and `docs/forensics/` location note.
- Build commands and expected binary paths.
- Development rules that prohibit speculative gameplay claims.

## PROJECT_BRAIN Changes

Added `## Release Roadmap` with:

- Current: `v27.4.0-stable`
- Status: Stabilization Candidate
- Requirements before release:
  - 3-7 days of real gameplay evidence
  - no Counter-Strafe failure recurrence
  - no BHOP failure recurrence
  - no Focus Desync recurrence
- Next milestone: V27.5 Observability Slimdown
- Future direction:
  - Release build uses minimal telemetry
  - Debug/Profile keep full forensic capability
  - reduce forensic noise
  - freeze architecture

## Push Result

Branch push:

```text
git push origin v27-stabilization
```

Result:

```text
To https://github.com/deanqkhanhcoder/Deterministic-CS2-Engine.git
 * [new branch]      v27-stabilization -> v27-stabilization
```

Tag push:

```text
git push origin v27.4.0-stable
```

Result:

```text
To https://github.com/deanqkhanhcoder/Deterministic-CS2-Engine.git
 * [new tag]         v27.4.0-stable -> v27.4.0-stable
```

Remote tag audit after push:

```text
dadb5439a7d665a51f7d39b951c27642fc8bd7f6 refs/tags/v27.4.0-stable
```

## Release Readiness Verdict

Verdict:

Stabilization Candidate, not production release.

Evidence supports publishing the documentation refresh and stabilization tag. It does not support a production release claim because 3-7 days of long-session gameplay evidence has not been recorded in this publish pass.

Required before release certification:

- 3-7 days of real gameplay.
- No recurrence of Counter-Strafe failure.
- No recurrence of BHOP failure.
- No recurrence of Focus Desync.
- Runtime logs reviewed for anomaly recurrence.

## Notes

No gameplay files were modified in this publish pass. No Counter-Strafe, BHOP, input routing, focus behavior, runtime config behavior, or timing logic changes were made.

This report was generated after the branch and tag were pushed so it can include final commit, tag, and push evidence.
