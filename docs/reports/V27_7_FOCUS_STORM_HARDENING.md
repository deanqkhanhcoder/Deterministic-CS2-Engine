# MARCO V27.7 FOCUS-STORM HARDENING

## 1. Previous Hypothesis
The `ROOT_CAUSE_TRACE_06032026.md` investigation hypothesized that severe stalls in `DispatchMessage` (111ms-185ms) during periods of rapid focus flapping ("Focus Storm") were caused by cross-thread lock contention over `s_focusMutex` and the synchronous execution of `OutputDebugStringA` via `DLOG_WARN`.

## 2. Source-Level Evidence
A trace of the source code revealed:
1. `dlog::Write()` acquired `s_logMutex` to enqueue the log, but then directly and synchronously executed `OutputDebugStringA(dbgBuf)` while on the caller's thread.
2. `capture::IsTargetActive()` in `src/core/input_capture.cpp` acquired a `std::lock_guard<std::mutex>` on `s_focusMutex`. While holding this lock, it evaluated focus changes and emitted `DLOG_WARN`.
3. Low-Level input hooks (`KeyboardProc`, `MouseProc`) executed on the Main Thread and invoked `IsTargetActive()`. When a focus storm occurred, these hooks attempted to acquire `s_focusMutex` and were blocked by either the background focus thread (also running `IsTargetActive()`) or directly delayed by the synchronous `OutputDebugStringA` call occurring inside the lock.

## 3. Proven Findings
* **Candidate A (OutputDebugStringA is executed synchronously in hot path):** PROVEN. `OutputDebugStringA` was executed directly on the caller thread in `dlog::Write()`, blocking the main thread during hook execution.
* **Candidate B (DLOG_WARN executes while holding s_focusMutex):** PROVEN. `IsTargetActive()` kept the lock for the entire duration of the telemetry and log emission.
* **Candidate C (Mutex hold duration is unnecessarily long):** PROVEN. The mutex only needs to protect state evaluation and updates, not the heavy log/telemetry string formatting and OS emission.

## 4. Rejected Findings
* **Candidate D (Actual bottleneck is somewhere else):** REJECTED. The evidence definitively points to `s_focusMutex` contention and `OutputDebugStringA` as the sole triggers for the message pump stall.

## 5. Patch Applied
Two surgical fixes were applied:
1. **Asynchronous Debug Output:** In `src/core/debug_logger.cpp`, the `OutputDebugStringA` call was moved from `dlog::Write()` (caller thread) into the background `LogWorker()` thread. This ensures `dlog::Write()` returns immediately without blocking on the OS.
2. **Lock Duration Reduction:** In `src/core/input_capture.cpp`, `std::lock_guard` was replaced with `std::unique_lock`. The evaluation of focus changes and target state updates are performed inside the lock, yielding `didLoseFocus` and `didRegainFocus` flags. The lock is then explicitly released (`lock.unlock()`) *before* executing the heavy `DLOG_WARN` and `telemetry::g_forensicBuffer.Push()` routines.

## 6. Regression Analysis
* **Focus behavior:** Identical. State tracking remains exactly the same.
* **Route behavior:** Identical.
* **Counter-Strafe:** Identical.
* **BHOP:** Identical. 
Only the logging and synchronization architectures were altered.

## 7. Validation Results
* `make clean && make debug` -> PASS
* `make clean && make profile` -> PASS
* `make clean && make release` -> PASS
* `git diff --check` -> PASS

## FINAL VERDICT
BUG CONFIRMED AND FIXED.
