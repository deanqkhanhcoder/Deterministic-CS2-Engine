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
            if (!s_state.autoFire.active || s_state.autoFire.expectedShotId != expectedTimerId) {
                return; // Stale or cancelled shot
            }
        }
        
        // Inject the mouse click!
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input.mi.dwExtraInfo = 0x1337BEEF; // Mark as injected so we don't swallow it again
        SendInput(1, &input, sizeof(INPUT));
        DLOG_TRACE(Runtime, "AutoFire Executed: Left Down injected");
        
        // Restore keys
        CancelPendingShot();
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
        if (s_state.logical[ki_k] && !s_state.phys[ki_k]) {
            s_state.logical[ki_k] = false;
            batch.push(k, false);
        }
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
}

// ── SYSTEM KEY HANDLERS ──

} // namespace engine
