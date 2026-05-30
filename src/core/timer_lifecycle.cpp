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
    bool _doNotify = false;
    int ki_k = ki(k);
    DLOG_TRACE(Runtime, "OnTimerExpired: Executing release for %s", reinterpret_cast<int64_t>(keymap::KeyName[ki_k]));
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};
    InjectionBatch batch;
    
#if MARCO_ENABLE_FORENSIC
    telemetry::g_timersExecuted.fetch_add(1, std::memory_order_relaxed);
#endif

    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        if (expectedTimerId != 0 && s_state.expectedTimerId[ki_k] != expectedTimerId) {
            DLOG_TRACE(Runtime, "OnTimerExpired: Ignoring stale callback for %s (expected=%llu, actual=%llu)",
                       reinterpret_cast<int64_t>(keymap::KeyName[ki_k]), s_state.expectedTimerId[ki_k], expectedTimerId);
#if MARCO_ENABLE_FORENSIC
            telemetry::ForensicEvent evRej = { telemetry::ForensicTrapType::TIMER_REJECTED, GetCurrentThreadId(), timing::NowUs(), (int32_t)ki_k, (uint32_t)(expectedTimerId & 0xFFFFFFFF), (uint32_t)(s_state.expectedTimerId[ki_k] & 0xFFFFFFFF), false };
            telemetry::g_forensicBuffer.Push(evRej);
#endif
            return;
        }
        
        // Release the injected counter key if it's not physically held
        if (s_state.logical[ki_k] && !s_state.phys[ki_k]) {
            s_state.logical[ki_k] = false;
            batch.push(k, false);
        }
        
        // Restore opposite key if it's physically held
        Key oppKey = keymap::Opposite[ki_k];
        int ki_opp = ki(oppKey);
        if (s_state.phys[ki_opp] && !s_state.logical[ki_opp]) {
            s_state.logical[ki_opp] = true;
            batch.push(oppKey, true);
        }
        
        // Update axis state
        Axis ax = keymap::KeyAxis[ki_k];
        Key posK = keymap::AxisPosKey[ai(ax)];
        Key negK = keymap::AxisNegKey[ai(ax)];
        bool posL = s_state.logical[ki(posK)];
        bool negL = s_state.logical[ki(negK)];
        
        if (posL && negL) {
            s_state.axisState[ai(ax)] = AxisState::Conflict;
        } else if (posL) {
            s_state.axisState[ai(ax)] = AxisState::Positive;
        } else if (negL) {
            s_state.axisState[ai(ax)] = AxisState::Negative;
        } else {
            s_state.axisState[ai(ax)] = AxisState::None;
        }
        
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
}

// â”€â”€ SYSTEM KEY HANDLERS â”€â”€

} // namespace engine
