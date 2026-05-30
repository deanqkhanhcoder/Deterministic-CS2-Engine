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

void HandleKeyDown(Key k, bool routeSemantic) {

    int64_t startUs = timing::NowUs();
    struct ScopedTrace {
        int64_t startUs;
        Key k;
        ~ScopedTrace() {
#if MARCO_ENABLE_FORENSIC
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(4, 0, (int32_t)(timing::NowUs() - startUs), (int32_t)k, 1); // Op = 1 (KeyDown)
            }
#endif
        }
    } tracer{startUs, k};

    int ki_k = ki(k);
    int64_t nowUs = startUs;
    int64_t nowMs = nowUs / 1000;
    Axis ax = keymap::KeyAxis[ki_k];

    DLOG_TRACE(Runtime, "HandleKeyDown: %s (routeSemantic=%d)", reinterpret_cast<int64_t>(keymap::KeyName[ki_k]), routeSemantic);

    bool _doNotify = false;
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);

        // --- PHYSICAL LAYER (Always Tracked) ---
        if (s_state.phys[ki_k]) return; // Already physically down
        s_state.phys[ki_k]     = true;
        s_state.downTimeUs[ki_k] = nowUs;

        timing::CancelTimer(k);
        s_state.expectedTimerId[ki(k)] = 0;

        // --- SEMANTIC ROUTING LAYER ---
        if (!routeSemantic) return;

        // Tap spam EMA decay
        int64_t lastDecayMs = s_state.mem.tapSpamLastDecayTimeMs[ki_k];
        if (lastDecayMs > 0) {
            int64_t elapsedMs = nowMs - lastDecayMs;
            double decayFactor = std::exp(-0.693 * (double)elapsedMs / (double)cfg_rt::TAP_SPAM_HALF_LIFE_MS());
            s_state.mem.tapSpamPenalty[ki_k] *= decayFactor;
        }
        s_state.mem.tapSpamLastDecayTimeMs[ki_k] = nowMs;

        // Spam detection
        int64_t lastTap = s_state.mem.tapSpamLastTimeMs[ki_k];
        if (lastTap > 0 && (nowMs - lastTap) < cfg_rt::TAP_SPAM_WINDOW_MS()) {
            s_state.mem.tapSpamPenalty[ki_k] =
                std::min(1.0, s_state.mem.tapSpamPenalty[ki_k] * (1.0 + cfg_rt::TAP_SPAM_ALPHA()) + cfg_rt::TAP_SPAM_ALPHA());
        }
        s_state.mem.tapSpamLastTimeMs[ki_k] = nowMs;

        // Walk tracking
        s_state.walk.accumUs[ki_k]   = 0;
        s_state.walk.startTimeUs[ki_k] = s_state.walk.shiftDown ? nowUs : 0;

        ResolveAxis(ax, batch);

        AxisState curState = s_state.axisState[ai(ax)];
        if (curState != AxisState::Conflict) {
            batch.push(k, true);
            s_state.logical[ki_k] = true;
        }
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  HANDLE KEY UP  (Â§16)
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
void HandleKeyUp(Key k, bool routeSemantic) {

    int64_t startUs = timing::NowUs();
    struct ScopedTrace {
        int64_t startUs;
        Key k;
        ~ScopedTrace() {
#if MARCO_ENABLE_FORENSIC
            if (telemetry::IsEnabled()) {
                telemetry::g_eventBuffer.Push(4, 0, (int32_t)(timing::NowUs() - startUs), (int32_t)k, 0); // Op = 0 (KeyUp)
            }
#endif
        }
    } tracer{startUs, k};

    int ki_k  = ki(k);
    int64_t nowUs = startUs;
    int64_t nowMs = nowUs / 1000;
    Axis ax   = keymap::KeyAxis[ki_k];
    Key oppK  = keymap::Opposite[ki_k];
    int ki_opp = ki(oppK);

    DLOG_TRACE(Runtime, "HandleKeyUp: %s (routeSemantic=%d)", reinterpret_cast<int64_t>(keymap::KeyName[ki_k]), routeSemantic);

    bool _doNotify = false;
    struct _Notifier { bool& n; ~_Notifier() { if(n) NotifyUI(); } } _notifier{_doNotify};

    InjectionBatch batch;
    {
        // --- PHYSICAL LAYER (Always Tracked) ---
        std::lock_guard<std::mutex> lock(s_stateMutex);

        if (!s_state.phys[ki_k]) {
            if (routeSemantic) {
                s_state.walk.startTimeUs[ki_k] = 0;
                ResolveAxis(ax, batch);
            }
        } else {
            int64_t heldUs         = nowUs - s_state.downTimeUs[ki_k];
            s_state.heldDurUs[ki_k] = heldUs;
            s_state.phys[ki_k]     = false;
            s_state.downTimeUs[ki_k] = 0;

            // --- SEMANTIC ROUTING LAYER ---
            if (routeSemantic) {
                bool wasInConflict = (s_state.axisState[ai(ax)] == AxisState::Conflict);
                ReleaseReason reason = wasInConflict ? ReleaseReason::Conflict : ReleaseReason::Normal;

                if (s_state.walk.shiftDown && s_state.walk.startTimeUs[ki_k] > 0)
                    s_state.walk.accumUs[ki_k] += nowUs - s_state.walk.startTimeUs[ki_k];
                s_state.walk.startTimeUs[ki_k] = 0;

                if (reason == ReleaseReason::Normal) {
                    s_state.mem.lastReleaseTimeMs[ai(ax)] = nowMs;
                    s_state.mem.lastHoldUs[ai(ax)] = heldUs;
                }

                bool strafed = false;
                if (reason == ReleaseReason::Normal && !s_state.phys[ki_opp]) {
                    strafed = AutoCounterStrafe(k, oppK, ax, heldUs, batch);
                }

                if (!strafed && s_state.logical[ki_k]) {
                    batch.push(k, false);
                    s_state.logical[ki_k] = false;
                }

                ResolveAxis(ax, batch);
            }
        }
        PublishEngineState();
    }
    batch.flush();
    _doNotify = true;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RESOLVE AXIS
void OnSysKeyChange(bool isLCtrl, bool down, bool routeSemantic) {
    // PHYSICAL TRUTH
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        if (isLCtrl) s_state.sysLCtrl = down;
        else         s_state.sysC     = down;
        PublishEngineState();
    }
    
    if (!routeSemantic) return;
    NotifyUI();
}

