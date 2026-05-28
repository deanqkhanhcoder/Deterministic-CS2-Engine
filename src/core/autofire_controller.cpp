#include "engine_internal.h"
#include "movement_reconstruction.h"
#include "runtime_config.h"
#include "bhop.h"
#include "telemetry.h"
#include "timing.h"
#include "config.h"
#include "debug_logger.h"
#include "input_capture.h"
#include <cmath>
#include <algorithm>

#define RC (rcfg::Get())

namespace engine {


BrakeResult InjectAutoFireBrake(Key heldKey, InjectionBatch& batch) {
    int ki_h = ki(heldKey);
    Key counterKey = keymap::Opposite[ki_h];
    int ki_c = ki(counterKey);
    Axis ax = keymap::KeyAxis[ki_h];
    
    if (s_state.phys[ki_c]) return {0, 0};
    if (s_state.axisState[ai(ax)] == AxisState::Conflict) return {0, 0};
    
    const RuntimeConfig& rc = rcfg::Get();
    int64_t heldUs = timing::NowUs() - s_state.downTimeUs[ki_h];
    if (heldUs < rc.minTapUs) return {0, 0};
    
    int64_t effectiveBrakeUs = CalculateTrueBrakeUs(heldKey, ax, heldUs);
    
    // TRUE TEMPORARY TAKEOVER:
    // 1. Logical release of the originally held key
    batch.push(heldKey, false);
    s_state.logical[ki_h] = false;
    s_state.autoFire.suspendedMovementMask |= (1 << ki_h);
    
    // 2. Inject counter key logically
    batch.push(counterKey, true);
    s_state.logical[ki_c] = true;
    s_state.autoFire.injectedCounterMask |= (1 << ki_c);
    
    s_state.axisState[ai(ax)] = (counterKey == keymap::AxisPosKey[ai(ax)]) ? AxisState::Positive : AxisState::Negative;
    
    LOG_FIRE_TRACE("BRAKE_INJECT", s_state.autoFire.fireGenerationId);
    
    bool needsStabilization = true;
    if (effectiveBrakeUs < rcfg::Get().minStopMs * 1000LL) {
        needsStabilization = false; 
    }
    int64_t preFireUs = needsStabilization ? rcfg::Get().tapDelayMs * 1000LL : 0;
    
    return {effectiveBrakeUs, preFireUs};
}

void CancelPendingShotLocked(InjectionBatch& batch) {
    if (s_state.autoFire.state != FireState::Idle) {
        bool needsMouse1 = false;
        if (s_state.autoFire.state == FireState::Stabilizing) {
            timing::CancelTimer(Key::Mouse1);
            needsMouse1 = true;
        }
        s_state.autoFire.state = FireState::Idle;
        
        if (needsMouse1) {
            // Push mouse event to batch instead of SendInput!
            batch.pushEvent(true, Key::Mouse1, true);
            s_state.autoFire.hasDispatchedShot = true;
        }
        
        // 1. Release injected counter keys
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.injectedCounterMask & (1 << i)) {
                batch.push((Key)i, false);
                s_state.logical[i] = false;
                timing::CancelTimer((Key)i);
            }
        }
        
        // 2. Restore suspended keys IF STILL PHYSICALLY HELD
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.suspendedMovementMask & (1 << i)) {
                if (s_state.phys[i]) {
                    batch.push((Key)i, true);
                    s_state.logical[i] = true;
                }
            }
        }
        
        // 3. Re-evaluate axis states
        for (int ax = 0; ax < 2; ++ax) {
            Key posK = keymap::AxisPosKey[ax];
            Key negK = keymap::AxisNegKey[ax];
            bool posL = s_state.logical[ki(posK)];
            bool negL = s_state.logical[ki(negK)];
            
            if (posL && negL) {
                s_state.axisState[ax] = AxisState::Conflict;
            } else if (posL) {
                s_state.axisState[ax] = AxisState::Positive;
            } else if (negL) {
                s_state.axisState[ax] = AxisState::Negative;
            } else {
                s_state.axisState[ax] = AxisState::None;
            }
        }
        
        s_state.autoFire.suspendedMovementMask = 0;
        s_state.autoFire.injectedCounterMask = 0;
        
        if (!s_state.autoFire.hasDispatchedShot) {
            DLOG_ERR(Runtime, "HARD WARNING: RESTORE_PHASE occurred BEFORE SHOT_DISPATCH on gen=%llu", s_state.autoFire.fireGenerationId);
            // Log generation dump
            engine::LogFireTrace("RESTORE_OVERLAP_VIOLATION", s_state.autoFire.fireGenerationId);
        }

        LOG_FIRE_TRACE("RESTORE_PHASE", s_state.autoFire.fireGenerationId);
    }
}

void CancelPendingShot() {
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        CancelPendingShotLocked(batch);
        PublishEngineState();
    }
    batch.flush();
}

void OnLButtonUp() {
    CancelPendingShot();
}

