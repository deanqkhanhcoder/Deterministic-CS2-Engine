#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Physics Engine                          ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "types.h"
#include "state.h"
#include <cstdint>
#include "runtime_config.h"

namespace movement {

// True Source Engine 2D Velocity Estimator
// Reconstructs the (v_x, v_y) vector based on how long keys were held,
// taking into account the sv_maxspeed circular clamping.
void EstimateTrueVelocity2D(int64_t heldUsX, int64_t heldUsY, int signX, int signY, const RuntimeConfig& rc, double& outVx, double& outVy);

// Multi-factor intent velocity attenuation (applies tap penalty, conflict penalty, walk, etc)
// Returns a scaling factor [0.0, 1.0] to multiply the true velocity by.
double CalcIntentEfficiency(Key k, int64_t heldUs, const State& state, const RuntimeConfig& rc);

// Precalculate offline 2D Hybrid LUT (called once at startup and config changes)
void InitLUT();

// O(1) Deterministic LUT Fetch
// Returns the stop duration in milliseconds for the given 2D velocity and wish_mode.
// wish_mode: 0 = orthogonal released, 1 = orthogonal held, 2 = orthogonal counter-strafed
int LookupStopDur2D(double vx, double vy, int wish_mode, bool crouch);

// Calculates strength scale.

// Stop strength = clamp(intentV / MAX_SPEED, [MIN, 1.0])
double CalcStopStrength(double intentVelocity);

} // namespace movement
