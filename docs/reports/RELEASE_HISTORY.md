# RELEASE HISTORY

## Version: v27.8.0-stable (Input Stabilization & Concurrency Safety)
*Date: 2026-08-03*

Highlights:
- **Target-Bound Synthetic Input**: Bound input injection and release ownership strictly to validated foreground target identities (`TargetIdentity`).
- **Hook-Owner Thread Dispatch**: Dispatched timer callbacks and BHOP injection requests onto the hook-owner message thread (`BhopInjectionQueue`), preventing worker-thread SendInput hangs and race conditions.
- **Seqlock Writer Synchronization**: Added `s_writerMutex` inside `rcfg::Apply()`, `GetMutable()`, and `Init()` to serialize configuration writes and prevent tearing/data races.
- **Bounded Logging Queue**: Converted `s_logQueue` in `debug_logger.cpp` into a bounded queue (`kMaxQueuedMessages = 4096`) to eliminate unbounded heap growth.
- **Circular ETW Tracing**: Enabled `EVENT_TRACE_FILE_MODE_CIRCULAR` to prevent ETW trace file starvation during marathon sessions.
- **Safe Test Gates & Isolation**: Integrated automated 18-test CTest regression suite (`make check`), PE import static auditing (`scripts/test/check_no_input_imports.py`), and private desktop isolation guards (`desktop_isolation_guard.cpp`).
- No gameplay physics regressions. Golden regression suite passes with 100% deterministic output match.

Status:
SOURCE CERTIFIED
AUTOMATED SUITE PASS (18/18 TESTS)
STABLE

## Version: v27.7 (Focus-Storm Hardening)
*Date: 2026-06-03*

Highlights:
- Fixed 100ms+ `DispatchMessage` stalls triggered during rapid OS focus flapping across multiple window handles (Focus Storm).
- Decoupled `OutputDebugStringA` execution by moving it into the background `LogWorker()` thread to prevent blocking the Main Thread's `KeyboardProc`.
- Optimized `s_focusMutex` in `IsTargetActive()` to only protect state transition reads/writes, moving telemetry emission and logging outside the critical section.
- No gameplay behavior changes.

Status:
SOURCE CERTIFIED
STABLE

## Version: v27.6 (Maturity Pass)
*Date: 2026-06-01*

Highlights:
- Purged Watchdog architecture (engine and UI) to reduce footprint.
- Decoupled ForensicRingBuffer to persist focus tracking in production.
- Kept WARN/ERROR/FATAL logging available in Release builds, omitting TRACE/INFO.
- Cleaned up dead code (autofire, F8 tracking, unused build flags).
- Repaired telemetry pipeline to expose thread health metrics in Release dashboard.
- No gameplay behavior changes.

Status:
SOURCE CERTIFIED
STABLE

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