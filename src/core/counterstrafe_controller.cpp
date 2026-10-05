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

static std::atomic<std::uint32_t> s_counterHeldMask{0};
static target_platform::detail::TargetPublicationStore s_counterTargets[4];
bool IsCounterStrafeHoldingKey(Key key, const target_platform::TargetIdentity& target) {
    return ki(key) < 4 && (s_counterHeldMask.load(std::memory_order_acquire) & (1u << ki(key))) != 0 &&
        s_counterTargets[ki(key)].Load().target == target;
}

void CommitLogicalStateFromInjection() {
    const std::uint32_t heldMask = injection::HeldMovementMask();
    std::lock_guard<std::mutex> lock(s_stateMutex);
    for (int i = 0; i < 4; ++i)
        s_state.logical[i] = s_state.nativeLogical[i] || (heldMask & (1u << i)) != 0;
    s_counterHeldMask.store(heldMask, std::memory_order_release);
    UpdateVelocityWishFromLogicalState();
    PublishEngineState();
}

void UpdateVelocityWishFromLogicalState() {
    // Velocity tracking: close the interval under the previous wish, then
    // open a new one for the keys now held in the game. Opposing keys held
    // together cancel to wish 0 (friction only), as in Source.
    movement::AdvanceVelocity(s_state.vel, timing::NowUs(), rcfg::Get());
    const auto key = [&](Key k) { return s_state.logical[ki(k)] ? 1 : 0; };
    s_state.vel.wishX = key(Key::D) - key(Key::A);
    s_state.vel.wishY = key(Key::W) - key(Key::S);
    s_state.vel.speedScale = 1.0;
    if (s_state.IsCrouching()) {
        s_state.vel.speedScale = 0.34;
    } else if (s_state.walk.shiftDown ||
               (timing::NowMs() - s_state.walk.shiftReleaseTimeMs) < rcfg::Get().walkMemoryMs) {
        s_state.vel.speedScale = 0.52;
    }
}

void TrackNativeMovement(const target_platform::TargetIdentity& target) {
    movement::AdvanceVelocity(s_state.vel, timing::NowUs(), rcfg::Get());
    const bool active = !s_state.suspended && target_platform::IsExpectedTargetActive(target);
    const auto heldMask = injection::HeldMovementMask();
    for (int i = 0; i < 4; ++i) {
        s_state.nativeLogical[i] = active && s_state.phys[i];
        s_state.logical[i] = s_state.nativeLogical[i] || (heldMask & (1u << i)) != 0;
    }
    UpdateVelocityWishFromLogicalState();

}

void FlushAndCommitLogicalState(InjectionBatch& batch) {
    batch.flush();
    CommitLogicalStateFromInjection();
}

UINT ReconcilePendingOutput(
    const target_platform::TargetIdentity& target) {
    std::lock_guard<std::mutex> operationLock(s_operationMutex);
    const UINT released = injection::ReconcilePendingReleasesForTarget(target);
    CommitLogicalStateFromInjection();
    return released;
}

void CancelSocdTransition(Axis ax) {
    auto& timerId = s_state.socdReleaseTimerId[ai(ax)];
    if (timerId == 0) return;
    for (Key key : {keymap::AxisPosKey[ai(ax)], keymap::AxisNegKey[ai(ax)]}) {
        if (s_state.expectedTimerId[ki(key)] != timerId) continue;
        timing::CancelTimer(key);
        s_state.expectedTimerId[ki(key)] = 0;
        s_state.expectedTimerTarget[ki(key)] = {};
    }
    timerId = 0;
}

