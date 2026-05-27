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

void OnTimerExpired(Key k, uint64_t expectedTimerId) {
    if (k == Key::Mouse1) {
        {
            std::lock_guard<std::mutex> lock(s_stateMutex);
            if (s_state.autoFire.state != FireState::Stabilizing || s_state.autoFire.expectedShotId != expectedTimerId) {
                return; // Stale or cancelled shot
            }
            s_state.autoFire.state = FireState::Fired;
            s_state.autoFire.hasDispatchedShot = true;
            LOG_FIRE_TRACE("SHOT_DISPATCH", s_state.autoFire.fireGenerationId);
        }
        
        // Inject the mouse click!
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input.mi.dwExtraInfo = 0x1337BEEF; // Mark as injected so we don't swallow it again
        SendInput(1, &input, sizeof(INPUT));
        DLOG_TRACE(Runtime, "AutoFire Executed: Left Down injected (1 quantum delay)");
        
        // [FIX BUG #2] DO NOT CALL CancelPendingShot() HERE
        // The brake routine needs to continue running until maxBrakeUs completes.
        // Movement will be restored naturally when the counter keys expire below.
        return;
    }

    bool _doNotify = false;
    int ki_k = ki(k);
    DLOG_TRACE(Runtime, "OnTimerExpired: Executing release for %s", reinterpret_cast<int64_t>(keymap::KeyName[ki_k]));
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};
    InjectionBatch batch;
    
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        if (expectedTimerId != 0 && s_state.expectedTimerId[ki_k] != expectedTimerId) {
            DLOG_TRACE(Runtime, "OnTimerExpired: Ignoring stale callback for %s (expected=%llu, actual=%llu)",
                       reinterpret_cast<int64_t>(keymap::KeyName[ki_k]), s_state.expectedTimerId[ki_k], expectedTimerId);
            return;
        }
        
        // Release logical key
        if (s_state.logical[ki_k] && !s_state.phys[ki_k]) {
            s_state.logical[ki_k] = false;
            batch.push(k, false);
        }
        
        // [FIX BUG #2] Restore movement if this was the last counter key of a fired shot
        if (s_state.autoFire.state == FireState::Fired) {
            if (s_state.autoFire.injectedCounterMask & (1 << ki_k)) {
                s_state.autoFire.injectedCounterMask &= ~(1 << ki_k);
                
                // If all counter keys have finished braking, restore originally held movement keys
                if (s_state.autoFire.injectedCounterMask == 0) {
                    for (int i = 0; i < 4; ++i) {
                        if (s_state.autoFire.suspendedMovementMask & (1 << i)) {
                            if (s_state.phys[i]) {
                                batch.push(static_cast<Key>(i), true);
                                s_state.logical[i] = true;
                            }
                        }
                    }
                    s_state.autoFire.suspendedMovementMask = 0;
                    s_state.autoFire.state = FireState::Restoring;
                    LOG_FIRE_TRACE("MOVEMENT_RESTORE", s_state.autoFire.fireGenerationId);
                    DLOG_TRACE(Runtime, "AutoFire Brake Finished: Movement Restored");
                }
            }
        }
        
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
}

// ── SYSTEM KEY HANDLERS ──

} // namespace engine