bool OnLButtonDown() {
    int64_t nowMs = timing::NowMs();
    
    bool _doNotify = false;
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};
    InjectionBatch batch;
    
    int64_t maxBrakeUs = 0;
    int64_t maxPreFireUs = 0;
    bool needsShot = false;
    
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        s_state.autoFire.stats.m1_down_us = timing::NowUs(); // Exact microsecond timestamp
        s_state.autoFire.stats.batch_flush_begin_us = 0;
        s_state.autoFire.stats.batch_flush_end_us = 0;
        s_state.autoFire.stats.shot_scheduled_us = 0;
        s_state.autoFire.stats.expected_deadline_us = 0;
        s_state.autoFire.stats.timer_wake_actual_us = 0;
        
        // If already waiting for a shot, cancel the old one and re-evaluate
        if (s_state.autoFire.state != FireState::Idle) {
            CancelPendingShotLocked(batch);
        }

        const RuntimeConfig& rc = rcfg::Get();
        if (nowMs - s_state.lastSpaceTimeMs < rc.spaceDelayMs) return false;
        s_state.PruneClicks(nowMs);
        s_state.PushClick(nowMs);
        s_state.TrimClicks(rc.burstThreshold);
        int effectiveDelay = (s_state.clickHistoryCount >= rc.burstThreshold) ? rc.sprayDelayMs : rc.tapDelayMs;
        if (nowMs - s_state.lastCounterMs < effectiveDelay) return false;
        s_state.lastCounterMs = nowMs;
        
        // Start a new fire generation for tracking
        s_state.autoFire.fireGenerationId++;
        s_state.autoFire.hasDispatchedShot = false;
        LOG_FIRE_TRACE("M1_PHYSICAL_DOWN", s_state.autoFire.fireGenerationId);

        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    BrakeResult res = InjectAutoFireBrake(key, batch);
                    if (res.effectiveBrakeUs > 0) {
                        needsShot = true;
                        if (res.effectiveBrakeUs > maxBrakeUs) maxBrakeUs = res.effectiveBrakeUs;
                        if (res.preFireUs > maxPreFireUs) maxPreFireUs = res.preFireUs;
                        
                        Key counterKey = (ax == Axis::Y) ? (i == 0 ? Key::S : Key::W) : (i == 0 ? Key::D : Key::A);
                        s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerUs(counterKey, res.effectiveBrakeUs);
                    }
                }
            }
        };
        applyBrake(Axis::Y); applyBrake(Axis::X);
        
        if (needsShot) {
            s_state.autoFire.state = FireState::Stabilizing;
        }
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
    
    // AFTER the flush
    if (needsShot) {
        int64_t flushDoneUs = timing::NowUs();
        
        uint64_t currentGen = 0;
        {
            std::lock_guard<std::mutex> reLock(s_stateMutex);
            currentGen = s_state.autoFire.fireGenerationId;
        }

        auto shotCallback = [currentGen]() {
            bool shouldFire = false;
            {
                std::lock_guard<std::mutex> lock(s_stateMutex);
                if (s_state.autoFire.state == FireState::Stabilizing && 
                    s_state.autoFire.fireGenerationId == currentGen) {
                    
                    s_state.autoFire.state = FireState::Fired;
                    s_state.autoFire.hasDispatchedShot = true;
                    shouldFire = true;
                }
            }
            if (shouldFire) {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                input.mi.dwExtraInfo = 0x1337BEEF; // Mark as injected
                
                int64_t preSyscall = timing::NowUs();
                SendInput(1, &input, sizeof(INPUT));
                int64_t postSyscall = timing::NowUs();
                
                int64_t totalLatency = postSyscall - s_state.autoFire.stats.m1_down_us;
                
                LOG_FIRE_TRACE("SHOT_DISPATCH", currentGen);
            }
        };

        if (maxPreFireUs > 0) {
            int64_t computedPreFireUs = 15625 + (int64_t)(rcfg::Get().subtickPaddingTicks * 15625.0);
            if (maxPreFireUs < computedPreFireUs) computedPreFireUs = maxPreFireUs; 
            int64_t shotDeadlineUs = flushDoneUs + computedPreFireUs;
            if (shotDeadlineUs < timing::NowUs()) {
                shotDeadlineUs = timing::NowUs();
            }
            
            std::lock_guard<std::mutex> reLock(s_stateMutex);
            if (s_state.autoFire.fireGenerationId == currentGen && s_state.autoFire.state == FireState::Stabilizing) {
                s_state.autoFire.stats.expected_deadline_us = shotDeadlineUs;
                s_state.autoFire.stats.shot_scheduled_us = timing::NowUs();
                s_state.autoFire.expectedShotId = timing::ScheduleTimerAtUs(Key::Mouse1, shotDeadlineUs, shotCallback);
                LOG_FIRE_TRACE("SHOT_TIMER_ARMED", currentGen);
            }
        } else {
            // Immediate shot (competitive safety)
            std::lock_guard<std::mutex> reLock(s_stateMutex);
            s_state.autoFire.stats.expected_deadline_us = timing::NowUs();
            s_state.autoFire.stats.shot_scheduled_us = timing::NowUs();
            LOG_FIRE_TRACE("SHOT_TIMER_IMMEDIATE", currentGen);
            shotCallback(); 
        }
    }
    
    return needsShot;
}

} // namespace engine
