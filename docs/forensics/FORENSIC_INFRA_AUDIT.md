# Forensic Infrastructure Audit

Date: 2026-05-30
Branch: v27-stabilization
Scope: telemetry.cpp, ForensicRingBuffer, FlushForensicLog, startup, shutdown, crash path.

## Evidence Level

This audit is based on static code inspection and local build verification only.

Runtime gameplay evidence: NOT TESTED.
Live reproduction evidence: INSUFFICIENT EVIDENCE.

## Findings

1. Previous flush behavior could duplicate forensic events.
   - `ForensicRingBuffer::FlushToFile()` used the current head and wrote the entire retained ring window on every flush.
   - With 1 second auto flush, the same events could be appended repeatedly.
   - This made the log noisy and weakened event ordering analysis.

2. Relative fallback path was unsafe for field debugging.
   - `FlushForensicLog()` could fall back to `FORENSIC_CAPTURE.log` when the configured path was empty.
   - That path depended on process CWD and did not satisfy deterministic log discovery.

3. Session metadata existed but needed cleanup.
   - Header included PID, CWD, EXE, build type, and log path.
   - Version string was not aligned with the handoff tag and was changed to `v27.4.0-stable`.

4. F8 was not a reliable forensic trigger.
   - Current code no longer has an active F8 flush branch, but stale F8 state tracking still exists in input capture.
   - The required path is automatic flush plus explicit lifecycle flush, not hotkey-dependent capture.

5. Crash coverage was split.
   - `telemetry.cpp` registered an unhandled exception filter that flushes forensic data.
   - `main.cpp` also has a vectored crash handler for severe faults, but it previously only wrote `startup_crash.log`.
   - The vectored crash handler now also requests a forensic flush when forensic is compiled in.

6. Profile build disabled forensic capture.
   - `MARCO_PROFILE` had `MARCO_ENABLE_FORENSIC 0`.
   - That conflicts with the stated need to capture intermittent runtime failures during non-Debug builds.

7. Anomaly event inventory mostly existed.
   - Present: `FOCUS_LOST`, `FOCUS_GAINED`, `PROFILE_CHANGED`, `COUNTERSTRAFE_CANCELLED`, `COUNTERSTRAFE_CONFLICT`, `BHOP_ABORTED`, `BHOP_STALL`, `TIMER_REJECTED`, `LOGICAL_PHYSICAL_DIVERGENCE`.
   - Removed from forensic file path: `TIMER_EXECUTED`, because normal timer execution is not an anomaly.

## Current State After Rework

- Forensic logs are generated under `runtime/logs/`.
- File format is `marco_YYYY-MM-DD_HH-MM-SS.log`.
- `FORENSIC_CAPTURE.log` fallback was removed.
- Flush is incremental: each ring event is written once unless the ring overwrites old unflushed data.
- Auto flush interval is 1000 ms.
- Explicit flush is still requested on focus loss, focus gain, profile change, shutdown, and severe crash path.
- Session header includes version, build, build date, PID, CWD, EXE path, log path, and scope.

## Residual Risks

- Crash-time flushing is best effort. If the process faults while holding the ring buffer lock, the flush can be skipped with a lock-busy marker.
- Event producers still write directly to `g_forensicBuffer`; a future cleanup should centralize event creation behind a helper API.
- Some divergence/conflict instrumentation may still be too chatty under persistent anomaly conditions.
- Runtime gameplay behavior remains NOT TESTED by this audit.
