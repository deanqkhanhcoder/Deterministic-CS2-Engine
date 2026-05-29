#include "engine_internal.h"
#include "movement_reconstruction.h"
#include "runtime_config.h"
#include "timing.h"
#include "telemetry.h"

namespace engine {

void CancelPendingShot() {}

bool OnLButtonDown() {
    bool _doNotify = false;
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};
    InjectionBatch batch;
    
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        
        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    Key counterKey = (ax == Axis::Y) ? (i == 0 ? Key::S : Key::W) : (i == 0 ? Key::D : Key::A);
                    int ki_c = ki(counterKey);
                    
                    if (s_state.phys[ki_c]) continue;
                    
                    int64_t heldUs = timing::NowUs() - s_state.downTimeUs[ki_k];
                    int64_t brakeUs = CalculateTrueBrakeUs(key, ax, heldUs);
                    
                    if (brakeUs > 0) {
                        batch.push(counterKey, true);
                        s_state.logical[ki_c] = true;
                        timing::ScheduleTimerUs(counterKey, brakeUs);
                    }
                }
            }
        };
        applyBrake(Axis::Y); applyBrake(Axis::X);
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
    
    return false; // NEVER swallow the actual mouse click
}

void OnLButtonUp() {}

} // namespace engine
