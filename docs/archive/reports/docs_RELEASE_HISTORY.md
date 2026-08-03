# Release History

## V27.6 Maturity & Observability Slimdown
*Date: 2026-06-01*
- Removed experimental Watchdog architecture to reduce runtime footprint and background I/O operations.
- Cleaned up duplicate and dead logic (`autofire_controller.cpp`).
- Decoupled `ForensicRingBuffer` from the `MARCO_ENABLE_FORENSIC` build flag, allowing Release builds to log critical focus anomalies for diagnostic support without full diagnostic overhead.
- RC Cleanup: Purged ghost diagnostic macros, dead UI diagnostic files, and stale configuration properties to achieve final repository cleanliness.
