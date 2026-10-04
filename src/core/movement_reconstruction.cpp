#include "movement_reconstruction.h"
#include "config.h"
#include "runtime_config.h"
#include "timing.h"
#include "debug_logger.h"
#include <cmath>
#include <algorithm>
#include <atomic>
#include <memory>

namespace movement {

struct MovementLutSnapshot {
    double velocity[51][51][2]; // [usX/tick][usY/tick][axis (0=X, 1=Y)]
    int stop[251][251][4];      // [vx][vy][wish_mode]
};

static std::atomic<std::shared_ptr<const MovementLutSnapshot>> s_lut;

// Valve PM_Friction (CGameMovement::Friction). Runs BEFORE accelerate.
static void PmFriction(double& vx, double& vy, const RuntimeConfig& rc, double dt) {
    const double speed = std::sqrt(vx * vx + vy * vy);
    if (speed < 0.1) return;
    const double control = speed < rc.physStopSpeed ? rc.physStopSpeed : speed;
    const double newSpeed = std::max(0.0, speed - control * rc.physFriction * dt);
    const double scale = newSpeed / speed;
    vx *= scale;
    vy *= scale;
}

// Valve PM_Accelerate (CGameMovement::Accelerate). wishdir need not be unit;
// wishspeed = sv_maxspeed. currentspeed < 0 (opposing key) gives
// addspeed > wishspeed, so accelspeed is only capped by accel*dt*wishspeed.
static void PmAccelerate(double& vx, double& vy, double wishX, double wishY,
                         double wishSpeed, const RuntimeConfig& rc, double dt) {
    const double mag = std::sqrt(wishX * wishX + wishY * wishY);
    if (mag < 0.001) return;
    wishX /= mag;
    wishY /= mag;
    const double currentSpeed = vx * wishX + vy * wishY;
    const double addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0) return;
    const double accelSpeed = std::min(rc.physAccelerate * dt * wishSpeed, addSpeed);
    vx += accelSpeed * wishX;
    vy += accelSpeed * wishY;
}

void AdvanceVelocity(VelocityTracker& t, int64_t nowUs, const RuntimeConfig& rc) {
    constexpr double kTickS = 1.0 / 64.0;
    constexpr int kMaxSteps = 256; // 4 s; speed is ~0 long before that
    if (t.lastUs < 0) {
        t.lastUs = nowUs;
        return;
    }
    if (nowUs <= t.lastUs) {
        return;
    }
    double remaining = static_cast<double>(nowUs - t.lastUs) * 1e-6;
    t.lastUs = nowUs;
    const double wishSpeed = rc.physMaxSpeed * t.speedScale;
    for (int i = 0; i < kMaxSteps && remaining > 1e-9; ++i) {
        const double dt = std::min(remaining, kTickS); // sub-tick tail step
        remaining -= dt;
        PmFriction(t.vx, t.vy, rc, dt);
        PmAccelerate(t.vx, t.vy, t.wishX, t.wishY, wishSpeed, rc, dt);
        if (t.wishX == 0 && t.wishY == 0 && t.vx == 0.0 && t.vy == 0.0) break;
    }
    // Friction leaves < 0.1 u/s residue only as exact zero via stopspeed; snap.
    if (t.wishX == 0 && t.wishY == 0 && std::hypot(t.vx, t.vy) < 0.1) {
        t.vx = t.vy = 0.0;
    }
}

