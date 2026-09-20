# Phase 2: Deterministic Fire Timeline Rebuild

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modify the timer scheduling so that the Shot Dispatch timer is only scheduled *after* `InjectionBatch::flush()` has completed, removing the OS injection delay from the stabilization timing. Unify clock domains and add safety heuristics for competitive feel.

**Tech Stack:** C++, `autofire_controller.cpp`, `injection.cpp`, `timing.cpp`, `state_engine.cpp`.

---

### Task 1: Unify Clock Domains & Trace Constants

**Files:** `c:/Users/toanpq/Desktop/marco/src/core/timing.cpp`, `c:/Users/toanpq/Desktop/marco/src/core/injection.cpp`

- [ ] **Step 1: Clock Domain Audit & Unification**
Verify that `timing::NowUs()`, `timing::ScheduleTimerUs()`, and `TimerThreadFunc` wakeup mechanism use the SAME clock domain (QPC). `timing::NowUs()` is QPC-based. `ScheduleTimerUs` must compute absolute target time based on `NowUs()`. Ensure `TimerThreadFunc` uses `timing::NowUs()` to check if `expireUs` is reached.

- [ ] **Step 2: Update Trace Phases in `InjectionBatch::flush`**
In `src/core/engine_internal.h`:
Rename `BATCH_FLUSH_START` to `BATCH_FLUSH_BEGIN`.
Rename `BATCH_FLUSH_END` to `BATCH_FLUSH_COMPLETE`.

- [ ] **Step 3: Update Trace Phases in `injection.cpp`**
Change the `DLOG_INFO` traces added in Phase 1 to log BEFORE and AFTER `SendInput`:
```cpp
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_BEGIN key=...");
    UINT sent = SendInput(...);
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_END key=...");
```

---

### Task 2: Implement Post-Flush Absolute Deadline Scheduling

**Files:** `c:/Users/toanpq/Desktop/marco/src/core/autofire_controller.cpp`, `c:/Users/toanpq/Desktop/marco/src/core/state_engine.cpp`, `c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h`

- [ ] **Step 1: Change `InjectAutoFireBrake` signature**
```cpp
// Returns the stabilization duration (preFireUs), or 0 if immediate shot/no shot
int64_t InjectAutoFireBrake(Key heldKey, InjectionBatch& batch, std::function<void()>& outShotCallback);
```

- [ ] **Step 2: Implement Competitive Feel Safety**
Inside `InjectAutoFireBrake`, before scheduling the timer, evaluate if we need stabilization:
```cpp
    // Check if stabilization is actually needed
    bool needsStabilization = true;
    if (brakeUs < rcfg::Get().minStopMs * 1000LL) { // Or other competitive threshold
        needsStabilization = false; // shoot immediately if brakeUs is extremely small
    }
    // (Add any currentMagnitude checking if available in state)
    
    int64_t preFireUs = needsStabilization ? rcfg::Get().tapDelayMs * 1000LL : 0;
```
If `preFireUs == 0`, still return the callback so it can be dispatched immediately after flush.

- [ ] **Step 3: Defer Scheduling to `state_engine.cpp`**
```cpp
    std::function<void()> shotCallback;
    int64_t preFireUs = InjectAutoFireBrake(heldKey, batch, shotCallback);
    
    s_stateMutex.unlock();
    batch.flush(); // BATCH_FLUSH_BEGIN -> ... -> BATCH_FLUSH_COMPLETE
    
    // AFTER the flush
    if (shotCallback) {
        int64_t flushDoneUs = timing::NowUs();
        if (preFireUs > 0) {
            int64_t shotDeadlineUs = flushDoneUs + preFireUs;
            std::lock_guard<std::mutex> reLock(s_stateMutex);
            s_state.autoFire.timerId = timing::ScheduleTimerAtUs(Key::Mouse1, shotDeadlineUs, shotCallback);
            LOG_FIRE_TRACE("SHOT_TIMER_ARMED", s_state.autoFire.fireGenerationId);
        } else {
            // Immediate shot (competitive safety)
            LOG_FIRE_TRACE("SHOT_TIMER_ARMED", s_state.autoFire.fireGenerationId);
            shotCallback(); 
        }
    }
```
*Note: `timing::ScheduleTimerAtUs` needs to be implemented or we can calculate `remainingUs = preFireUs` since `flushDoneUs` is right now, so `ScheduleTimerUs(Key::Mouse1, preFireUs, ...)` achieves the same if internally it adds to `NowUs()` immediately.*

---

### Task 3: Assert Restore Never Precedes Shot

**Files:** `c:/Users/toanpq/Desktop/marco/src/core/autofire_controller.cpp`, `c:/Users/toanpq/Desktop/marco/src/core/timer_lifecycle.cpp`

- [ ] **Step 1: Track Shot Dispatch State**
In `AutoFireState`, add a boolean `hasDispatchedShot` (reset to false on new generation).

- [ ] **Step 2: Set Flag on Dispatch**
In `DispatchShot`, set `hasDispatchedShot = true`. Log `SHOT_DISPATCH`.

- [ ] **Step 3: Assert in Restore Phase**
When `RestoreMovement` is called, check if `hasDispatchedShot` is false.
```cpp
    if (!s_state.autoFire.hasDispatchedShot) {
        DLOG_ERR(Engine, "HARD WARNING: RESTORE_PHASE occurred BEFORE SHOT_DISPATCH on gen=%llu", s_state.autoFire.fireGenerationId);
        // Log generation dump
        engine::LogFireTrace("RESTORE_OVERLAP_VIOLATION", s_state.autoFire.fireGenerationId);
    }
```

---

### Task 4: Forensic Reporting & Regression

- [ ] **Step 1:** Run `make debug`, `make release`, `make profile`.
- [ ] **Step 2:** Run `run_trace.bat`.
- [ ] **Step 3:** Generate `docs/reports/V26_1_FIRE_TIMELINE_FORENSICS.md` documenting the new architecture, latencies, and competitive safety rules.
- [ ] **Step 4:** Git commit and push.