void OnShiftChange(bool down, bool routeSemantic) {
    // PHYSICAL TRUTH
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        if (down) {
            if (!s_state.walk.shiftDown) {
                s_state.walk.shiftDown = true;
                int64_t nowUs = timing::NowUs();
                for (int i = 0; i < 4; ++i) if (s_state.phys[i] && s_state.walk.startTimeUs[i] == 0) s_state.walk.startTimeUs[i] = nowUs;
            }
        } else {
            if (s_state.walk.shiftDown) {
                s_state.walk.shiftDown = false;
                s_state.walk.shiftReleaseTimeMs = timing::NowMs();
                int64_t nowUs = timing::NowUs();
                for (int i = 0; i < 4; ++i) if (s_state.phys[i] && s_state.walk.startTimeUs[i] > 0) {
                    s_state.walk.accumUs[i] += nowUs - s_state.walk.startTimeUs[i];
                    s_state.walk.startTimeUs[i] = 0;
                }
            }
        }
        PublishEngineState();
    }
    
    if (!routeSemantic) return;
    NotifyUI();
}

void OnSpaceDown(bool routeSemantic) {

    // PHYSICAL TRUTH
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        s_state.spacePhys = true;
        if (routeSemantic) {
            
        }
        PublishEngineState();
    }
    NotifyUI();
}

void OnSpaceUp() {
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        s_state.spacePhys = false;
        PublishEngineState();
    }
    NotifyUI();
}

} // namespace engine
