#include "movement_reconstruction.h"
#include "runtime_config.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace movement;

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    rcfg::Init();
    InitLUT();
    const RuntimeConfig rc = rcfg::Get();

    double diagonalVx = 0.0;
    double diagonalVy = 0.0;
    double diagonalBaselineVx = 0.0;
    double diagonalBaselineVy = 0.0;
    EstimateTrueVelocity2D(INT64_MAX, INT64_MAX, 1, 1, rc,
                           diagonalVx, diagonalVy);
    EstimateTrueVelocity2D(50 * 15625, 50 * 15625, 1, 1, rc,
                           diagonalBaselineVx, diagonalBaselineVy);
    Expect(std::isfinite(diagonalVx) && std::isfinite(diagonalVy),
           "diagonal velocity must remain finite for saturated hold duration");
    Expect(diagonalVx == diagonalBaselineVx && diagonalVy == diagonalBaselineVy,
           "saturated diagonal hold must use LUT boundary tick");

    EstimateTrueVelocity2D(-15625, -15625, 1, 1, rc,
                           diagonalVx, diagonalVy);
    EstimateTrueVelocity2D(0, 0, 1, 1, rc,
                           diagonalBaselineVx, diagonalBaselineVy);
    Expect(diagonalVx == diagonalBaselineVx && diagonalVy == diagonalBaselineVy,
           "negative diagonal hold must clamp to zero tick");

    const int legacyNarrow = LookupStopDur2D(250.0, 0.0, 0, false);
    const int nonFiniteLookup = LookupStopDur2D(
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), 0, false);
    const int saturatedLookup = LookupStopDur2D(
        std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::infinity(), 0, false);
    const int rifle = CalculateStopDur2D(250.0, 0.0, 0, false, 50.0, rc);
    const int pistol = CalculateStopDur2D(250.0, 0.0, 0, false, 80.0, rc);
    const int alreadyAccurate =
        CalculateStopDur2D(40.0, 0.0, 0, false, 50.0, rc);
    const int rifleBrake =
        ShapeBrakeDurationMs(rifle, rc.brakeProfiles[1], rc);

    RuntimeConfig::BrakeProfile extreme = rc.brakeProfiles[1];
    extreme.brake_bias_multiplier = 2.0;
    extreme.authority_bias_ms = 100.0;
    extreme.aggressiveness_curve = 4.0;
    const int boundedExtreme = ShapeBrakeDurationMs(1563, extreme, rc);

    RuntimeConfig::BrakeProfile nonFinite = rc.brakeProfiles[1];
    nonFinite.brake_bias_multiplier =
        std::numeric_limits<double>::infinity();
    const int boundedNonFinite = ShapeBrakeDurationMs(100, nonFinite, rc);

    RuntimeConfig invertedBounds = rc;
    invertedBounds.minStopMs = 1000;
    invertedBounds.lutMaxMs = 1;
    const int boundedInverted =
        ShapeBrakeDurationMs(100, rc.brakeProfiles[1], invertedBounds);

    Expect(legacyNarrow == 109, "17 u/s legacy threshold remains deterministic");
    Expect(nonFiniteLookup >= 0 && saturatedLookup >= 0,
           "non-finite and out-of-range velocity must not enter float-to-int UB");
    Expect(rifle == 94, "rifle 50 u/s threshold releases one tick earlier");
    Expect(pistol == 78, "pistol 80 u/s threshold releases two ticks earlier");
    Expect(alreadyAccurate == 0,
           "velocity already below profile threshold must not counter-strafe");
    Expect(ShapeBrakeDurationMs(0, rc.brakeProfiles[1], rc) == 0,
           "zero stop duration must not become a minimum synthetic hold");
    Expect(rifleBrake == 88,
           "rifle profile shaping includes the six millisecond latency margin");
    Expect(boundedExtreme == rc.lutMaxMs,
           "extreme profile shaping must remain inside the safety horizon");
    Expect(boundedNonFinite == rc.lutMaxMs,
           "non-finite profile shaping must fail closed at the safety horizon");
    Expect(boundedInverted == 1,
           "inverted duration bounds must fail closed without std::clamp UB");
    Expect(ClampCounterOverlapUs(1000000, 4000) == 3900,
           "overlap must be bounded by the computed brake hold");
    Expect(ClampCounterOverlapUs(-100, 4000) == 0,
           "negative overlap must be disabled");
    Expect(ClampCounterOverlapUs(1000, 0) == 0,
           "zero brake duration must not retain overlap");
    Expect(pistol < rifle && rifle < legacyNarrow,
           "larger accuracy threshold must reduce brake duration");

    std::cout << "test_physics: profile thresholds "
              << pistol << " < " << rifle << " < " << legacyNarrow << " ms\n";
    return 0;
}