static int SimulateStopDurationMs(double vx,
                           double vy,
                           int wishMode,
                           double releaseVelocityWindow,
                           const RuntimeConfig& rc) {
    double curVx = std::abs(vx);
    double curVy = std::abs(vy);
    if (curVx <= releaseVelocityWindow) return 0;

    // Frame of reference: velocity along +X/+Y, so opposing key = -X.
    const double wishX = -1.0;
    double wishY = 0.0;
    if (wishMode == 1) wishY = curVy > 0.0 ? 1.0 : 0.0;
    else if (wishMode == 2) wishY = curVy > 0.0 ? -1.0 : 0.0;
    else if (wishMode == 3) wishY = curVy > 0.0 ? -1.0 : 1.0;

    constexpr double dt = 1.0 / 64.0;
    int ticks = 0;
    double previousVx = curVx;
    while (ticks < 100 && curVx > releaseVelocityWindow) {
        previousVx = curVx;
        PmFriction(curVx, curVy, rc, dt);
        PmAccelerate(curVx, curVy, wishX, wishY, rc.physMaxSpeed, rc, dt);
        ++ticks;
    }

    // Sub-tick crossing time, then round UP to a whole tick: the engine
    // only integrates whole ticks of the held key.
    double exactTicks = static_cast<double>(ticks);
    if (ticks > 0 && curVx <= releaseVelocityWindow &&
        previousVx > releaseVelocityWindow && previousVx != curVx) {
        exactTicks = static_cast<double>(ticks - 1) +
            (previousVx - releaseVelocityWindow) / (previousVx - curVx);
    }

    const double alignedMs = std::ceil(exactTicks) * 15.625;
    return static_cast<int>(alignedMs + 0.5);
}

void InitLUT() {
    const RuntimeConfig& rc = rcfg::Get();
    auto next = std::make_shared<MovementLutSnapshot>();
    double dt = 1.0 / 64.0;
    
    // 1. Generate 2D Velocity Accumulation LUT
    for (int t1 = 0; t1 <= 50; ++t1) {
        for (int t2 = 0; t2 <= 50; ++t2) {
            double vx = 0.0, vy = 0.0;
            int min_t = std::min(t1, t2);
            int max_t = std::max(t1, t2);
            bool x_longer = (t1 > t2);
            
            for (int i = 0; i < max_t; ++i) {
                PmFriction(vx, vy, rc, dt);

                double wish_x = 0.0, wish_y = 0.0;
                if (i < (max_t - min_t)) {
                    if (x_longer) wish_x = 1.0;
                    else wish_y = 1.0;
                } else {
                    wish_x = 1.0; wish_y = 1.0;
                }
                PmAccelerate(vx, vy, wish_x, wish_y, rc.physMaxSpeed, rc, dt);
            }
            
            next->velocity[t1][t2][0] = vx;
            next->velocity[t1][t2][1] = vy;
        }
    }

    // 2. Generate 2D Hybrid LUT
    for (int vx = 0; vx <= 250; ++vx) {
        for (int vy = 0; vy <= 250; ++vy) {
            for (int mode = 0; mode <= 3; ++mode) {
                next->stop[vx][vy][mode] = SimulateStopDurationMs(
                    static_cast<double>(vx),
                    static_cast<double>(vy),
                    mode,
                    cfg::RELEASE_VELOCITY_WINDOW,
                    rc);
            }
        }
    }
    std::shared_ptr<const MovementLutSnapshot> published = next;
    s_lut.store(published, std::memory_order_release);
    // DLOG_INFO(Runtime, "movement::InitLUT() built 2D Matrix.");
}

void EstimateTrueVelocity2D(int64_t heldUsX, int64_t heldUsY, int signX, int signY, const RuntimeConfig& rc, double& outVx, double& outVy) {
    (void)rc;
    constexpr double kTickUs = 15625.0;
    constexpr int kMaxTicks = 50;

    auto lut = s_lut.load(std::memory_order_acquire);
    if (!lut) {
        outVx = 0.0;
        outVy = 0.0;
        return;
    }

    // Sub-tick: a key pressed mid-tick contributes a fractional accelerate
    // step. Flooring to whole ticks under-estimated speed (-> early brake).
    const auto split = [&](int64_t us, int& lo, int& hi, double& frac) {
        const double t = std::clamp(static_cast<double>(std::max<int64_t>(us, 0)) / kTickUs,
                                    0.0, static_cast<double>(kMaxTicks));
        lo = static_cast<int>(t);
        hi = std::min(lo + 1, kMaxTicks);
        frac = t - lo;
    };
    int x0, x1, y0, y1;
    double fx, fy;
    split(heldUsX, x0, x1, fx);
    split(heldUsY, y0, y1, fy);

    for (int axis = 0; axis < 2; ++axis) {
        const double v00 = lut->velocity[x0][y0][axis];
        const double v10 = lut->velocity[x1][y0][axis];
        const double v01 = lut->velocity[x0][y1][axis];
        const double v11 = lut->velocity[x1][y1][axis];
        const double v = (v00 * (1 - fx) + v10 * fx) * (1 - fy) +
                         (v01 * (1 - fx) + v11 * fx) * fy;
        (axis == 0 ? outVx : outVy) = v * (axis == 0 ? signX : signY);
    }
}

