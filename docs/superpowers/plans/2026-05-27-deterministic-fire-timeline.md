# Deterministic Fire Timeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the AutoFire timeline ordering to guarantee that shot stabilization and counter-strafe timers only start ticking *after* the OS has fully processed the physical brake inputs.

**Architecture:** Defer the `timing::ScheduleTimerUs` calls until after `batch.flush()` executes in `OnLButtonDown()`. By caching the required timer parameters (keys and delays) locally inside the function, flushing the physical brake inputs via `SendInput`, and only then locking the state mutex to schedule the timers, we eliminate OS injection latency from the stabilization equation.

**Tech Stack:** C++, C++11 Mutexes, Counter-Strafe State Engine.

---

### Task 1: Refactor `OnLButtonDown` Timeline Ordering

**Files:**
- Modify: `c:/Users/toanpq/Desktop/marco/src/core/autofire_controller.cpp`

- [ ] **Step 1: Extract timer scheduling logic into a deferred queue**

```cpp
bool OnLButtonDown() {
    int64_t maxBrakeUs = 0;
    
    // Struct to hold deferred timers
    struct PendingTimer {
        Key k;
        int64_t delayUs;
    };
    PendingTimer pendingTimers[3]; // Max 2 axis brakes + 1 shot
    int pendingTimerCount = 0;

    bool _doNotify = false;
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};
    InjectionBatch batch;
    
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        
        if (s_state.autoFire.state != FireState::Idle) return false;
        
        int64_t nowMs = timing::NowMs();
        const RuntimeConfig& rc = rcfg::Get();
        
        if (nowMs - s_state.lastSpaceTimeMs < rc.spaceDelayMs) return false;
        s_state.PruneClicks(nowMs);
        s_state.PushClick(nowMs);
        s_state.TrimClicks(rc.burstThreshold);
        
        int effectiveDelay = (s_state.clickHistoryCount >= rc.burstThreshold) ? rc.sprayDelayMs : rc.tapDelayMs;
        if (nowMs - s_state.lastCounterMs < effectiveDelay) return false;
        s_state.lastCounterMs = nowMs;
        
        s_state.autoFire.fireGenerationId++;

        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    int64_t brakeUs = InjectAutoFireBrake(key, batch);
                    if (brakeUs > 0) {
                        Key counterKey = (ax == Axis::Y) ? (i == 0 ? Key::S : Key::W) : (i == 0 ? Key::D : Key::A);
                        // DEFER TIMER
                        pendingTimers[pendingTimerCount++] = {counterKey, brakeUs};
                    }
                    if (brakeUs > maxBrakeUs) maxBrakeUs = brakeUs;
                }
            }
        };
        
        applyBrake(Axis::Y); applyBrake(Axis::X);
        
        if (maxBrakeUs > 0) {
            s_state.autoFire.state = FireState::Stabilizing;
            
            int64_t preFireUs = 15625 + (int64_t)(rc.subtickPaddingTicks * 15625.0);
            if (maxBrakeUs < preFireUs) preFireUs = maxBrakeUs; 
            
            // DEFER SHOT TIMER
            pendingTimers[pendingTimerCount++] = {Key::Mouse1, preFireUs};
        }
        PublishEngineState();
    }
    
    // [CRITICAL TIMELINE FIX]
    // Flush the physical inputs to the OS FIRST, before we start any timers.
    // This ensures OS injection latency does not eat into our subtick budget.
    batch.flush();
    _doNotify = true;
    
    // Now schedule the deferred timers
    if (pendingTimerCount > 0) {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        // Only schedule if a physical release didn't cancel us while we were flushing!
        if (s_state.autoFire.state == FireState::Stabilizing) {
            for (int i = 0; i < pendingTimerCount; ++i) {
                const auto& pt = pendingTimers[i];
                uint64_t id = timing::ScheduleTimerUs(pt.k, pt.delayUs);
                if (pt.k == Key::Mouse1) {
                    s_state.autoFire.expectedShotId = id;
                    LOG_FIRE_TRACE("SHOT_SCHEDULED", s_state.autoFire.fireGenerationId);
                    DLOG_TRACE(Runtime, "AutoFire Scheduled: %lld us (Brake total: %lld us)", pt.delayUs, maxBrakeUs);
                } else {
                    s_state.expectedTimerId[ki(pt.k)] = id;
                }
            }
        }
    }
    
    return (maxBrakeUs > 0);
}
```

- [ ] **Step 2: Commit**

```bash
git add src/core/autofire_controller.cpp
git commit -m "fix: rebuild deterministic fire timeline by scheduling timers post-flush"
```
