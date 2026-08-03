#include "runtime_config.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <atomic>
#include <thread>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void ExpectFinite(double value, const char* message) {
    Expect(std::isfinite(value), message);
}

} // namespace

int main() {
    RuntimeConfig invalid{};
    invalid.crouchMult = std::numeric_limits<double>::infinity();
    invalid.walkRatioSkip = -std::numeric_limits<double>::infinity();
    invalid.walkRatioLight = std::numeric_limits<double>::quiet_NaN();
    invalid.decayK = std::numeric_limits<double>::infinity();
    invalid.stopStrengthMin = std::numeric_limits<double>::quiet_NaN();
    invalid.tapSpamAlpha = -std::numeric_limits<double>::infinity();
    invalid.physMaxSpeed = std::numeric_limits<double>::infinity();
    invalid.physFriction = std::numeric_limits<double>::quiet_NaN();
    invalid.physStopSpeed = -std::numeric_limits<double>::infinity();
    invalid.physAccelerate = std::numeric_limits<double>::infinity();
    invalid.conflictIncrement = std::numeric_limits<double>::quiet_NaN();
    invalid.conflictDecrement = std::numeric_limits<double>::infinity();
    invalid.quickTapMs = -50;
    invalid.bhopMode = 99;
    invalid.activeBrakeProfileIndex = -1;
    invalid.humanizeMinUs = 500;
    invalid.humanizeMaxUs = -500;
    invalid.brakeProfiles[1].overlap_duration_us = -1;
    invalid.brakeProfiles[1].brake_bias_multiplier =
        std::numeric_limits<double>::infinity();
    invalid.brakeProfiles[1].authority_bias_ms =
        -std::numeric_limits<double>::infinity();
    invalid.brakeProfiles[1].aggressiveness_curve =
        std::numeric_limits<double>::quiet_NaN();
    invalid.brakeProfiles[1].momentum_memory_ms = -10.0;
    invalid.brakeProfiles[1].accuracyThreshold =
        std::numeric_limits<double>::quiet_NaN();
    invalid.minStopMs = 1000;
    invalid.lutMaxMs = 1;

    RuntimeConfig sanitized = rcfg::Sanitize(invalid);
    Expect(sanitized.crouchMult == 0.05, "infinite crouch multiplier is clamped");
    Expect(sanitized.walkRatioSkip == 0.01, "negative-infinite walk ratio is clamped");
    Expect(sanitized.walkRatioLight == 0.01, "NaN walk ratio is clamped");
    Expect(sanitized.decayK == 0.00001, "infinite decay is clamped");
    Expect(sanitized.stopStrengthMin == 0.01, "NaN stop strength is clamped");
    Expect(sanitized.tapSpamAlpha == 0.01, "negative-infinite EMA alpha is clamped");
    Expect(sanitized.physMaxSpeed == 1.0, "infinite max speed is clamped");
    Expect(sanitized.physFriction == 0.01, "NaN friction is clamped");
    Expect(sanitized.physStopSpeed == 1.0, "negative-infinite stop speed is clamped");
    Expect(sanitized.physAccelerate == 0.01, "infinite acceleration is clamped");
    Expect(sanitized.conflictIncrement == 0.01, "NaN conflict increment is clamped");
    Expect(sanitized.conflictDecrement == 0.01, "infinite conflict decrement is clamped");
    Expect(sanitized.quickTapMs == 1, "negative timing is clamped");
    Expect(sanitized.bhopMode == 4, "invalid bhop mode falls back deterministically");
    Expect(sanitized.activeBrakeProfileIndex == 1, "invalid brake profile falls back deterministically");
    Expect(sanitized.humanizeMaxUs == sanitized.humanizeMinUs,
           "humanize range is ordered");
    Expect(sanitized.brakeProfiles[1].overlap_duration_us == 0,
           "negative overlap is clamped");
    Expect(sanitized.brakeProfiles[1].brake_bias_multiplier == 1.0,
           "invalid brake multiplier falls back to profile default");
    Expect(sanitized.brakeProfiles[1].authority_bias_ms == 0.0,
           "invalid authority bias falls back to profile default");
    Expect(sanitized.brakeProfiles[1].aggressiveness_curve == 1.0,
           "invalid brake curve falls back to profile default");
    Expect(sanitized.brakeProfiles[1].momentum_memory_ms == 0.0,
           "negative momentum memory is clamped");
    Expect(sanitized.brakeProfiles[1].accuracyThreshold == 1.0,
           "accuracy threshold is bounded by sanitized max speed");
    Expect(sanitized.minStopMs <= sanitized.lutMaxMs,
           "counter-strafe duration bounds remain ordered");

    RuntimeConfig invalidProfileOnly{};
    invalidProfileOnly.brakeProfiles[1].accuracyThreshold =
        std::numeric_limits<double>::quiet_NaN();
    const RuntimeConfig profileFallback = rcfg::Sanitize(invalidProfileOnly);
    Expect(profileFallback.brakeProfiles[1].accuracyThreshold == 50.0,
           "invalid accuracy threshold falls back to rifle default");

    ExpectFinite(sanitized.crouchMult, "crouch multiplier is finite");
    ExpectFinite(sanitized.walkRatioSkip, "walk ratio skip is finite");
    ExpectFinite(sanitized.walkRatioLight, "walk ratio light is finite");
    ExpectFinite(sanitized.decayK, "decay is finite");
    ExpectFinite(sanitized.stopStrengthMin, "stop strength is finite");
    ExpectFinite(sanitized.tapSpamAlpha, "EMA alpha is finite");
    ExpectFinite(sanitized.physMaxSpeed, "max speed is finite");
    ExpectFinite(sanitized.physFriction, "friction is finite");
    ExpectFinite(sanitized.physStopSpeed, "stop speed is finite");
    ExpectFinite(sanitized.physAccelerate, "acceleration is finite");
    ExpectFinite(sanitized.conflictIncrement, "conflict increment is finite");
    ExpectFinite(sanitized.conflictDecrement, "conflict decrement is finite");

    const RuntimeConfig sanitizedAgain = rcfg::Sanitize(invalid);
    Expect(sanitizedAgain.physFriction == sanitized.physFriction,
           "Sanitize is deterministic");
    Expect(sanitizedAgain.activeBrakeProfileIndex == sanitized.activeBrakeProfileIndex,
           "Sanitize normalizes profile selection");

    rcfg::Init();
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};
    auto toggleBhop = [&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < 2000; ++i) {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.bhopEnabled = !cfg.bhopEnabled;
            rcfg::Apply(cfg);
        }
    };
    auto cycleProfile = [&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < 2000; ++i) {
            RuntimeConfig cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = (cfg.activeBrakeProfileIndex % 4) + 1;
            rcfg::Apply(cfg);
            const RuntimeConfig snapshot = rcfg::Get();
            if (snapshot.activeBrakeProfileIndex < 1 ||
                snapshot.activeBrakeProfileIndex > 4) {
                failed.store(true, std::memory_order_release);
            }
        }
    };
    std::thread writerA(toggleBhop);
    std::thread writerB(cycleProfile);
    std::thread reader([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < 200000; ++i) {
            const RuntimeConfig snapshot = rcfg::Get();
            if (snapshot.activeBrakeProfileIndex < 1 ||
                snapshot.activeBrakeProfileIndex > 4 ||
                snapshot.minStopMs > snapshot.lutMaxMs) {
                failed.store(true, std::memory_order_release);
            }
        }
    });
    start.store(true, std::memory_order_release);
    writerA.join();
    writerB.join();
    reader.join();
    Expect(!failed.load(std::memory_order_acquire),
           "concurrent config edits preserve a valid published profile");

    std::cout << "PASS: runtime config validation\n";
    return 0;
}
