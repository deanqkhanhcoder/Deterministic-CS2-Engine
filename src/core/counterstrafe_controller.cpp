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

void ResolveAxis(Axis ax, InjectionBatch& batch) {
    int ai_a = ai(ax);
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

    if (newState == prevState) return;

    s_state.axisState[ai_a] = newState;
    s_state.generation[ai_a]++;

    if (newState == AxisState::Conflict) {
        NeutralizeAxis(ax, batch);
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

        batch.push(survKey, true);
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

struct NoiseGenerator {
    uint64_t state[2];

    NoiseGenerator() {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        state[0] = t.QuadPart ^ 0x9E3779B97F4A7C15ULL;
        state[1] = GetCurrentThreadId() ^ 0xBF58476D1CE4E5B9ULL;
    }

    uint64_t next() {
        uint64_t s1 = state[0];
        const uint64_t s0 = state[1];
        state[0] = s0;
        s1 ^= s1 << 23;
        state[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
        return state[1] + s0;
    }

    int next_jitter(int min_us, int max_us) {
        if (min_us >= max_us) return min_us;
        return min_us + (next() % (max_us - min_us + 1));
    }

    double next_double() {
        return (next() >> 11) * (1.0 / 9007199254740992.0);
    }

    double next_gaussian(double mean, double stddev) {
        double u1 = next_double();
        double u2 = next_double();
        if (u1 <= 1e-15) u1 = 1e-15;
        double z0 = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979323846 * u2);
        return z0 * stddev + mean;
    }
};

static thread_local NoiseGenerator s_noise;

void ApplyOverlapCounterStrafe(Key releaseKey, Key counterKey, int64_t overlapUs, int64_t brakeUs, InjectionBatch& batch) {
    (void)brakeUs;
    int ki_r = ki(releaseKey);
    int ki_c = ki(counterKey);
    // 1. Inject counterKey DOWN immediately (in the same batch, achieving 0-us subtick packing)
    if (!s_state.logical[ki_c]) {
        batch.push(counterKey, true);
        s_state.logical[ki_c] = true;
    }
    
    // 2. Schedule releaseKey UP
    if (overlapUs <= 0) {
        if (s_state.logical[ki_r]) {
            batch.push(releaseKey, false);
            s_state.logical[ki_r] = false;
        }
    } else {
        s_state.expectedTimerId[ki_r] = timing::ScheduleTimerUs(releaseKey, overlapUs);
    }
}


int64_t CalculateTrueBrakeUs(Key relKey, Axis ax, int64_t heldUs) {
    const RuntimeConfig& rc = rcfg::Get();
    const auto& profile = rc.brakeProfiles[rc.activeBrakeProfileIndex];
    
    // 1. Reconstruct 2D Held Times for True Velocity
    int64_t heldUsX = 0;
    int64_t heldUsY = 0;
    int signX = 0;
    int signY = 0;
    
    auto getHold = [&](Axis queryAx, int64_t& outHold, int& outSign) {
        if (queryAx == ax) {
            outHold = heldUs;
            outSign = (relKey == Key::D || relKey == Key::W) ? 1 : -1;
        } else {
            Key posK = keymap::AxisPosKey[ai(queryAx)];
            Key negK = keymap::AxisNegKey[ai(queryAx)];
            if (s_state.phys[ki(posK)]) {
                outHold = timing::NowUs() - s_state.downTimeUs[ki(posK)];
                outSign = 1;
            } else if (s_state.phys[ki(negK)]) {
                outHold = timing::NowUs() - s_state.downTimeUs[ki(negK)];
                outSign = -1;
            } else {
                int64_t lastReleaseMs = s_state.mem.lastReleaseTimeMs[ai(queryAx)];
                int64_t nowMs = timing::NowMs();
                if (nowMs - lastReleaseMs < profile.momentum_memory_ms) {
                    outHold = s_state.mem.lastHoldUs[ai(queryAx)];
                    outSign = (s_state.mem.lastDir[ai(queryAx)] == AxisDir::Positive) ? 1 : -1;
                }
            }
        }
    };
    
    getHold(Axis::X, heldUsX, signX);
    getHold(Axis::Y, heldUsY, signY);
    
    // 2. Extract Base True Velocity
    double vx = 0.0, vy = 0.0;
    movement::EstimateTrueVelocity2D(heldUsX, heldUsY, signX, signY, rc, vx, vy);
    
    // Apply efficiency modifiers (walk, tap spam, etc)
    double efficiency = movement::CalcIntentEfficiency(relKey, heldUs, s_state, rc);
    vx *= efficiency;
    vy *= efficiency;
    
    // ── True Velocity Capping (Walk/Crouch) ──
    double maxSpeed = 250.0;
    if (s_state.IsCrouching()) {
        maxSpeed = 250.0 * 0.34; // 85.0
    } else {
        bool inWalkCtx = s_state.walk.shiftDown || ((timing::NowMs() - s_state.walk.shiftReleaseTimeMs) < rc.walkMemoryMs);
        if (inWalkCtx) {
            maxSpeed = 250.0 * 0.52; // 130.0
        }
    }
    
    double currentMagnitude = std::sqrt(vx*vx + vy*vy);
    if (currentMagnitude > maxSpeed) {
        double scale = maxSpeed / currentMagnitude;
        vx *= scale;
        vy *= scale;
    }
    
    // 3. Determine wish_mode for the offline LUT
    int wish_mode = 0;
    
    Axis orthAx = (ax == Axis::X) ? Axis::Y : Axis::X;
    Key orthPos = keymap::AxisPosKey[ai(orthAx)];
    Key orthNeg = keymap::AxisNegKey[ai(orthAx)];
    
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
    
    double v_target = (ax == Axis::X) ? vx : vy;
    
    // 4. O(1) Deterministic Hybrid 2D LUT Lookup
    int pureDurMs = movement::LookupStopDur2D(v_target, v_orth, wish_mode, s_state.IsCrouching());
    
    // 5. Apply authority biases
    int alignedDurMs = pureDurMs;
    
    auto applyShape = [&](int dur) {
        double d = (double)dur * profile.brake_bias_multiplier;
        double norm = d / 31.25;
        if (norm < 0.01) norm = 0.01;
        return std::pow(norm, profile.aggressiveness_curve) * 31.25;
    };
    
    double shapedDur = applyShape(alignedDurMs) + profile.authority_bias_ms;
    
    int finalDurMs = std::max(rc.minStopMs, (int)std::round(shapedDur) - rc.latencyMarginMs);
    int64_t baseBrakeUs = (int64_t)finalDurMs * 1000LL;
    int64_t effectiveBrakeUs = std::max(100LL, baseBrakeUs);
    
    int64_t microJitterBrakeUs = (int64_t)(s_noise.next_gaussian(0.0, 0.25) * 1000.0);
    effectiveBrakeUs = std::max(100LL, effectiveBrakeUs + microJitterBrakeUs);
    
    return effectiveBrakeUs;
}

bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch) {
    int ki_c = ki(counterKey);
    if (s_state.phys[ki_c]) {
        DLOG_TRACE(Runtime, "AutoCounterStrafe ABORT: %s phys held", reinterpret_cast<int64_t>(keymap::KeyName[ki_c]));
        return false;
    }
    if (s_state.axisState[ai(ax)] == AxisState::Conflict) {
        DLOG_TRACE(Runtime, "AutoCounterStrafe ABORT: Axis %d in Conflict", ai(ax));
        return false;
    }
    const RuntimeConfig& rc = rcfg::Get();
    if (heldUs < rc.minTapUs) {
        DLOG_TRACE(Runtime, "AutoCounterStrafe ABORT: tap too short (%lld < %d)", heldUs, rc.minTapUs);
        return false;
    }

    const auto& profile = rc.brakeProfiles[rc.activeBrakeProfileIndex];
    int64_t effectiveBrakeUs = CalculateTrueBrakeUs(relKey, ax, heldUs);
    
    int64_t baseOverlapUs = profile.overlap_duration_us;
    
    int64_t effectiveOverlapUs = baseOverlapUs;

    // Phase 5: Hardware-Emulation Humanization (Gaussian Micro-Jitter)
    int64_t microJitterOverlapUs = (int64_t)(s_noise.next_gaussian(0.0, 0.25) * 1000.0);

    effectiveOverlapUs = std::max(0LL, effectiveOverlapUs + microJitterOverlapUs);

    // Phase 6: Guarantee Physical Quantization Survival
    const int64_t MIN_BRAKE_PHASE_US = 16000;
    if (effectiveBrakeUs - effectiveOverlapUs < MIN_BRAKE_PHASE_US) {
        effectiveBrakeUs = effectiveOverlapUs + MIN_BRAKE_PHASE_US;
    }

    ApplyOverlapCounterStrafe(relKey, counterKey, effectiveOverlapUs, effectiveBrakeUs, batch);
    
    s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerUs(counterKey, effectiveBrakeUs);
    
    s_state.mem.conflictPenalty[ai(ax)] = std::max(0.0, s_state.mem.conflictPenalty[ai(ax)] - rc.conflictDecrement);
    return true;
}

} // namespace engine