void ResolveAxis(Axis ax, InjectionBatch& batch, bool onPhysicalPress) {
    int ai_a = ai(ax);
    const auto mode = rcfg::Get().socdMode;
    Key posKey = keymap::AxisPosKey[ai_a];
    Key negKey = keymap::AxisNegKey[ai_a];

    bool posDown = s_state.phys[ki(posKey)];
    bool negDown = s_state.phys[ki(negKey)];

    AxisState prevState = s_state.axisState[ai_a];
    AxisState newState;

    if (posDown && negDown)       newState = AxisState::Conflict;
    else if (posDown)             newState = AxisState::Positive;
    else if (negDown)             newState = AxisState::Negative;
    else                          newState = AxisState::None;

    if (newState == prevState && !(onPhysicalPress && newState == AxisState::Conflict)) return;

    CancelSocdTransition(ax);
    s_state.axisState[ai_a] = newState;
    s_state.generation[ai_a]++;

    if (newState == AxisState::Conflict) {
        // A physical second press owns this transition. Repeats/ticks return
        // above; stale brake timers must never restore an unprioritized key.
        for (Key key : {posKey, negKey}) {
            timing::CancelTimer(key);
            s_state.expectedTimerId[ki(key)] = 0;
            s_state.expectedTimerTarget[ki(key)] = {};
        }
        if (mode == SocdMode::OFF) return; // Preserve both native physical edges.
        const Key winner = s_state.socdLastKey[ai_a];
        const Key loser = keymap::Opposite[ki(winner)];
        if (mode == SocdMode::HUMANIZED && onPhysicalPress && s_state.logical[ki(loser)]) {
            const uint64_t timerId = timing::ScheduleTimerUs(loser, 8000);
            if (timerId != 0) {
                s_state.socdReleaseTimerId[ai_a] = timerId;
                s_state.expectedTimerId[ki(loser)] = timerId;
                s_state.expectedTimerTarget[ki(loser)] = batch.expectedTarget;
            }
            // Timer admission failure falls back to immediate last-key priority.
        }
        if (s_state.socdReleaseTimerId[ai_a] == 0) {
            if (s_state.logical[ki(loser)]) batch.push(loser, false);
            s_state.logical[ki(loser)] = false;
        }
        if (!s_state.logical[ki(winner)]) batch.push(winner, true);
        s_state.logical[ki(winner)] = true;
        return;
    }

    if (prevState == AxisState::Conflict && (newState == AxisState::Positive || newState == AxisState::Negative)) {
        Key survKey = (newState == AxisState::Positive) ? posKey : negKey;
        int ki_s = ki(survKey);
        int64_t nowUs = timing::NowUs();

        s_state.downTimeUs[ki_s]       = nowUs;
        s_state.walk.accumUs[ki_s]     = 0;
        s_state.walk.startTimeUs[ki_s] = s_state.walk.shiftDown ? nowUs : 0;
        s_state.conflictEnteredTimeMs[ai_a] = 0;
        s_state.mem.lastDir[ai_a] = (newState == AxisState::Positive) ? AxisDir::Positive : AxisDir::Negative;
        s_state.mem.lastConflictExitTimeMs[ai_a] = timing::NowMs();

        if (mode != SocdMode::OFF && !s_state.logical[ki_s]) batch.push(survKey, true);
        s_state.logical[ki_s] = true;
        return;
    }

    if (newState == AxisState::Positive || newState == AxisState::Negative) {
        Key oppKey = (newState == AxisState::Positive) ? negKey : posKey;
        CancelStaleCounterStrafe(oppKey, batch);

        AxisDir newDir  = (newState == AxisState::Positive) ? AxisDir::Positive : AxisDir::Negative;
        if (s_state.mem.lastDir[ai_a] != AxisDir::None && s_state.mem.lastDir[ai_a] != newDir) {
            s_state.mem.lastDirChangeTimeMs[ai_a] = timing::NowMs();
        }
        s_state.mem.lastDir[ai_a] = newDir;
    }
}

void CancelStaleCounterStrafe(Key oppKey, InjectionBatch& batch) {
    int ki_o = ki(oppKey);
    timing::CancelTimer(oppKey);
        s_state.expectedTimerId[ki(oppKey)] = 0;
    if (s_state.logical[ki_o] && !s_state.phys[ki_o]) {
        batch.push(oppKey, false);
        s_state.logical[ki_o] = false;
    }
}

void NeutralizeAxis(Axis ax, InjectionBatch& batch) {
    int ai_a = ai(ax);
    Key keys[2] = { keymap::AxisNegKey[ai_a], keymap::AxisPosKey[ai_a] };
    s_state.conflictEnteredTimeMs[ai_a] = timing::NowMs();
    s_state.mem.conflictPenalty[ai_a] = std::min(1.0, s_state.mem.conflictPenalty[ai_a] + cfg_rt::CONFLICT_INCREMENT());

    for (Key k : keys) {
        int ki_k = ki(k);
        timing::CancelTimer(k);
        s_state.expectedTimerId[ki(k)] = 0;
        if (s_state.logical[ki_k]) {
            batch.push(k, false);
            s_state.logical[ki_k] = false;
        }
        if (s_state.walk.startTimeUs[ki_k] > 0) {
            s_state.walk.accumUs[ki_k] += timing::NowUs() - s_state.walk.startTimeUs[ki_k];
            s_state.walk.startTimeUs[ki_k] = 0;
        }
    }
}

