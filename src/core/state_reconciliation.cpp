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

void ToggleSuspend() {

    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        s_state.suspended = !s_state.suspended;
        s_suspendedAtomic.store(s_state.suspended, std::memory_order_release);  // Atomic publish
        DLOG_WARN(Runtime, s_state.suspended ? ">>> SUSPENDED <<<" : ">>> RESUMED <<<");
        if (s_state.suspended) {
            ReconcileInternal(true, batch);
        }
        PublishEngineState();
    }
    batch.flush();
    if (!s_suspendedAtomic.load(std::memory_order_acquire)) {
        // Reinstall hooks if they were uninstalled by watchdog/fail-safe
        capture::Reinstall();
        RebuildState();
    }
    NotifyUI();
}

void ClearHeldKeys() {

    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        ReconcileInternal(true, batch);
        PublishEngineState();
    }
    batch.flush();
}

// Unified Focus Reconciliation
static void ReconcileLogicalStateFromPhysical(InjectionBatch& batch) {
    // 1. Re-sync Bhop if Space is physically held
    bhop::ForceSpaceSync(s_state.spacePhys);

    // 2. Re-sync WASD
    for (int i = 0; i < 4; ++i) {
        Key k = static_cast<Key>(i);
        if (s_state.phys[i]) {
            if (!s_state.logical[i]) {
                if (s_state.walk.shiftDown) {
                    s_state.walk.startTimeUs[i] = timing::NowUs();
                }
                s_state.logical[i] = true;
                batch.push(k, true);
            }
        } else {
            if (s_state.logical[i]) {
                timing::CancelTimer(k);
                s_state.expectedTimerId[i] = 0;
                batch.push(k, false);
                s_state.logical[i] = false;
            }
        }
    }
    
    // Reset axis state and resolve to naturally apply logic
    s_state.axisState[0] = AxisState::None;
    s_state.axisState[1] = AxisState::None;
    ResolveAxis(Axis::X, batch);
    ResolveAxis(Axis::Y, batch);
}

void RebuildState() {
    DLOG_INFO(Runtime, "Rebuilding semantic state from physical truth...");
    
    // Sync cached physical states with hardware physical truth (prevents stuck keys via Admin window hook bypass)
    bool snapSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    bool snapW = (GetAsyncKeyState('W') & 0x8000) != 0;
    bool snapS = (GetAsyncKeyState('S') & 0x8000) != 0;
    bool snapA = (GetAsyncKeyState('A') & 0x8000) != 0;
    bool snapD = (GetAsyncKeyState('D') & 0x8000) != 0;
    
    bool snapLShift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    bool snapLCtrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    bool snapC = (GetAsyncKeyState('C') & 0x8000) != 0;
    
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        
        s_state.spacePhys = snapSpace;
        s_state.phys[ki(Key::W)] = snapW;
        s_state.phys[ki(Key::S)] = snapS;
        s_state.phys[ki(Key::A)] = snapA;
        s_state.phys[ki(Key::D)] = snapD;
        
        s_state.walk.shiftDown = snapLShift;
        s_state.sysLCtrl = snapLCtrl;
        s_state.sysC = snapC;

        telemetry::ForensicEvent evBefore = { telemetry::ForensicTrapType::FOCUS_LOST, GetCurrentThreadId(), timing::NowUs(), 0,
            (uint32_t)(s_state.phys[0] | (s_state.phys[1] << 1) | (s_state.phys[2] << 2) | (s_state.phys[3] << 3)),
            (uint32_t)(s_state.logical[0] | (s_state.logical[1] << 1) | (s_state.logical[2] << 2) | (s_state.logical[3] << 3)),
            true };
        telemetry::g_forensicBuffer.Push(evBefore);

        // Re-sync using the exact physical truth
        ReconcileLogicalStateFromPhysical(batch);
        PublishEngineState();

        telemetry::ForensicEvent evAfter = { telemetry::ForensicTrapType::FOCUS_GAINED, GetCurrentThreadId(), timing::NowUs(), 0,
            (uint32_t)(s_state.phys[0] | (s_state.phys[1] << 1) | (s_state.phys[2] << 2) | (s_state.phys[3] << 3)),
            (uint32_t)(s_state.logical[0] | (s_state.logical[1] << 1) | (s_state.logical[2] << 2) | (s_state.logical[3] << 3)),
            true };
        telemetry::g_forensicBuffer.Push(evAfter);
    }
    batch.flush();
    NotifyUI();
}

void ReconcileInternal(bool suspending, InjectionBatch& batch) {
    if (suspending) {

        s_state.axisState[0] = s_state.axisState[1] = AxisState::None;
    }
    for (int i = 0; i < 4; ++i) {
        Key k = static_cast<Key>(i);
        timing::CancelTimer(k);
        s_state.expectedTimerId[ki(k)] = 0;
        if (suspending && s_state.logical[i]) {
            batch.push(k, false);
            s_state.logical[i] = false;
        }
    }
    if (suspending) {
        ResolveAxis(Axis::X, batch); ResolveAxis(Axis::Y, batch);
    }
}




} // namespace engine
