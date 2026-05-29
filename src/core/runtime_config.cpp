// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Runtime Configuration Implementation             ║
// ║  Seqlock pattern: true lock-free concurrent reads, safe atomic sync ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "runtime_config.h"
#include <thread>
#include <cstring>
#include <cmath>
#include <immintrin.h>
#include "movement_reconstruction.h"

namespace rcfg {

constexpr size_t WORDS = (sizeof(RuntimeConfig) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
static std::atomic<uint64_t> s_active[WORDS];
static std::atomic<uint32_t> s_seq{0};
static RuntimeConfig s_staging;  // UI thread scratch space
static RuntimeConfig s_baseConfig; // Pure user config without SafeMode overrides

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

void Apply(const RuntimeConfig& newCfg) {
    // 1. Store the pure user configuration (used for UI and saving)
    s_baseConfig = newCfg;
    
    // 2. Prepare the validated config to be published to the physics runtime
    RuntimeConfig validated = newCfg;

    // --- Clamping & Validation Bounds Layer ---
    // Clamping to physically sensible ranges to protect physics, timing, and wait logic from NaN/Inf/Negative values.
    if (std::isnan(validated.crouchMult) || validated.crouchMult < 0.05) validated.crouchMult = 0.05;
    if (validated.crouchMult > 5.0) validated.crouchMult = 5.0;

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

    if (validated.walkMemoryMs < 1) validated.walkMemoryMs = 1;
    if (validated.walkMemoryMs > 2000) validated.walkMemoryMs = 2000;

    if (std::isnan(validated.walkRatioSkip) || validated.walkRatioSkip < 0.01) validated.walkRatioSkip = 0.01;
    if (validated.walkRatioSkip > 5.0) validated.walkRatioSkip = 5.0;

    if (std::isnan(validated.walkRatioLight) || validated.walkRatioLight < 0.01) validated.walkRatioLight = 0.01;
    if (validated.walkRatioLight > 5.0) validated.walkRatioLight = 5.0;

    if (validated.minWalkStopMs < 1) validated.minWalkStopMs = 1;
    if (validated.minWalkStopMs > 1000) validated.minWalkStopMs = 1000;

    if (validated.walkMaxStopMs < 1) validated.walkMaxStopMs = 1;
    if (validated.walkMaxStopMs > 1000) validated.walkMaxStopMs = 1000;

    if (std::isnan(validated.decayK) || validated.decayK < 0.0001) validated.decayK = 0.0001;
    if (validated.decayK > 1.0) validated.decayK = 1.0;

    if (validated.dirChangePenaltyMs < 1) validated.dirChangePenaltyMs = 1;
    if (validated.dirChangePenaltyMs > 1000) validated.dirChangePenaltyMs = 1000;

    if (validated.tapSpamWindowMs < 1) validated.tapSpamWindowMs = 1;
    if (validated.tapSpamWindowMs > 1000) validated.tapSpamWindowMs = 1000;

    if (std::isnan(validated.stopStrengthMin) || validated.stopStrengthMin < 0.01) validated.stopStrengthMin = 0.01;
    if (validated.stopStrengthMin > 5.0) validated.stopStrengthMin = 5.0;

    if (std::isnan(validated.tapSpamAlpha) || validated.tapSpamAlpha < 0.01) validated.tapSpamAlpha = 0.01;
    if (validated.tapSpamAlpha > 1.0) validated.tapSpamAlpha = 1.0;

    if (validated.tapSpamHalfLifeMs < 1) validated.tapSpamHalfLifeMs = 1;
    if (validated.tapSpamHalfLifeMs > 5000) validated.tapSpamHalfLifeMs = 5000;

    if (validated.minTapUs < 1) validated.minTapUs = 1;
    if (validated.minTapUs > 1000000) validated.minTapUs = 1000000;

    // Physics
    if (std::isnan(validated.physMaxSpeed) || validated.physMaxSpeed < 1.0) validated.physMaxSpeed = 1.0;
    if (validated.physMaxSpeed > 10000.0) validated.physMaxSpeed = 10000.0;

    if (std::isnan(validated.physFriction) || validated.physFriction < 0.01) validated.physFriction = 0.01;
    if (validated.physFriction > 100.0) validated.physFriction = 100.0;

    if (std::isnan(validated.physStopSpeed) || validated.physStopSpeed < 1.0) validated.physStopSpeed = 1.0;
    if (validated.physStopSpeed > 10000.0) validated.physStopSpeed = 10000.0;

    if (std::isnan(validated.physAccelerate) || validated.physAccelerate < 0.01) validated.physAccelerate = 0.01;
    if (validated.physAccelerate > 100.0) validated.physAccelerate = 100.0;

    // Conflict
    if (std::isnan(validated.conflictIncrement) || validated.conflictIncrement < 0.01) validated.conflictIncrement = 0.01;
    if (validated.conflictIncrement > 10.0) validated.conflictIncrement = 10.0;

    if (std::isnan(validated.conflictDecrement) || validated.conflictDecrement < 0.01) validated.conflictDecrement = 0.01;
    if (validated.conflictDecrement > 10.0) validated.conflictDecrement = 10.0;

    if (validated.conflictHalfLifeMs < 1) validated.conflictHalfLifeMs = 1;
    if (validated.conflictHalfLifeMs > 5000) validated.conflictHalfLifeMs = 5000;

    // Bhop Mode Index
    if (validated.bhopMode < 1 || validated.bhopMode > 4) validated.bhopMode = 4;
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
}

RuntimeConfig& GetMutable() {
    s_staging = s_baseConfig;
    return s_staging;
}

void Init() {
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