// Telemetry globals for last brake event
alignas(64) std::atomic<int64_t> g_lastBrakeUs{0};
alignas(64) std::atomic<int64_t> g_lastPreSpeedTenths{0};
alignas(64) std::atomic<uint32_t> g_lastBrakeResultCode{0}; // 0=IDLE, 1=FINE, 2=EARLY, 3=OVER

int64_t CalculateTrueBrakeUs(Key relKey, Axis ax, int64_t heldUs) {
    const RuntimeConfig& rc = rcfg::Get();
    const auto& profile = rc.brakeProfiles[rc.activeBrakeProfileIndex];
    
    // 1. Velocity at the instant of release, from the tracked state that
    // integrates every key actually injected into the game (A-D strafing,
    // conflict neutralization, previous brakes). Never restarted from zero.
    movement::AdvanceVelocity(s_state.vel, timing::NowUs(), rc);
    double vx = s_state.vel.vx;
    double vy = s_state.vel.vy;
    
    // Check for diagonal movement context
    Axis orthAx = (ax == Axis::X) ? Axis::Y : Axis::X;
    Key orthPos = keymap::AxisPosKey[ai(orthAx)];
    Key orthNeg = keymap::AxisNegKey[ai(orthAx)];

    bool orthPhys = s_state.phys[ki(orthPos)] || s_state.phys[ki(orthNeg)];
    bool orthLog  = s_state.logical[ki(orthPos)] || s_state.logical[ki(orthNeg)];
    bool orthRecent = (timing::NowMs() - s_state.mem.lastReleaseTimeMs[ai(orthAx)]) < 120;
    bool isDiagonal = orthPhys || orthLog || orthRecent ||
                      (std::abs(vx) > 30.0 && std::abs(vy) > 30.0);

    // Cross-check with continuous physical hold time of the releasing key.
    // If the player held this key continuously (e.g. running for >200ms),
    // velocity along this direction cannot be less than the physical hold acceleration.
    double physVx = 0.0, physVy = 0.0;
    movement::EstimateTrueVelocity2D(
        (ax == Axis::X) ? heldUs : 0,
        (ax == Axis::Y) ? heldUs : 0,
        1, 1, rc, physVx, physVy);
    double sustainedMinV = (ax == Axis::X) ? physVx : physVy;

    // In CS2, when strafing diagonally, vector speed is capped by sv_maxspeed (250 u/s),
    // so steady-state per-axis component speed is capped at 250 / sqrt(2) ≈ 176.78 u/s.
    if (isDiagonal) {
        constexpr double kInvSqrt2 = 0.7071067811865475;
        sustainedMinV *= kInvSqrt2;
    }

    // 3. Determine wish_mode for the offline LUT
    int wish_mode = 0;
    double v_orth = (ax == Axis::X) ? vy : vx;
    if (std::abs(v_orth) > 0.1) {
        bool posLog = s_state.logical[ki(orthPos)];
        bool negLog = s_state.logical[ki(orthNeg)];
        
        if (posLog && !negLog) {
            wish_mode = (v_orth > 0) ? 1 : 2;
        } else if (negLog && !posLog) {
            wish_mode = (v_orth < 0) ? 1 : 2;
        }
    }
    
    // Speed along the direction of the key being released. If we are already
    // moving the other way, pressing the opposite key would accelerate us.
    const double relSign = (relKey == Key::D || relKey == Key::W) ? 1.0 : -1.0;
    double v_target = ((ax == Axis::X) ? vx : vy) * relSign;
    if (sustainedMinV > v_target) {
        v_target = sustainedMinV;
    }
    if (isDiagonal) {
        constexpr double kMaxDiagAxisSpeed = 250.0 * 0.7071067811865475; // ~176.78 u/s
        if (v_target > kMaxDiagAxisSpeed) {
            v_target = kMaxDiagAxisSpeed;
        }
    }
    if (v_target <= 0.0) return 0;
    
    // 4. Profile-aware deterministic stop simulation
    // In CS2, weapon firing accuracy requires velocity to decrease below the
    // weapon's true accuracy threshold (< 34.0 u/s, or 17.0–20.0 u/s for complete stop).
    // Clamp threshold to <= 34.0 u/s so legacy config values (80/70 u/s) never
    // truncate the simulation at 3-4 ticks (47-63 ms).
    double stopThreshold = profile.accuracyThreshold;
    if (stopThreshold > 34.0 || stopThreshold < 1.0) {
        stopThreshold = 20.0;
    }

    int pureDurMs = movement::CalculateStopDur2D(
        v_target,
        v_orth,
        wish_mode,
        s_state.IsCrouching(),
        stopThreshold,
        rc);

    if (pureDurMs <= 0) {
        return 0;
    }

    // When releasing from diagonal strafe, compensate for the diagonal projection
    // by trimming ~1 tick (~15.6 ms) of excess brake duration to eliminate over-counter-strafe.
    if (isDiagonal && pureDurMs > 16) {
        pureDurMs = std::max(16, pureDurMs - 16);
    }

    // 5. Apply authority biases through the bounded, deterministic policy.
    // Do not allow profile multipliers (< 1.0) or negative bias to reduce the
    // brake duration below the physical ticks required to reach the target threshold.
    int finalDurMs = movement::ShapeBrakeDurationMs(pureDurMs, profile, rc);
    if (finalDurMs < pureDurMs && profile.brake_bias_multiplier < 1.0) {
        finalDurMs = pureDurMs;
    }
    int64_t baseBrakeUs = (int64_t)finalDurMs * 1000LL;
    int64_t effectiveBrakeUs = std::max(100LL, baseBrakeUs);
    
    // Record telemetry for HUD
    g_lastBrakeUs.store(effectiveBrakeUs, std::memory_order_relaxed);
    g_lastPreSpeedTenths.store(static_cast<int64_t>(std::hypot(vx, vy) * 10.0), std::memory_order_relaxed);
    g_lastBrakeResultCode.store(1, std::memory_order_relaxed); // FINE

    return effectiveBrakeUs;
}

bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch) {
    const int ki_c = ki(counterKey);
    if (s_state.phys[ki_c] || s_state.suspended ||
        !target_platform::IsExpectedTargetActive(batch.expectedTarget)) return false;
    const RuntimeConfig& rc = rcfg::Get();
    // No min-tap guard: a short tap while already moving fast still needs a
    const auto& profile = rc.brakeProfiles[rc.activeBrakeProfileIndex];
    int64_t effectiveBrakeUs = CalculateTrueBrakeUs(relKey, ax, heldUs);
    if (effectiveBrakeUs <= 0) {
        return false;
    }
    
    // When overlap duration is 0 (standard across all neutral weapon profiles),
    // the original key is immediately released and the counter key pressed in the
    // same injection batch, ensuring zero overlap (no friction-only waste tick).
    // If a non-zero overlap is configured, an overlap release timer is scheduled.
    int64_t effectiveOverlapUs = movement::ClampCounterOverlapUs(
        s_state.logical[ki(relKey)] ? profile.overlap_duration_us : 0, effectiveBrakeUs);

    uint64_t overlapTimerId = 0;
    if (effectiveOverlapUs > 0) {
        overlapTimerId = timing::ScheduleTimerUs(relKey, effectiveOverlapUs);
        if (overlapTimerId == 0) return false;
    }

    const uint64_t counterTimerId =
        timing::ScheduleTimerUs(counterKey, effectiveBrakeUs);
    if (counterTimerId == 0) {
        if (overlapTimerId != 0) timing::CancelTimer(relKey);
        return false;
    }

    const int ki_r = ki(relKey);

    if (effectiveOverlapUs <= 0) {
        s_state.expectedTimerId[ki_r] = 0;
        if (s_state.logical[ki_r]) {
            batch.push(relKey, false);
            s_state.logical[ki_r] = false;
        }
    } else {
        s_state.expectedTimerTarget[ki_r] = batch.expectedTarget;
        s_state.expectedTimerId[ki_r] = overlapTimerId;
    }

    if (!s_state.logical[ki_c]) {
        if ((s_counterHeldMask.load(std::memory_order_acquire) & (1u << ki_c)) == 0)
            s_counterTargets[ki_c].Store(nullptr, batch.expectedTarget);
        batch.push(counterKey, true);
        s_state.logical[ki_c] = true;
    }

    s_state.expectedTimerTarget[ki_c] = batch.expectedTarget;
    s_state.expectedTimerId[ki_c] = counterTimerId;
    
    s_state.mem.conflictPenalty[ai(ax)] = std::max(0.0, s_state.mem.conflictPenalty[ai(ax)] - rc.conflictDecrement);
    return true;
}

} // namespace engine
