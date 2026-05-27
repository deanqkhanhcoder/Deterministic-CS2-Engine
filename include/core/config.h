#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Configuration Constants                 ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>

namespace cfg {

// § 1  CONFIGURATION — mirrors AHK CFG object
constexpr int    QUICK_TAP_MS        = 30;
constexpr int    MAX_SCALE_MS        = 80;
constexpr double CROUCH_MULT         = 0.75;
constexpr int    TAP_DELAY_MS        = 25;
constexpr int    SPRAY_DELAY_MS      = 120;
constexpr int    BURST_THRESHOLD     = 3;
constexpr int    SPACE_DELAY_MS      = 400;

constexpr int    NOISE_MIN           = 0;
constexpr int    NOISE_MAX           = 0;
constexpr int    LUT_MAX_MS          = 350;
constexpr double RELEASE_VELOCITY_WINDOW = 17.0;
constexpr bool   DEBUG_MODE          = false;

// Walk Memory
constexpr int    WALK_MEMORY_MS      = 130;
constexpr double WALK_RATIO_SKIP     = 0.65;
constexpr double WALK_RATIO_LIGHT    = 0.35;
constexpr int    MIN_WALK_STOP_MS    = 15;
constexpr int    WALK_MAX_STOP_MS    = 22;

// MIN_STOP
constexpr int    MIN_STOP_MS         = 4;

// Intent Velocity Penalty
constexpr double DECAY_K                          = 0.005;
constexpr int    DIRECTION_CHANGE_PENALTY_WINDOW_MS = 60;
constexpr int    TAP_SPAM_WINDOW_MS               = 60;
constexpr double STOP_STRENGTH_MIN                = 0.25;

// Latency Compensation
constexpr int    LATENCY_MARGIN_MS   = 6;

// Tap Spam EMA
constexpr double TAP_SPAM_ALPHA          = 0.15;
constexpr int    TAP_SPAM_HALF_LIFE_MS   = 150;

// Micro-tap filtering
constexpr int64_t MIN_TAP_US         = 2500;

// § 2  PHYSICS MODEL — CS2 Source 2 server CVars
namespace phys {
constexpr double MAX_SPEED       = 250.0;
constexpr double SV_FRICTION     = 5.2;
constexpr double SV_STOPSPEED    = 80.0;
constexpr double SV_ACCELERATE   = 5.5;
} // namespace phys

// Watchdog
constexpr int    WATCHDOG_INTERVAL_MS = 10000;
constexpr int    WATCHDOG_STUCK_MS    = 500;

// Click history buffer size
constexpr int    CLICK_HISTORY_MAX   = 8;
constexpr int    CLICK_HISTORY_WINDOW_MS = 500;

// Conflict penalty
constexpr double CONFLICT_INCREMENT  = 0.3;
constexpr double CONFLICT_DECREMENT  = 0.1;
constexpr int    CONFLICT_HALF_LIFE_MS = 400;

} // namespace cfg
