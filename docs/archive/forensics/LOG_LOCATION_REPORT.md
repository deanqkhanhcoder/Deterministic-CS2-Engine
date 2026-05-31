# Log Location Report

Date: 2026-05-30

## Forensic Log Path

Forensic sessions now write to:

```text
<project-root>\runtime\logs\marco_YYYY-MM-DD_HH-MM-SS.log
```

Example:

```text
C:\Users\toanpq\Desktop\marco\runtime\logs\marco_2026-05-30_14-20-05.log
```

`<project-root>` is resolved from the executable path by `workspace::GetProjectRootW()`.

## Directory Creation

The `runtime\logs\` directory is created by `workspace::EnsureLogDirectoryExists()` before forensic logging starts.

## Removed Location

This path is no longer used by forensic flushing:

```text
FORENSIC_CAPTURE.log
```

Reason: it was relative to the process CWD and could be hard to locate during field debugging.

## Related Logs

Debug logger output now writes to:

```text
<project-root>\runtime\logs\marco_debug.log
```

Startup crash marker still writes to:

```text
<project-root>\runtime\crash\startup_crash.log
```

## Header Fields

Every new forensic session log starts with:

- Version
- Build
- BuildDate
- SessionStartLocal
- TickMs
- PID
- CWD
- EXE path
- LogPath
- Scope
- AutoFlush policy

## Evidence Status

Static path audit: completed.
Runtime log creation: NOT TESTED.
Gameplay reproduction: NOT TESTED.
