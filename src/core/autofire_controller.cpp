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

int64_t InjectAutoFireBrake(Key heldKey, InjectionBatch& batch, std::function<void()>& outShotCallback) {
    int ki_h = ki(heldKey);
    Key counterKey = keymap::Opposite[ki_h];
    int ki_c = ki(counterKey);
    Axis ax = keymap::KeyAxis[ki_h];
    
    if (s_state.phys[ki_c]) return 0;
    if (s_state.axisState[ai(ax)] == AxisState::Conflict) return 0;
    
    const RuntimeConfig& rc = rcfg::Get();
    int64_t heldUs = timing::NowUs() - s_state.downTimeUs[ki_h];
    if (heldUs < rc.minTapUs) return 0;
    
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
    
    outShotCallback = []() {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input.mi.dwExtraInfo = 0x1337BEEF; // Mark as injected
        SendInput(1, &input, sizeof(INPUT));
        {
            std::lock_guard<std::mutex> lock(s_stateMutex);
            s_state.autoFire.hasDispatchedShot = true;
        }
    };
    
    if (preFireUs == 0) {
        return 0;
    }
    
    return preFireUs;
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
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            input.mi.dwExtraInfo = 0x1337BEEF; // Mark as injected
            SendInput(1, &input, sizeof(INPUT));
            s_state.autoFire.hasDispatchedShot = true;
        }
        
        // 1. Release injected counter keys
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.injectedCounterMask & (1 << i)) {
                batch.push((Key)i, false);
                s_state.logical[i] = false;
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
        DLOG_TRACE(Runtime, "AutoFire Cancelled & Movement Restored");
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
    std::function<void()> shotCallback;
    
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        
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

        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    std::function<void()> cb;
                    int64_t brakeUs = InjectAutoFireBrake(key, batch, cb);
                    if (cb) shotCallback = cb;
                    if (brakeUs > 0) {
                        Key counterKey = (ax == Axis::Y) ? (i == 0 ? Key::S : Key::W) : (i == 0 ? Key::D : Key::A);
                        s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerUs(counterKey, brakeUs);
                    }
                    if (brakeUs > maxBrakeUs) maxBrakeUs = brakeUs;
                }
            }
        };
        applyBrake(Axis::Y); applyBrake(Axis::X);
        
        if (maxBrakeUs > 0 || shotCallback) {
            s_state.autoFire.state = FireState::Stabilizing;
        }
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
    
    // AFTER the flush
    if (shotCallback) {
        int64_t flushDoneUs = timing::NowUs();
        if (maxBrakeUs > 0) {
            int64_t preFireUs = 15625 + (int64_t)(rcfg::Get().subtickPaddingTicks * 15625.0);
            if (maxBrakeUs < preFireUs) preFireUs = maxBrakeUs; // don't delay longer than brake
            int64_t shotDeadlineUs = flushDoneUs + preFireUs;
            std::lock_guard<std::mutex> reLock(s_stateMutex);
            s_state.autoFire.expectedShotId = timing::ScheduleTimerAtUs(Key::Mouse1, shotDeadlineUs, shotCallback);
            LOG_FIRE_TRACE("SHOT_TIMER_ARMED", s_state.autoFire.fireGenerationId);
        } else {
            // Immediate shot (competitive safety)
            LOG_FIRE_TRACE("SHOT_TIMER_ARMED", s_state.autoFire.fireGenerationId);
            shotCallback(); 
        }
    }
    
    return (maxBrakeUs > 0);
}

} // namespace engine
