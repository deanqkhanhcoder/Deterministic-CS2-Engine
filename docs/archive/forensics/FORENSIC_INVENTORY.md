# Forensic Inventory
- `MARCO_ENABLE_FORENSIC`: Unified macro controlling all telemetry.
- `DLOG`: Used for forensic tracing.
- `telemetry::g_forensicBuffer`: The ring buffer for capturing traces.
- `VK_F8`: The emergency core dump hotkey.
- ETW Trace macros: Used for detailed subsystem tracing (e.g. `MARCO_ENABLE_ETW`).
