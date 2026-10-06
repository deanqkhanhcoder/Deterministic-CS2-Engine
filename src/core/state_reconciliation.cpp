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

    std::unique_lock<std::mutex> operationLock(s_operationMutex);

    InjectionBatch batch(target_platform::GetCurrentIdentity());
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
    FlushAndCommitLogicalState(batch);
    operationLock.unlock();
    if (!s_suspendedAtomic.load(std::memory_order_acquire)) {
        // Reinstall hooks if they were uninstalled by watchdog/fail-safe
        capture::Reinstall();
        RebuildState();
    }
    NotifyStateChanged();
}

void ClearHeldKeys(const target_platform::TargetIdentity& target) {

    std::lock_guard<std::mutex> operationLock(s_operationMutex);

    InjectionBatch batch(target);
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        ReconcileInternal(true, batch);
        PublishEngineState();
    }
    FlushAndCommitLogicalState(batch);
}

// Unified Focus Reconciliation
void ReconcileLogicalStateFromPhysical(InjectionBatch& batch) {
    bhop::ForceSpaceSync(s_state.spacePhys);
    if (rcfg::Get().socdMode == SocdMode::OFF) {
        TrackNativeMovement(batch.expectedTarget);
        s_state.axisState[0] = s_state.axisState[1] = AxisState::None;
        ResolveAxis(Axis::X, batch);
        ResolveAxis(Axis::Y, batch);
        return;
    }

    bool desired[4];
    for (int i = 0; i < 4; ++i) desired[i] = s_state.phys[i];
    for (Axis ax : {Axis::X, Axis::Y}) {
        const int axis = ai(ax);
        const Key pos = keymap::AxisPosKey[axis], neg = keymap::AxisNegKey[axis];
        const bool positive = s_state.phys[ki(pos)], negative = s_state.phys[ki(neg)];
        if (positive && negative) {
            // Keep an existing 8 ms transition; otherwise only the latest key
            // is desired. Never press the suppressed key just to release it.
            if (s_state.socdReleaseTimerId[axis] == 0)
                desired[ki(keymap::Opposite[ki(s_state.socdLastKey[axis])])] = false;
            s_state.axisState[axis] = AxisState::Conflict;
        } else {
            CancelSocdTransition(ax);
            s_state.axisState[axis] = positive ? AxisState::Positive : negative ? AxisState::Negative : AxisState::None;
        }
    }
    for (int i = 0; i < 4; ++i) {
        // An already armed auto-brake owns its deadline on an otherwise idle
        // axis. Reconciliation must neither restart nor truncate it.
        const Key opposite = keymap::Opposite[i];
        if (!s_state.phys[i] && !s_state.phys[ki(opposite)] &&
            s_state.logical[i] && s_state.expectedTimerId[i] != 0) desired[i] = true;
    }
    // All releases precede restores, preserving the last-key switch ordering.
    for (bool down : {false, true}) for (int i = 0; i < 4; ++i) {
        if (desired[i] != down || desired[i] == s_state.logical[i]) continue;
        if (!down) {
            timing::CancelTimer(static_cast<Key>(i));
            s_state.expectedTimerId[i] = 0;
            s_state.expectedTimerTarget[i] = {};
        }
        batch.push(static_cast<Key>(i), down);
        s_state.logical[i] = down;
    }
}

void RebuildState() {
    const auto physicalMask = capture::PhysicalMovementMask();
    std::lock_guard<std::mutex> operationLock(s_operationMutex);
    DLOG_INFO(Runtime, "Rebuilding semantic state from physical truth...");
    const auto activeTarget = target_platform::GetCurrentIdentity();
    
    // Movement uses physical hook edges; other keys keep their existing snapshot path.
    bool snapSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    // Windows async state includes injected key-up; use the hook's physical
    // edge ledger for movement, so a suppressed loser survives focus rebuild.
    bool snapW = (physicalMask & (1u << ki(Key::W))) != 0;
    bool snapS = (physicalMask & (1u << ki(Key::S))) != 0;
    bool snapA = (physicalMask & (1u << ki(Key::A))) != 0;
    bool snapD = (physicalMask & (1u << ki(Key::D))) != 0;
    
    bool snapLShift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    bool snapLCtrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    bool snapC = (GetAsyncKeyState('C') & 0x8000) != 0;

    InjectionBatch batch(activeTarget);
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
    FlushAndCommitLogicalState(batch);
    NotifyStateChanged();
}

void ReconcileInternal(bool suspending, InjectionBatch& batch) {
    CancelSocdTransition(Axis::X);
    CancelSocdTransition(Axis::Y);
    if (suspending) {

        s_state.axisState[0] = s_state.axisState[1] = AxisState::None;
        for (auto& native : s_state.nativeLogical) native = false;
    }
    for (int i = 0; i < 4; ++i) {
        Key k = static_cast<Key>(i);
        timing::CancelTimer(k);
        s_state.expectedTimerId[ki(k)] = 0;
        s_state.expectedTimerTarget[ki(k)] = {};
        if (suspending && s_state.logical[i]) {
            batch.push(k, false);
            s_state.logical[i] = false;
        }
    }
    // Resume/focus rebuild resolves physical keys; cleanup must not press them again.
}




} // namespace engine