int LookupStopDur2D(double vx, double vy, int wish_mode, bool crouch) {
    (void)crouch;
    const auto velocityIndex = [](double value) {
        if (std::isnan(value)) return 0;
        if (!std::isfinite(value)) return 250;
        const double magnitude = std::abs(value);
        if (magnitude >= 250.0) return 250;
        return static_cast<int>(magnitude);
    };
    const int idx_x = velocityIndex(vx);
    const int idx_y = velocityIndex(vy);
    int mode = std::clamp(wish_mode, 0, 3);
    auto lut = s_lut.load(std::memory_order_acquire);
    if (!lut) return 0;
    return lut->stop[idx_x][idx_y][mode];
}

int CalculateStopDur2D(double vx,
                       double vy,
                       int wish_mode,
                       bool crouch,
                       double releaseVelocityWindow,
                       const RuntimeConfig& rc) {
    (void)crouch;
    const double threshold = std::isfinite(releaseVelocityWindow)
        ? std::clamp(releaseVelocityWindow, 0.0, rc.physMaxSpeed)
        : cfg::RELEASE_VELOCITY_WINDOW;
    return SimulateStopDurationMs(vx, vy, std::clamp(wish_mode, 0, 3), threshold, rc);
}

int ShapeBrakeDurationMs(int pureDurMs,
                         const RuntimeConfig::BrakeProfile& profile,
                         const RuntimeConfig& rc) {
    if (pureDurMs <= 0) return 0;

    const double scaled = static_cast<double>(pureDurMs) *
                          profile.brake_bias_multiplier;
    const double normalized = std::max(0.01, scaled / 31.25);
    const double shaped = std::pow(normalized, profile.aggressiveness_curve) *
                          31.25 + profile.authority_bias_ms;

    // Do not cast an unbounded or non-finite double to int. Apart from being
    // undefined behaviour, it can create a random negative/huge timer and an
    // excessive synthetic hold.
    if (!std::isfinite(shaped)) return rc.lutMaxMs;

    const double compensated = shaped - static_cast<double>(rc.latencyMarginMs);
    const int safeMaxMs = std::max(1, rc.lutMaxMs);
    const int safeMinMs = std::clamp(rc.minStopMs, 1, safeMaxMs);
    const double bounded = std::clamp(
        compensated,
        static_cast<double>(safeMinMs),
        static_cast<double>(safeMaxMs));
    return static_cast<int>(std::round(bounded));
}

int64_t ClampCounterOverlapUs(int64_t requestedOverlapUs, int64_t brakeUs) {
    if (requestedOverlapUs <= 0 || brakeUs <= 100) return 0;
    return std::min(requestedOverlapUs, brakeUs - 100);
}

double CalcIntentEfficiency(Key k, int64_t heldUs, const State& state, const RuntimeConfig& rc) {
    (void)k;
    (void)state;
    if (heldUs < rc.minTapUs) return 0.0;
    return 1.0;
}

double CalcStopStrength(double intentVelocity) {
    double strength = intentVelocity / 250.0;
    return std::clamp(strength, 0.2, 1.0);
}

} // namespace movement
