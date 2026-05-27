#include "movement_reconstruction.h"
#include "config.h"
#include "runtime_config.h"
#include "timing.h"
#include "debug_logger.h"
#include <cmath>
#include <algorithm>

namespace movement {

static double s_velocityLUT[51][51][2]; // [usX/tick][usY/tick][axis (0=X, 1=Y)]
static int s_stopLUT[251][251][4]; // [vx][vy][wish_mode]

void InitLUT() {
    const RuntimeConfig& rc = rcfg::Get();
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
            
            s_velocityLUT[t1][t2][0] = vx;
            s_velocityLUT[t1][t2][1] = vy;
        }
    }

    // 2. Generate 2D Hybrid LUT
    for (int vx = 0; vx <= 250; ++vx) {
        for (int vy = 0; vy <= 250; ++vy) {
            for (int mode = 0; mode <= 3; ++mode) {
                if (vx == 0 && vy == 0) {
                    s_stopLUT[vx][vy][mode] = 0;
                    continue;
                }
                
                double cur_vx = vx;
                double cur_vy = vy;
                
                double wish_x = (vx > 0) ? -1.0 : 0.0;
                double wish_y = 0.0;
                if (mode == 1) wish_y = (vy > 0) ? 1.0 : 0.0;
                else if (mode == 2) wish_y = (vy > 0) ? -1.0 : 0.0;
                else if (mode == 3) wish_y = (vy > 0) ? -1.0 : 1.0;
                
                double w_mag = std::sqrt(wish_x*wish_x + wish_y*wish_y);
                if (w_mag > 0.001) { wish_x /= w_mag; wish_y /= w_mag; }
                
                int ticks = 0;
                double prev_vx = cur_vx;
                
                while (ticks < 100) {
                    if (cur_vx <= cfg::RELEASE_VELOCITY_WINDOW) {
                        break;
                    }
                    
                    prev_vx = cur_vx;
                    double speed = std::sqrt(cur_vx*cur_vx + cur_vy*cur_vy);
                    
                    double control = (speed < rc.physStopSpeed) ? rc.physStopSpeed : speed;
                    double drop = control * rc.physFriction * dt;
                    double newspeed = speed - drop;
                    if (newspeed < 0) newspeed = 0;
                    double f_scale = (speed > 0) ? (newspeed / speed) : 0;
                    
                    double f_vx = cur_vx * f_scale;
                    double f_vy = cur_vy * f_scale;
                    
                    double currentspeed = f_vx * wish_x + f_vy * wish_y;
                    double addspeed = rc.physMaxSpeed - currentspeed;
                    
                    if (addspeed > 0) {
                        double accelspeed = rc.physAccelerate * dt * rc.physMaxSpeed;
                        if (accelspeed > addspeed) accelspeed = addspeed;
                        f_vx += accelspeed * wish_x;
                        f_vy += accelspeed * wish_y;
                    }
                    
                    cur_vx = f_vx;
                    cur_vy = f_vy;
                    ticks++;
                }
                
                double exact_ticks = (double)ticks;
                if (ticks > 0 && cur_vx <= cfg::RELEASE_VELOCITY_WINDOW && prev_vx > cfg::RELEASE_VELOCITY_WINDOW) {
                    double frac = (prev_vx - cfg::RELEASE_VELOCITY_WINDOW) / (prev_vx - cur_vx);
                    exact_ticks = (ticks - 1) + frac;
                }
                
                double pure_ms = exact_ticks * 15.625;
                double aligned_ms = std::ceil(pure_ms / 15.625) * 15.625;
                s_stopLUT[vx][vy][mode] = (int)(aligned_ms + 0.5);
            }
        }
    }
    DLOG_INFO(Runtime, "movement::InitLUT() built 2D Matrix.");
}

void EstimateTrueVelocity2D(int64_t heldUsX, int64_t heldUsY, int signX, int signY, const RuntimeConfig& rc, double& outVx, double& outVy) {
    (void)rc;
    int ticksX = (int)(heldUsX / 15625);
    int ticksY = (int)(heldUsY / 15625);
    if (ticksX > 50) ticksX = 50;
    if (ticksY > 50) ticksY = 50;
    
    outVx = s_velocityLUT[ticksX][ticksY][0] * signX;
    outVy = s_velocityLUT[ticksX][ticksY][1] * signY;
    
    // Apply speed limits if walking
    // This is a simplification for walking
}

int LookupStopDur2D(double vx, double vy, int wish_mode, bool crouch) {
    (void)crouch;
    int idx_x = std::clamp((int)std::abs(vx), 0, 250);
    int idx_y = std::clamp((int)std::abs(vy), 0, 250);
    int mode = std::clamp(wish_mode, 0, 3);
    return s_stopLUT[idx_x][idx_y][mode];
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
