# MARCO ROOT CAUSE TRACE

## PHASE 1: TIMELINE RECONSTRUCTION
Based on `runtime/logs/marco_debug.log`:
* **T0:** `Failed to register MMCSS for hook thread (error 1550)` - Occurs at startup during `topology::PinHookThread`.
* **T1 - T17:** Severe focus flapping. `Target focus LOST` and `Target focus REGAINED` oscillate rapidly across multiple PIDs (1744, 8148, 10684, 7996, 4968, 12728).
* **T18:** `Bhop ENABLED` - User enables BHOP via hotkey.
* **T19:** `DispatchMessage stalled for 172633 us!` - The main thread's message pump blocks for 172ms.
* **T20 - T38:** Continuous cycle of rapid focus flapping directly followed by `DispatchMessage stalled` warnings (ranging from 111ms to 185ms).

## PHASE 2: MMCSS REGISTRATION FAILURE
* **Locate:** `src/core/topology.cpp:128` inside `PinHookThread(L"Games")`.
* **Caller:** `WinMain` in `src/core/main.cpp:190`.
* **Thread Owner:** Main Thread.
* **Failure Path:** `AvSetMmThreadCharacteristicsW` returns `NULL`, and `GetLastError()` yields 1550 (`ERROR_NO_SUCH_LOGON_SESSION`).
* **Determination:** **CONFIGURATION ISSUE / HARMLESS WARNING**. 
Error 1550 in this context means the Multimedia Class Scheduler Service (MMCSS) is disabled on the host machine, or the "Games" profile is missing from the registry. It is not an engine bug. The code safely falls back to `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)`, making this a harmless environmental warning.

## PHASE 3: FOCUS FLAPPING
* **Producer:** The OS foreground window is genuinely changing. Events are caught by `EVENT_SYSTEM_FOREGROUND` in `WinEventProc` (running on `s_focusThread`) and passively polled by `KeyboardProc`/`MouseProc` (running on the main thread).
* **Target Resolver:** `target_platform::ResolveTargetAsync`.
* **Why Multiple PIDs?:** The PIDs are genuinely changing in the OS. The resolver successfully identifies multiple PIDs (e.g., 1744, 7996, 12728) as valid targets. This implies the user/script was rapidly alt-tabbing between multiple running instances of a target application (or multiple applications matching the `SDL_app` class), mixed with non-target background apps (like PID 8148).
* **Determination:** **IS IT REAL? YES**. This is not logging noise or a resolver bug. The OS foreground window was violently and repeatedly changing, likely due to an external stress-test script or rapid user interaction.

## PHASE 4: UI STALLS (DispatchMessage)
* **Source:** `src/core/main.cpp:310`
* **Measured By:** Main Thread (UI message pump).
* **Threshold:** `dispatchUs > 100000` (100ms).
* **Trace:** The main thread processes Low-Level input hooks (`KeyboardProc`/`MouseProc`) synchronously inside `DispatchMessage`. When the hook runs, it evaluates `IsTargetActive()`.
* **Determination:** **INSTRUMENTATION ARTIFACT / OVERLOADED PUMP**. 
During the massive focus flapping, `IsTargetActive()` logs `DLOG_WARN`. `DLOG_WARN` synchronously calls `OutputDebugStringA`. `OutputDebugStringA` is notoriously slow and locks a global OS mutex. Furthermore, `IsTargetActive()` acquires `s_focusMutex`, which creates extreme lock contention between the main thread (processing hooks) and the background `s_focusThread` (processing `EVENT_SYSTEM_FOREGROUND`). This cross-thread contention and heavy logging completely starves the main thread, causing `DispatchMessage` to stall for >100ms.

## PHASE 5: CAUSAL GRAPH
```text
[External OS Behavior]
Rapid Focus Flapping (Alt-Tab spam / Test script)
         |
         | (Triggers)
         v
[Cross-Thread Contention]
WinEventProc (s_focusThread) AND Keyboard/MouseProc (Main Thread) both spam IsTargetActive()
         |
         | (Causes)
         v
[Instrumentation Bottleneck]
Severe lock contention on s_focusMutex + synchronous OutputDebugStringA (DLOG_WARN) calls
         |
         | (Blocks)
         v
[Main Thread Starvation]
Keyboard/MouseProc execution is delayed by 100ms+, causing DispatchMessage to stall.

*Note: MMCSS failure is isolated and has NO causal relationship to the stalls or focus flapping.*
```

## PHASE 6: HYPOTHESIS ELIMINATION
* **Hypothesis:** MMCSS failure causes the UI stalls.
  * **REJECTED:** MMCSS gracefully falls back to standard real-time priorities. It does not block the message pump.
* **Hypothesis:** Focus flapping is a bug in the Marco resolver.
  * **REJECTED:** The resolver only reacts to `GetForegroundWindow()`. It does not inject focus changes. The OS was genuinely shifting focus.
* **Hypothesis:** `DispatchMessage` stalls are true UI starvation.
  * **SUPPORTED:** The main thread is genuinely blocked and cannot process Windows messages.
* **Hypothesis:** `DispatchMessage` stalls are caused by heavy logging/mutex contention during the focus flapping.
  * **SUPPORTED:** LL Hooks run on the main thread. When they call `IsTargetActive()` during a flap storm, they get blocked by `s_focusMutex` contention and the slow `OutputDebugStringA` system call, directly freezing `DispatchMessage`.

## FINAL VERDICT
**PRIMARY ROOT CAUSE IDENTIFIED**

**Summary:** 
* **First Event:** An external script or user rapidly cycled the OS foreground window across multiple applications.
* **Symptom:** `DispatchMessage stalled for 172633 us!`
* **Actual Cause:** Low-Level hooks (running on the main thread) and the Focus tracker (running on a background thread) aggressively contended for `s_focusMutex` and spammed `OutputDebugStringA` while trying to log the focus state changes, heavily blocking the main thread's message pump. The MMCSS error was a harmless, unrelated environmental warning.
