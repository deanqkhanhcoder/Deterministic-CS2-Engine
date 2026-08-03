// â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
// â•‘  CS2 Macro Suite â€” Runtime Configuration Implementation             â•‘
// â•‘  Seqlock pattern: true lock-free concurrent reads, safe atomic sync â•‘
// â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

#include "runtime_config.h"
#include <thread>
#include <cstring>
#include <cmath>
#include <mutex>
#include <immintrin.h>
#include "movement_reconstruction.h"
#include "telemetry.h"
#include "timing.h"

namespace rcfg {

constexpr size_t WORDS = (sizeof(RuntimeConfig) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
static std::atomic<uint64_t> s_active[WORDS];
static std::atomic<uint32_t> s_seq{0};
static RuntimeConfig s_baseConfig; // Pure user config without SafeMode overrides
static std::mutex s_writerMutex; // Seqlocks require exactly one writer.

RuntimeConfig Get() {
    RuntimeConfig snapshot;
    uint64_t buffer[WORDS];
    uint32_t seq0, seq1;
    int spin_count = 0;
    
    while (true) {
        seq0 = s_seq.load(std::memory_order_acquire);
        
        if (seq0 & 1) {
            // Writer in progress. Adaptive backoff:
            if (spin_count < 64) {
                _mm_pause(); // Tight spin for short write bursts
            } else if (spin_count < 1024) {
                std::this_thread::yield(); // Yield to writer on same core
            } else {
                // Long-tail starvation: sleep to allow system-wide progress
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            spin_count++;
            continue;
        }

        // Try reading payload
        for (size_t i = 0; i < WORDS; ++i) {
            buffer[i] = s_active[i].load(std::memory_order_relaxed);
        }

        std::atomic_thread_fence(std::memory_order_acq_rel); 
        seq1 = s_seq.load(std::memory_order_relaxed);

        if (seq0 == seq1) {
            break; // Success: no concurrent write during payload load
        }
        
        // Read failed due to concurrent write. Increment spin and retry.
        spin_count++;
        if (spin_count > 2048) {
            // Extreme starvation safety
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            spin_count = 0;
        }
    }

    std::memcpy(&snapshot, buffer, sizeof(RuntimeConfig));
    return snapshot;
}

RuntimeConfig Sanitize(const RuntimeConfig& newCfg) {
    RuntimeConfig validated = newCfg;

    const auto clampFinite = [](double value, double minimum, double maximum) {
        if (!std::isfinite(value)) return minimum;
        if (value < minimum) return minimum;
        if (value > maximum) return maximum;
        return value;
    };

    // --- Clamping & Validation Bounds Layer ---
    // Every floating-point input is made finite before it reaches physics or timing.
    validated.crouchMult = clampFinite(validated.crouchMult, 0.05, 5.0);

    if (validated.quickTapMs < 1) validated.quickTapMs = 1;
    if (validated.quickTapMs > 1000) validated.quickTapMs = 1000;

    if (validated.maxScaleMs < 1) validated.maxScaleMs = 1;
    if (validated.maxScaleMs > 2000) validated.maxScaleMs = 2000;

    if (validated.latencyMarginMs < 0) validated.latencyMarginMs = 0;
    if (validated.latencyMarginMs > 500) validated.latencyMarginMs = 500;

    if (validated.minStopMs < 1) validated.minStopMs = 1;
    if (validated.minStopMs > 1000) validated.minStopMs = 1000;

    if (validated.lutMaxMs < 1) validated.lutMaxMs = 1;
    if (validated.lutMaxMs > 2000) validated.lutMaxMs = 2000;
    // Duration shaping uses this pair as clamp bounds. Preserve the hard
    // safety horizon and lower the minimum rather than extending a corrupt
    // maximum; an inverted pair would otherwise violate std::clamp's
    // precondition and could produce random over-counter behaviour.
    if (validated.minStopMs > validated.lutMaxMs) {
        validated.minStopMs = validated.lutMaxMs;
    }

    if (validated.walkMemoryMs < 1) validated.walkMemoryMs = 1;
    if (validated.walkMemoryMs > 2000) validated.walkMemoryMs = 2000;

    validated.walkRatioSkip = clampFinite(validated.walkRatioSkip, 0.01, 5.0);
    validated.walkRatioLight = clampFinite(validated.walkRatioLight, 0.01, 5.0);

    if (validated.minWalkStopMs < 1) validated.minWalkStopMs = 1;
    if (validated.minWalkStopMs > 1000) validated.minWalkStopMs = 1000;

    if (validated.walkMaxStopMs < 1) validated.walkMaxStopMs = 1;
    if (validated.walkMaxStopMs > 1000) validated.walkMaxStopMs = 1000;

    validated.decayK = clampFinite(validated.decayK, 0.00001, 1.0);

    if (validated.dirChangePenaltyMs < 1) validated.dirChangePenaltyMs = 1;
    if (validated.dirChangePenaltyMs > 1000) validated.dirChangePenaltyMs = 1000;

    if (validated.tapSpamWindowMs < 1) validated.tapSpamWindowMs = 1;
    if (validated.tapSpamWindowMs > 1000) validated.tapSpamWindowMs = 1000;

    validated.stopStrengthMin = clampFinite(validated.stopStrengthMin, 0.01, 2.0);

    validated.tapSpamAlpha = clampFinite(validated.tapSpamAlpha, 0.01, 1.0);

    if (validated.tapSpamHalfLifeMs < 1) validated.tapSpamHalfLifeMs = 1;
    if (validated.tapSpamHalfLifeMs > 5000) validated.tapSpamHalfLifeMs = 5000;

    if (validated.minTapUs < 1) validated.minTapUs = 1;
    if (validated.minTapUs > 1000000) validated.minTapUs = 1000000;

    if (validated.humanizeMinUs < -1000000) validated.humanizeMinUs = -1000000;
    if (validated.humanizeMinUs > 1000000) validated.humanizeMinUs = 1000000;
    if (validated.humanizeMaxUs < -1000000) validated.humanizeMaxUs = -1000000;
    if (validated.humanizeMaxUs > 1000000) validated.humanizeMaxUs = 1000000;
    if (validated.humanizeMaxUs < validated.humanizeMinUs) {
        validated.humanizeMaxUs = validated.humanizeMinUs;
    }

    // Physics
    validated.physMaxSpeed = clampFinite(validated.physMaxSpeed, 1.0, 10000.0);
    validated.physFriction = clampFinite(validated.physFriction, 0.01, 100.0);
    validated.physStopSpeed = clampFinite(validated.physStopSpeed, 1.0, 10000.0);
    validated.physAccelerate = clampFinite(validated.physAccelerate, 0.01, 100.0);

    // Brake profiles directly control synthetic hold duration. Corrupt or
    // non-finite values must never amplify a counter-strafe.
    const RuntimeConfig defaults{};
    for (size_t index = 0; index < 5; ++index) {
        auto& profile = validated.brakeProfiles[index];
        const auto& fallback = defaults.brakeProfiles[index];

        if (profile.overlap_duration_us < 0) profile.overlap_duration_us = 0;
        if (profile.overlap_duration_us > 1000000) {
            profile.overlap_duration_us = 1000000;
        }

        const auto finiteOrDefault = [](double value, double defaultValue) {
            return std::isfinite(value) ? value : defaultValue;
        };
        profile.brake_bias_multiplier = finiteOrDefault(
            profile.brake_bias_multiplier, fallback.brake_bias_multiplier);
        profile.authority_bias_ms = finiteOrDefault(
            profile.authority_bias_ms, fallback.authority_bias_ms);
        profile.aggressiveness_curve = finiteOrDefault(
            profile.aggressiveness_curve, fallback.aggressiveness_curve);
        profile.momentum_memory_ms = finiteOrDefault(
            profile.momentum_memory_ms, fallback.momentum_memory_ms);
        profile.accuracyThreshold = finiteOrDefault(
            profile.accuracyThreshold, fallback.accuracyThreshold);

        profile.brake_bias_multiplier = clampFinite(
            profile.brake_bias_multiplier, 0.1, 2.0);
        profile.authority_bias_ms = clampFinite(
            profile.authority_bias_ms, -100.0, 100.0);
        profile.aggressiveness_curve = clampFinite(
            profile.aggressiveness_curve, 0.25, 4.0);
        profile.momentum_memory_ms = clampFinite(
            profile.momentum_memory_ms, 0.0, 1000.0);
        profile.accuracyThreshold = clampFinite(
            profile.accuracyThreshold, 1.0, validated.physMaxSpeed);
    }

    // Conflict
    validated.conflictIncrement = clampFinite(validated.conflictIncrement, 0.01, 10.0);
    validated.conflictDecrement = clampFinite(validated.conflictDecrement, 0.01, 10.0);

    if (validated.conflictHalfLifeMs < 1) validated.conflictHalfLifeMs = 1;
    if (validated.conflictHalfLifeMs > 5000) validated.conflictHalfLifeMs = 5000;

    // Bhop Mode / profile indices
    if (validated.bhopMode < 1 || validated.bhopMode > 4) validated.bhopMode = 4;
    if (validated.activeBrakeProfileIndex < 1 || validated.activeBrakeProfileIndex > 4) {
        validated.activeBrakeProfileIndex = 1;
    }
    if (validated.airborneDelayMs < 1) validated.airborneDelayMs = 1;
    if (validated.airborneDelayMs > 5000) validated.airborneDelayMs = 5000;

    if (validated.scrollBurstGapMs < 1) validated.scrollBurstGapMs = 1;
    if (validated.scrollBurstGapMs > 1000) validated.scrollBurstGapMs = 1000;

    if (validated.landingScanMs < 1) validated.landingScanMs = 1;
    if (validated.landingScanMs > 5000) validated.landingScanMs = 5000;

    if (validated.airborneLockMs < 1) validated.airborneLockMs = 1;
    if (validated.airborneLockMs > 5000) validated.airborneLockMs = 5000;

    if (validated.spamIntervalMs < 1) validated.spamIntervalMs = 1;
    if (validated.spamIntervalMs > 1000) validated.spamIntervalMs = 1000;

    for (int i = 1; i <= 4; ++i) {
        auto& m = validated.modeCfg[i];
        if (m.hMin < 0) m.hMin = 0;
        if (m.hMin > 1000) m.hMin = 1000;
        if (m.hMax < m.hMin) m.hMax = m.hMin;
        if (m.hMax > 2000) m.hMax = 2000;

        if (m.dMin < 0) m.dMin = 0;
        if (m.dMin > 1000) m.dMin = 1000;
        if (m.dMax < m.dMin) m.dMax = m.dMin;
        if (m.dMax > 2000) m.dMax = 2000;
    }

    if (validated.dashboardRefreshMs < 10) validated.dashboardRefreshMs = 10;
    if (validated.dashboardRefreshMs > 10000) validated.dashboardRefreshMs = 10000;

    // --- Safe Mode Override Layer ---
    if (validated.safeModeEnabled) {
        validated.latencyMarginMs = 4;
        validated.minStopMs = 6;

        validated.walkMemoryMs = 80;
        validated.tapSpamWindowMs = 40;

        validated.stopStrengthMin = 0.40;

        validated.conflictIncrement = 0.15;
        validated.conflictDecrement = 0.15;

        validated.humanizeMinUs = 0;
        validated.humanizeMaxUs = 0;

        // Bhop (limit spam)
        if (validated.spamIntervalMs < 4) {
            validated.spamIntervalMs = 4;
        }
    }

    return validated;
}

void Apply(const RuntimeConfig& newCfg) {
    std::lock_guard<std::mutex> writerLock(s_writerMutex);
    RuntimeConfig validated = Sanitize(newCfg);
    // UI editing and persistence receive the same finite, range-checked values.
    s_baseConfig = validated;

    uint32_t seq = s_seq.load(std::memory_order_relaxed);
    
    // 1. Publish write-in-progress (odd seq) with release order
    s_seq.store(seq + 1, std::memory_order_release);
    
    // Release fence guarantees ARM visibility of the odd sequence before payload stores
    std::atomic_thread_fence(std::memory_order_release);

    uint64_t buffer[WORDS] = {0};
    std::memcpy(buffer, &validated, sizeof(RuntimeConfig));
    for (size_t i = 0; i < WORDS; ++i) {
        s_active[i].store(buffer[i], std::memory_order_relaxed);
    }

    // Release fence guarantees payload stores are completed before publishing write-completed
    std::atomic_thread_fence(std::memory_order_release);

    // 2. Publish write-completed (even seq) with release order
    s_seq.store(seq + 2, std::memory_order_release);

    // 3. Regenerate offline physics LUTs based on new config parameters
    movement::InitLUT();

    telemetry::ForensicEvent ev = { telemetry::ForensicTrapType::PROFILE_CHANGED, GetCurrentThreadId(), timing::NowUs(),
        (int32_t)validated.activeBrakeProfileIndex, (uint32_t)validated.bhopMode, (uint32_t)validated.bhopEnabled, true };
    telemetry::g_forensicBuffer.Push(ev);
    telemetry::RequestForensicFlush();
}

RuntimeConfig GetMutable() {
    std::lock_guard<std::mutex> writerLock(s_writerMutex);
    return s_baseConfig;
}

void Init() {
    std::lock_guard<std::mutex> writerLock(s_writerMutex);
    RuntimeConfig def{};
    s_baseConfig = def;
    uint64_t buffer[WORDS] = {0};
    std::memcpy(buffer, &def, sizeof(RuntimeConfig));
    for (size_t i = 0; i < WORDS; ++i) {
        s_active[i].store(buffer[i], std::memory_order_relaxed);
    }
    s_seq.store(0, std::memory_order_release);
}

} // namespace rcfg
