#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Runtime Configuration                            ║
// ║  Replaces compile-time config.h with mutable, persistable values    ║
// ║  Thread-safe: double-buffered with atomic pointer swap              ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>
#include <atomic>

struct RuntimeConfig {
    // ── Counter-Strafe ──
    int    quickTapMs        = 30;
    int    maxScaleMs        = 80;
    double crouchMult        = 0.75;
    int    latencyMarginMs   = 6;
    int    minStopMs         = 4;
    int    lutMaxMs          = 350;

    // Walk Memory
    int    walkMemoryMs      = 130;
    double walkRatioSkip     = 0.65;
    double walkRatioLight    = 0.35;
    int    minWalkStopMs     = 15;
    int    walkMaxStopMs     = 22;

    // Intent Velocity
    double decayK            = 0.005;
    int    dirChangePenaltyMs= 60;
    int    tapSpamWindowMs   = 60;
    double stopStrengthMin   = 0.25;

    // Tap Spam EMA
    double tapSpamAlpha      = 0.15;
    int    tapSpamHalfLifeMs = 150;

    // Micro-tap
    int64_t minTapUs         = 2500;

    // Physics
    double physMaxSpeed      = 250.0;
    double physFriction      = 5.2;
    double physStopSpeed     = 80.0;
    double physAccelerate    = 5.5;

    // Watchdog
    int    watchdogIntervalMs= 10000;
    int    watchdogStuckMs   = 500;

    // Conflict penalty
    double conflictIncrement = 0.3;
    double conflictDecrement = 0.1;
    int    conflictHalfLifeMs= 400;

    // Noise
    int    noiseMin          = 0;
    int    noiseMax          = 0;

    // Movement Evolution
    int    hardwareDebounceUs= 2000;
    int    humanizeMinUs     = -100;
    int    humanizeMaxUs     = 200;
    
    struct BrakeProfile {
        int64_t overlap_duration_us;
        double  brake_bias_multiplier;
        double  authority_bias_ms;
        double  aggressiveness_curve;
        double  momentum_memory_ms;
        double  accuracyThreshold;
    };
    
    BrakeProfile brakeProfiles[5] = {
        { 0,    1.0,  0.0, 1.0, 40.0, 75.0 }, // [0] Unused / Off
        { 2000, 1.0,  0.0, 1.0, 35.0, 50.0 }, // [1] Rifle
        { 0,    0.85,-2.0, 1.1, 25.0, 80.0 }, // [2] Pistol
        { 4000, 1.15, 3.0, 0.9, 40.0, 30.0 }, // [3] Sniper
        { 1000, 0.8, -4.0, 1.2, 20.0, 70.0 }  // [4] SMG
    };
    int activeBrakeProfileIndex = 1; // Default to Rifle

    // ── Bhop ──
    bool   bhopEnabled       = false;
    int    bhopMode          = 4;     // 1=Legit, 2=Aggressive, 3=Humanized, 4=Scroll
    int    airborneDelayMs   = 350;
    int    scrollBurstGapMs  = 2;

    // Profile-specific flexible timings: collapsed into unified scalar properties
    int    landingScanMs     = 450;
    int    airborneLockMs    = 350;
    int    spamIntervalMs    = 2;

    // Per-mode timing [mode 1-4][hold min, hold max, delay min, delay max]
    struct ModeTimings {
        int hMin, hMax, dMin, dMax;
    };
    ModeTimings modeCfg[5] = {
        {0,0,0,0},             // [0] unused
        {12, 18, 18, 25},     // [1] Legit
        { 5,  8,  4,  8},     // [2] Aggressive
        { 8, 15, 12, 22},     // [3] Humanized
        { 0,  0,  4,  8},     // [4] Scroll
    };

    // ── Application ──
    bool   minimizeToTray    = true;
    int    dashboardRefreshMs= 250;
    bool   debugMode         = false;
    bool   safeModeEnabled   = false;
};

// ── Global config accessor (seqlock, true lock-free read) ──
namespace rcfg {
    // Get current active config (called from any thread — lock-free)
    // Returns by value to ensure snapshot consistency via seqlock.
    RuntimeConfig Get();

    // Apply new config (called from main/UI thread only)
    void Apply(const RuntimeConfig& newCfg);

    // Get mutable reference for editing (UI thread only, before Apply)
    RuntimeConfig& GetMutable();

    // Initialize with defaults
    void Init();
}
