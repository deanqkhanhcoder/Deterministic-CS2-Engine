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

int64_t InjectAutoFireBrake(Key heldKey, InjectionBatch& batch) {
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
    
    return effectiveBrakeUs;
}

void CancelPendingShotLocked(InjectionBatch& batch) {
    if (s_state.autoFire.state != FireState::Idle) {
        if (s_state.autoFire.state == FireState::Stabilizing) {
            timing::CancelTimer(Key::Mouse1);
        }
        s_state.autoFire.state = FireState::Idle;
        
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

        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    int64_t brakeUs = InjectAutoFireBrake(key, batch);
                    if (brakeUs > 0) {
                        Key counterKey = (ax == Axis::Y) ? (i == 0 ? Key::S : Key::W) : (i == 0 ? Key::D : Key::A);
                        s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerUs(counterKey, brakeUs);
                    }
                    if (brakeUs > maxBrakeUs) maxBrakeUs = brakeUs;
                }
            }
        };
        applyBrake(Axis::Y); applyBrake(Axis::X);
        
        if (maxBrakeUs > 0) {
            s_state.autoFire.state = FireState::Stabilizing;
            
            // [FIX BUG #2] Pre-fire Subtick Stabilization
            // Instead of delaying the shot by full maxBrakeUs (sluggish/cắm đất),
            // wait exactly 1 subtick quantum for crosshair stabilization.
            int64_t preFireUs = 15625 + (int64_t)(rc.subtickPaddingTicks * 15625.0);
            if (maxBrakeUs < preFireUs) preFireUs = maxBrakeUs; // don't delay longer than brake
            
            s_state.autoFire.expectedShotId = timing::ScheduleTimerUs(Key::Mouse1, preFireUs);
            DLOG_TRACE(Runtime, "AutoFire Scheduled: %lld us (Brake total: %lld us)", preFireUs, maxBrakeUs);
        }
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
    
    return (maxBrakeUs > 0);
}

} // namespace engine
