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
    std::lock_guard<std::mutex> operationLock(s_operationMutex);
    bool _doNotify = false;
    int ki_k = ki(k);
    DLOG_TRACE(Runtime, "OnTimerExpired: Executing release for %s", keymap::KeyName[ki_k]);
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyStateChanged(); } } _notifier{_doNotify};
    InjectionBatch batch;
    
#if MARCO_ENABLE_FORENSIC
    telemetry::g_timersExecuted.fetch_add(1, std::memory_order_relaxed);
#endif

    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        if (expectedTimerId == 0 || s_state.expectedTimerId[ki_k] != expectedTimerId) {
            DLOG_TRACE(Runtime, "OnTimerExpired: Ignoring stale callback for %s (expected=%llu, actual=%llu)",
                       keymap::KeyName[ki_k], s_state.expectedTimerId[ki_k], expectedTimerId);
#if MARCO_ENABLE_FORENSIC
            telemetry::ForensicEvent evRej = { telemetry::ForensicTrapType::TIMER_REJECTED, GetCurrentThreadId(), timing::NowUs(), (int32_t)ki_k, (uint32_t)(expectedTimerId & 0xFFFFFFFF), (uint32_t)(s_state.expectedTimerId[ki_k] & 0xFFFFFFFF), false };
            telemetry::g_forensicBuffer.Push(evRej);
#endif
            return;
        }
        batch.expectedTarget = s_state.expectedTimerTarget[ki_k];
        const Axis ax = keymap::KeyAxis[ki_k];
        const uint64_t timerId = s_state.expectedTimerId[ki_k];
        const bool releasingSocd = s_state.socdReleaseTimerId[ai(ax)] == timerId;
        s_state.expectedTimerId[ki_k] = 0;
        s_state.expectedTimerTarget[ki_k] = {};
        if (releasingSocd) {
            s_state.socdReleaseTimerId[ai(ax)] = 0;
            const Key opposite = keymap::Opposite[ki_k];
            if (rcfg::Get().socdMode == SocdMode::HUMANIZED && !s_state.suspended &&
                s_state.phys[ki_k] && s_state.phys[ki(opposite)] &&
                s_state.socdLastKey[ai(ax)] == opposite && s_state.logical[ki_k]) {
                // End the fixed overlap once, then lock last-key priority.
                batch.push(k, false);
                s_state.logical[ki_k] = false;
            }
        } else {
            // Release the injected counter key if it's not physically held
            if (s_state.logical[ki_k] && !s_state.phys[ki_k]) {
                s_state.logical[ki_k] = false;
                batch.push(k, false);
            }

            // Restore opposite key if it's physically held
            Key oppKey = keymap::Opposite[ki_k];
            int ki_opp = ki(oppKey);
            if (!s_state.phys[ki_k] && s_state.phys[ki_opp] && !s_state.logical[ki_opp]) {
                s_state.logical[ki_opp] = true;
                batch.push(oppKey, true);
            }
        }
        
        // Update axis state
        Key posK = keymap::AxisPosKey[ai(ax)];
        Key negK = keymap::AxisNegKey[ai(ax)];
        // axisState describes physical conflict, never temporary brake output.
        bool posL = s_state.phys[ki(posK)];
        bool negL = s_state.phys[ki(negK)];
        
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
    FlushAndCommitLogicalState(batch);
    _doNotify = true;
}

// â”€â”€ SYSTEM KEY HANDLERS â”€â”€

} // namespace engine
