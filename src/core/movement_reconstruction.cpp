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

static int SimulateStopDurationMs(double vx,
                           double vy,
                           int wishMode,
                           double releaseVelocityWindow,
                           const RuntimeConfig& rc) {
    double curVx = std::abs(vx);
    double curVy = std::abs(vy);
    if (curVx <= releaseVelocityWindow) return 0;

    double wishX = curVx > 0.0 ? -1.0 : 0.0;
    double wishY = 0.0;
    if (wishMode == 1) wishY = curVy > 0.0 ? 1.0 : 0.0;
    else if (wishMode == 2) wishY = curVy > 0.0 ? -1.0 : 0.0;
    else if (wishMode == 3) wishY = curVy > 0.0 ? -1.0 : 1.0;

    const double wishMagnitude = std::sqrt(wishX * wishX + wishY * wishY);
    if (wishMagnitude > 0.001) {
        wishX /= wishMagnitude;
        wishY /= wishMagnitude;
    }

    constexpr double dt = 1.0 / 64.0;
    int ticks = 0;
    double previousVx = curVx;
    while (ticks < 100 && curVx > releaseVelocityWindow) {
        previousVx = curVx;
        const double speed = std::sqrt(curVx * curVx + curVy * curVy);
        const double control = speed < rc.physStopSpeed ? rc.physStopSpeed : speed;
        const double newSpeed = std::max(0.0, speed - control * rc.physFriction * dt);
        const double frictionScale = speed > 0.0 ? newSpeed / speed : 0.0;

        double frictionVx = curVx * frictionScale;
        double frictionVy = curVy * frictionScale;
        const double currentSpeed = frictionVx * wishX + frictionVy * wishY;
        const double addSpeed = rc.physMaxSpeed - currentSpeed;
        if (addSpeed > 0.0) {
            const double accelSpeed = std::min(
                rc.physAccelerate * dt * rc.physMaxSpeed,
                addSpeed);
            frictionVx += accelSpeed * wishX;
            frictionVy += accelSpeed * wishY;
        }

        curVx = frictionVx;
        curVy = frictionVy;
        ++ticks;
    }

    double exactTicks = static_cast<double>(ticks);
    if (ticks > 0 && curVx <= releaseVelocityWindow &&
        previousVx > releaseVelocityWindow && previousVx != curVx) {
        const double fraction =
            (previousVx - releaseVelocityWindow) / (previousVx - curVx);
        exactTicks = static_cast<double>(ticks - 1) + fraction;
    }

    const double pureMs = exactTicks * 15.625;
    const double alignedMs = std::ceil(pureMs / 15.625) * 15.625;
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
                double speed = std::sqrt(vx*vx + vy*vy);
                double fric_k = rc.physFriction * dt;
                
                // PM_Friction
                if (speed > 0.1) {
                    double control = (speed < rc.physStopSpeed) ? rc.physStopSpeed : speed;
                    double drop = control * fric_k;
                    double newspeed = speed - drop;
                    if (newspeed < 0) newspeed = 0;
                    double f_scale = newspeed / speed;
                    vx *= f_scale;
                    vy *= f_scale;
                }
                
                // PM_Accelerate
                double wish_x = 0.0, wish_y = 0.0;
                
                if (i < (max_t - min_t)) {
                    if (x_longer) wish_x = 1.0;
                    else wish_y = 1.0;
                } else {
                    wish_x = 1.0; wish_y = 1.0;
                }
                
                double w_mag = std::sqrt(wish_x*wish_x + wish_y*wish_y);
                if (w_mag > 0.001) { wish_x /= w_mag; wish_y /= w_mag; }
                
                double currentspeed = vx * wish_x + vy * wish_y;
                double addspeed = rc.physMaxSpeed - currentspeed;
                
                if (addspeed > 0) {
                    double accelspeed = rc.physAccelerate * dt * rc.physMaxSpeed;
                    if (accelspeed > addspeed) accelspeed = addspeed;
                    vx += accelspeed * wish_x;
                    vy += accelspeed * wish_y;
                }
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
    constexpr int64_t kTickUs = 15625;
    constexpr int64_t kMaxHeldUs = 50 * kTickUs;
    const int ticksX = static_cast<int>(
        std::clamp(heldUsX, int64_t{0}, kMaxHeldUs) / kTickUs);
    const int ticksY = static_cast<int>(
        std::clamp(heldUsY, int64_t{0}, kMaxHeldUs) / kTickUs);

    auto lut = s_lut.load(std::memory_order_acquire);
    if (!lut) {
        outVx = 0.0;
        outVy = 0.0;
        return;
    }
    
    outVx = lut->velocity[ticksX][ticksY][0] * signX;
    outVy = lut->velocity[ticksX][ticksY][1] * signY;
    
    // Apply speed limits if walking
    // This is a simplification for walking
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
