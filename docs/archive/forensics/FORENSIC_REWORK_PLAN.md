# Forensic Rework Plan

Date: 2026-05-30
Status: Infrastructure patch applied locally; runtime gameplay validation is NOT TESTED.

## Goals

1. Remove forensic dependency on manual F8 capture.
2. Put logs in a deterministic `runtime/logs/` directory.
3. Use timestamped per-session log names.
4. Flush automatically without duplicating old ring entries.
5. Flush on important lifecycle edges: focus, profile change, shutdown, crash.
6. Record anomaly events only, not raw input streams.

## Patch List

1. `include/core/telemetry.h`
   - Added explicit forensic event enum for anomaly events.
   - Added `ForensicRingBuffer::flushed` cursor.
   - Changed ring push to preserve ring consistency under concurrent flush.
   - Added `GetForensicLogPath()`.

2. `src/core/telemetry.cpp`
   - Added deterministic default path creation under `runtime/logs/`.
   - Removed fallback to relative `FORENSIC_CAPTURE.log`.
   - Reworked `FlushToFile()` to write only unflushed events.
   - Added human-readable event names.
   - Added session header fields: version, build, build date, PID, CWD, EXE path, log path, scope, auto-flush policy.
   - Kept 1000 ms auto-flush thread.
   - Kept unhandled exception flush path.

3. `src/core/main.cpp`
   - Added forensic flush request to the severe vectored crash handler.
   - Existing startup path creates `runtime/logs/marco_YYYY-MM-DD_HH-MM-SS.log`.
   - Existing shutdown path still calls `ShutdownForensics()`.

4. `src/core/timer_lifecycle.cpp`
   - Removed normal `TIMER_EXECUTED` forensic file event.
   - Kept `TIMER_REJECTED` as an anomaly event.

5. `src/core/debug_logger.cpp`
   - Moved debug logger output to the same deterministic `runtime/logs/` root.
   - Path is now `runtime/logs/marco_debug.log`.

6. `include/ui/build_config.h`
   - Enabled `MARCO_ENABLE_FORENSIC` for Profile builds.
   - Debug, Profile, and Release now compile forensic infrastructure.

## Affected Files

- `include/core/telemetry.h`
- `src/core/telemetry.cpp`
- `src/core/main.cpp`
- `src/core/timer_lifecycle.cpp`
- `src/core/debug_logger.cpp`
- `include/ui/build_config.h`
- `docs/forensics/FORENSIC_INFRA_AUDIT.md`
- `docs/forensics/FORENSIC_REWORK_PLAN.md`
- `docs/forensics/LOG_LOCATION_REPORT.md`

## Event Scope

Logged anomaly event types:

- `FOCUS_LOST`
- `FOCUS_GAINED`
- `PROFILE_CHANGED`
- `COUNTERSTRAFE_CANCELLED`
- `COUNTERSTRAFE_CONFLICT`
- `BHOP_ABORTED`
- `BHOP_STALL`
- `TIMER_REJECTED`
- `LOGICAL_PHYSICAL_DIVERGENCE`

Not logged to forensic file:

- Raw keyboard input
- Raw mouse input
- Normal timer execution
- Normal hook latency samples

## Validation Plan

1. Build Debug, Profile, and Release.
2. Launch each build and confirm a real `runtime/logs/marco_*.log` appears with a session header.
3. Trigger focus loss and focus regain; confirm events flush without pressing F8.
4. Change profile; confirm `PROFILE_CHANGED` appears and flushes immediately.
5. Reproduce the intermittent Counter-Strafe/BHOP failure during real gameplay and inspect anomaly sequence.

Until steps 2-5 are completed on a live run, the field result remains NOT TESTED.
