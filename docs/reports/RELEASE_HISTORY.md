# RELEASE HISTORY

## Version: v27.5-rc1
*Date: 2026-05-30*

Highlights:
- Focus Desync fixed
- Certified runtime risks removed
- Hook I/O starvation removed
- Watchdog deadlock removed
- Movement LUT race removed
- Observability architecture stabilized

Status:
SOURCE CERTIFIED
RUNTIME NOT YET VERIFIED

## V27.4 (Stabilization Candidate)
*Date: 2026-05-30*
- **Focus Desync Fix**: Resolved an issue where input routing became permanently disabled due to stale focus cache evaluation.
- **Timer Jitter Elimination**: Removed `WM_TIMER_EXPIRED` message queue dispatch in favor of a direct callback architecture to guarantee microsecond precision.
- **BHOP Concurrency Refactor**: Separated BHOP logic into an isolated worker thread using a Condition Variable to prevent hook blocking.
- **State Reconciliation**: Implemented logical-to-physical synchronization upon window focus regain.
- **Forensic Infrastructure**: Introduced lock-free ring buffers to capture high-frequency intermittent failure data in production without degrading hook performance.

## V27.3 (Legacy)
- Implemented core rollback architecture.
- Replaced experimental asynchronous 1-tick fire delays with purely deterministic counter-strafe ordering.
- Improved UI dashboard with real-time latency timelines.

## V26.0 (Legacy Baseline)
- Deterministic CS2 physics baseline.
- Regression validation against golden outputs.