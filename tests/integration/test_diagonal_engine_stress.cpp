#include "../../src/core/engine_internal.h"
#include "injection.h"
#include "movement_reconstruction.h"
#include "runtime_config.h"
#include "state_engine.h"
#include "timing.h"

#include <windows.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace diagonal_test {
void SetLiveTarget(const target_platform::TargetIdentity& target);
target_platform::TargetIdentity GetLiveTarget();
void AdvanceUs(int64_t deltaUs);
uint64_t TakeTimer(Key key);
int64_t TimerDeadlineUs(Key key);
void ExpireDueTimers();
void SetTimerSchedulesBeforeFailure(int admissions);
void ResetScheduleTrace();
size_t ScheduleTraceCount();
Key ScheduledKey(size_t index);
}

namespace {
target_platform::TargetIdentity g_routedTarget;
target_platform::TargetIdentity g_foreignTarget;
std::atomic<bool> g_flipAfterSend{false};
std::atomic<int> g_foreignDispatches{0};
std::atomic<uint64_t> g_sendCalls{0};

UINT CaptureInputs(UINT count, INPUT*, int inputSize) {
    assert(inputSize == static_cast<int>(sizeof(INPUT)));
    if (diagonal_test::GetLiveTarget() != g_routedTarget) {
        g_foreignDispatches.fetch_add(1, std::memory_order_relaxed);
    }
    g_sendCalls.fetch_add(count, std::memory_order_relaxed);
    if (g_flipAfterSend.exchange(false, std::memory_order_acq_rel)) {
        diagonal_test::SetLiveTarget(g_foreignTarget);
    }
    return count;
}

target_platform::TargetIdentity Target(std::uintptr_t hwnd, DWORD pid, DWORD tid) {
    return {reinterpret_cast<HWND>(hwnd), pid, tid,
            static_cast<uint64_t>(hwnd) ^ (static_cast<uint64_t>(pid) << 32) ^ tid,
            1000 + pid};
}

void Expire(Key key) {
    const uint64_t timerId = diagonal_test::TakeTimer(key);
    if (timerId != 0) engine::OnTimerExpired(key, timerId);
}

void AssertLogicalMatchesAcceptedOutput() {
    const State state = engine::GetState();
    const std::uint32_t heldMask = injection::HeldMovementMask();
    for (int i = 0; i < 4; ++i) {
        assert(state.logical[i] == (state.nativeLogical[i] || (heldMask & (1u << i)) != 0));
    }
}

void AssertSettled() {
    const State state = engine::GetState();
    for (int i = 0; i < 4; ++i) {
        assert(!state.phys[i]);
        assert(!state.logical[i]);
        assert(state.expectedTimerId[i] == 0);
    }
    assert(injection::PendingReleaseCount() == 0);
    assert(state.socdReleaseTimerId[0] == 0 && state.socdReleaseTimerId[1] == 0);
}

void AssertSustainedHold(SocdMode mode, Key first) {
    RuntimeConfig config; config.socdMode = mode; rcfg::Apply(config);
    const Key second = keymap::Opposite[ki(first)];
    const Axis axis = keymap::KeyAxis[ki(first)];
    {
        std::lock_guard<std::mutex> lock(engine::s_stateMutex);
        engine::s_state.Reset();
    }
    diagonal_test::ResetScheduleTrace();
    const auto before = g_sendCalls.load();
    engine::HandleKeyDown(first, true, g_routedTarget);
    diagonal_test::AdvanceUs(200000);
    engine::HandleKeyDown(second, true, g_routedTarget);
    const auto start = timing::NowUs();
    const auto transitionOutput = g_sendCalls.load();
    const bool native = mode == SocdMode::OFF;
    const bool delayed = mode == SocdMode::HUMANIZED;
    assert(engine::GetState().logical[ki(first)] == (native || delayed));
    assert(engine::GetState().logical[ki(second)]);
    assert(diagonal_test::ScheduleTraceCount() == (delayed ? 1u : 0u));
    if (native) assert(transitionOutput == before);
    if (delayed) assert(diagonal_test::TimerDeadlineUs(first) == start + 8000);
    diagonal_test::AdvanceUs(7999);
    diagonal_test::ExpireDueTimers();
    assert(g_sendCalls.load() == transitionOutput);
    diagonal_test::AdvanceUs(1);
    diagonal_test::ExpireDueTimers();
    const auto settledOutput = g_sendCalls.load();
    assert(settledOutput == transitionOutput + (delayed ? 1 : 0));
    assert(engine::GetState().socdReleaseTimerId[ai(axis)] == 0);
    const int wish = second == Key::D || second == Key::W ? 1 : -1;
    for (int elapsedMs = 9; elapsedMs <= 2000; ++elapsedMs) {
        diagonal_test::AdvanceUs(1000);
        diagonal_test::ExpireDueTimers();
        engine::HandleKeyDown(first, true, g_routedTarget);
        engine::HandleKeyDown(second, true, g_routedTarget);
        engine::InjectionBatch batch(g_routedTarget);
        {
            std::lock_guard<std::mutex> lock(engine::s_stateMutex);
            engine::ResolveAxis(axis, batch);
        }
        engine::FlushAndCommitLogicalState(batch);
        const auto state = engine::GetState();
        assert(state.phys[ki(first)] && state.phys[ki(second)]);
        assert(state.logical[ki(first)] == native && state.logical[ki(second)]);
        assert(state.axisState[ai(axis)] == AxisState::Conflict);
        assert((axis == Axis::X ? state.vel.wishX : state.vel.wishY) == (native ? 0 : wish));
        assert(g_sendCalls.load() == settledOutput);
        assert(!timing::AreTimersActive());
        AssertLogicalMatchesAcceptedOutput();
    }
    assert(timing::NowUs() == start + 2000000);
    const auto state = engine::GetState();
    if (native) assert(state.vel.vx == 0 && state.vel.vy == 0);
    else assert((axis == Axis::X ? state.vel.vx : state.vel.vy) * wish > 249);
    engine::HandleKeyUp(first, true, g_routedTarget);
    engine::HandleKeyUp(second, true, g_routedTarget);
    diagonal_test::AdvanceUs(350000);
    diagonal_test::ExpireDueTimers();
    AssertSettled();
}

void AssertAutoBrake(SocdMode mode, bool physicalCounterTap = false) {
    RuntimeConfig config; config.socdMode = mode; rcfg::Apply(config);
    {
        std::lock_guard<std::mutex> lock(engine::s_stateMutex);
        engine::s_state.Reset();
    }
    diagonal_test::ResetScheduleTrace();
    engine::HandleKeyDown(Key::A, true, g_routedTarget);
    diagonal_test::AdvanceUs(600000);
    engine::HandleKeyUp(Key::A, true, g_routedTarget);
    const auto releaseUs = timing::NowUs();
    const auto deadline = diagonal_test::TimerDeadlineUs(Key::D);
    const auto brakeUs = deadline - releaseUs;
    assert(brakeUs >= 94000 && brakeUs <= 109000);
    assert(engine::g_lastBrakeUs.load() == brakeUs);
    assert(diagonal_test::ScheduleTraceCount() == 1);
    assert(diagonal_test::ScheduledKey(0) == Key::D);
    assert(engine::GetState().logical[ki(Key::D)]);
    assert(engine::IsCounterStrafeHoldingKey(Key::D, g_routedTarget));
    assert(!engine::IsCounterStrafeHoldingKey(Key::D, g_foreignTarget));
    const auto afterBurstStart = g_sendCalls.load();
    if (physicalCounterTap) {
        diagonal_test::AdvanceUs(20000);
        engine::HandleKeyDown(Key::D, true, g_routedTarget);
        diagonal_test::AdvanceUs(10000);
        engine::HandleKeyUp(Key::D, true, g_routedTarget);
        assert(diagonal_test::TimerDeadlineUs(Key::D) == deadline);
        assert(engine::GetState().logical[ki(Key::D)]);
    }
    diagonal_test::AdvanceUs(deadline - timing::NowUs() - 1);
    diagonal_test::ExpireDueTimers();
    assert(engine::IsCounterStrafeHoldingKey(Key::D, g_routedTarget));
    assert(g_sendCalls.load() == afterBurstStart);
    diagonal_test::AdvanceUs(1);
    diagonal_test::ExpireDueTimers();
    assert(!engine::IsCounterStrafeHoldingKey(Key::D, g_routedTarget));
    assert(std::hypot(engine::GetState().vel.vx, engine::GetState().vel.vy) < 34);
    diagonal_test::AdvanceUs(2000000);
    engine::CommitLogicalStateFromInjection();
    assert(engine::GetState().vel.vx == 0 && engine::GetState().vel.vy == 0);
    AssertSettled();
    std::cout << "Auto Counter-Strafe mode=" << static_cast<int>(mode)
              << " burst=" << brakeUs << " us, stationary after friction\n";
}
}

int main() {
    std::mutex watchdogMutex;
    std::condition_variable watchdogCv;
    bool done = false;
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(watchdogMutex);
        if (!watchdogCv.wait_for(lock, std::chrono::seconds(20), [&] { return done; })) {
            std::_Exit(124);
        }
    });

    rcfg::Init();
    timing::Init();
    movement::InitLUT();
    injection::ResetForTesting();
    injection::SetSendInputBackendForTesting(CaptureInputs);
    g_routedTarget = Target(1, 10, 20);
    g_foreignTarget = Target(2, 11, 21);
    diagonal_test::SetLiveTarget(g_routedTarget);

    {
        std::lock_guard<std::mutex> lock(engine::s_stateMutex);
        engine::s_state.Reset();
        engine::s_suspendedAtomic.store(false, std::memory_order_release);
    }

    // A routed event may sit behind focus-refresh work. Planning must not
    // publish logical ownership when dispatch-time validation rejects output.
    diagonal_test::SetLiveTarget(g_foreignTarget);
    engine::HandleKeyDown(Key::W, true, g_routedTarget);
    {
        const State rejected = engine::GetState();
        assert(rejected.phys[ki(Key::W)]);
        assert(!rejected.logical[ki(Key::W)]);
    }
    engine::HandleKeyUp(Key::W, false, g_foreignTarget);
    diagonal_test::SetLiveTarget(g_routedTarget);
    AssertSettled();

    // Removing Mouse1-triggered braking must not alter normal key-release
    // counter-strafe ordering: release-key overlap expires before counter brake.
    RuntimeConfig normalReleaseConfig = rcfg::Get();
    normalReleaseConfig.brakeProfiles[normalReleaseConfig.activeBrakeProfileIndex]
        .overlap_duration_us = 100000;
    normalReleaseConfig.socdMode = SocdMode::HUMANIZED;
    rcfg::Apply(normalReleaseConfig);
    diagonal_test::ResetScheduleTrace();
    engine::HandleKeyDown(Key::W, true, g_routedTarget);
    diagonal_test::AdvanceUs(200000);
    engine::HandleKeyUp(Key::W, true, g_routedTarget);
    assert(diagonal_test::ScheduleTraceCount() == 2);
    assert(diagonal_test::ScheduledKey(0) == Key::W);
    assert(diagonal_test::ScheduledKey(1) == Key::S);
    const uint64_t normalCounterTimer = diagonal_test::TakeTimer(Key::S);
    assert(normalCounterTimer != 0);
    const uint64_t normalOverlapTimer = diagonal_test::TakeTimer(Key::W);
    assert(normalOverlapTimer != 0);
    engine::OnTimerExpired(Key::W, normalOverlapTimer);
    engine::OnTimerExpired(Key::S, normalCounterTimer);
    AssertLogicalMatchesAcceptedOutput();
    AssertSettled();
    rcfg::Init();

    // A counter-key down without an armed release timer can stick forever.
    // Timer admission failure must fail closed: release physical W, but never
    // inject or publish opposite S ownership.
    diagonal_test::SetTimerSchedulesBeforeFailure(0);
    engine::HandleKeyDown(Key::W, true, g_routedTarget);
    diagonal_test::AdvanceUs(200000);
    engine::HandleKeyUp(Key::W, true, g_routedTarget);
    assert(diagonal_test::TakeTimer(Key::S) == 0);
    AssertLogicalMatchesAcceptedOutput();
    AssertSettled();
    diagonal_test::SetTimerSchedulesBeforeFailure(-1);

    // If overlap release arms but counter release cannot, cancel overlap and
    // emit neither side of partially protected counter-strafe.
    RuntimeConfig overlapConfig = rcfg::Get();
    overlapConfig.brakeProfiles[overlapConfig.activeBrakeProfileIndex]
        .overlap_duration_us = 100000;
    overlapConfig.socdMode = SocdMode::HUMANIZED;
    rcfg::Apply(overlapConfig);
    diagonal_test::SetTimerSchedulesBeforeFailure(1);
    engine::HandleKeyDown(Key::W, true, g_routedTarget);
    diagonal_test::AdvanceUs(200000);
    engine::HandleKeyUp(Key::W, true, g_routedTarget);
    assert(diagonal_test::TakeTimer(Key::S) == 0);
    assert(diagonal_test::TakeTimer(Key::W) == 0);
    AssertLogicalMatchesAcceptedOutput();
    AssertSettled();
    diagonal_test::SetTimerSchedulesBeforeFailure(-1);
    rcfg::Init();

    // SOCD mode never overrides the independent auto-brake profile overlap.
    RuntimeConfig full = rcfg::Get();
    full.brakeProfiles[full.activeBrakeProfileIndex].overlap_duration_us = 100000;
    rcfg::Apply(full);
    diagonal_test::ResetScheduleTrace();
    engine::HandleKeyDown(Key::W, true, g_routedTarget);
    diagonal_test::AdvanceUs(200000);
    engine::HandleKeyUp(Key::W, true, g_routedTarget);
    assert(diagonal_test::ScheduleTraceCount() == 2);
    assert(diagonal_test::ScheduledKey(0) == Key::W);
    Expire(Key::W); Expire(Key::S);
    AssertSettled();
    rcfg::Init();

    // FULL prioritizes the actual newest edge, including equal QPC timestamps.
    for (const auto first : {Key::A, Key::D, Key::W, Key::S}) {
        const auto opposite = keymap::Opposite[ki(first)];
        engine::HandleKeyDown(first, true, g_routedTarget);
        engine::HandleKeyDown(opposite, true, g_routedTarget);
        auto state = engine::GetState();
        assert(state.phys[ki(first)] && state.phys[ki(opposite)]);
        assert(!state.logical[ki(first)] && state.logical[ki(opposite)]);
        engine::HandleKeyUp(opposite, true, g_routedTarget);
        assert(engine::GetState().logical[ki(first)]);
        engine::HandleKeyUp(first, true, g_routedTarget);
        for (auto key : {first, opposite}) Expire(key);
        AssertSettled();
    }

    for (auto mode : {SocdMode::FULL, SocdMode::HUMANIZED, SocdMode::OFF}) {
        for (auto first : {Key::A, Key::D, Key::W, Key::S}) AssertSustainedHold(mode, first);
        AssertAutoBrake(mode);
        AssertAutoBrake(mode, true);
    }

    RuntimeConfig humanized; humanized.socdMode = SocdMode::HUMANIZED;
    rcfg::Apply(humanized);
    engine::HandleKeyDown(Key::A, true, g_routedTarget);
    engine::HandleKeyDown(Key::D, true, g_routedTarget);
    const auto staleTimer = engine::GetState().socdReleaseTimerId[ai(Axis::X)];
    assert(staleTimer != 0);
    diagonal_test::AdvanceUs(4000);
    engine::HandleKeyUp(Key::D, true, g_routedTarget);
    assert(engine::GetState().socdReleaseTimerId[ai(Axis::X)] == 0);
    assert(diagonal_test::TakeTimer(Key::A) == 0);
    engine::HandleKeyDown(Key::D, true, g_routedTarget);
    const auto newTimer = engine::GetState().socdReleaseTimerId[ai(Axis::X)];
    assert(newTimer != 0 && newTimer != staleTimer);
    const auto beforeStale = g_sendCalls.load();
    engine::OnTimerExpired(Key::A, staleTimer);
    assert(g_sendCalls.load() == beforeStale);
    assert(engine::GetState().logical[ki(Key::A)]);
    assert(engine::GetState().socdReleaseTimerId[ai(Axis::X)] == newTimer);
    diagonal_test::AdvanceUs(8000);
    diagonal_test::ExpireDueTimers();
    assert(!engine::GetState().logical[ki(Key::A)]);
    engine::HandleKeyUp(Key::A, true, g_routedTarget);
    engine::HandleKeyUp(Key::D, true, g_routedTarget);
    Expire(Key::A); Expire(Key::D);
    AssertSettled();

    // No timer means immediate priority; never leave both keys held forever.
    diagonal_test::SetTimerSchedulesBeforeFailure(0);
    engine::HandleKeyDown(Key::A, true, g_routedTarget);
    engine::HandleKeyDown(Key::D, true, g_routedTarget);
    assert(!engine::GetState().logical[ki(Key::A)] && engine::GetState().logical[ki(Key::D)]);
    assert(!timing::AreTimersActive());
    engine::HandleKeyUp(Key::A, true, g_routedTarget);
    engine::HandleKeyUp(Key::D, true, g_routedTarget);
    AssertSettled();
    diagonal_test::SetTimerSchedulesBeforeFailure(-1);

    // Cleanup cancels overlap; stale expiry cannot restore/release physical keys.
    engine::HandleKeyDown(Key::A, true, g_routedTarget);
    engine::HandleKeyDown(Key::D, true, g_routedTarget);
    const auto cleanupTimer = engine::GetState().socdReleaseTimerId[ai(Axis::X)];
    engine::ClearHeldKeys(g_routedTarget);
    const auto afterCleanup = g_sendCalls.load();
    assert(injection::HeldMovementMask() == 0 && !timing::AreTimersActive());
    engine::OnTimerExpired(Key::A, cleanupTimer);
    assert(g_sendCalls.load() == afterCleanup);
    engine::HandleKeyUp(Key::D, false, g_routedTarget);
    engine::HandleKeyUp(Key::A, false, g_routedTarget);
    AssertSettled();

    // OFF outside the routing scope never injects a brake, even on a long hold.
    RuntimeConfig off; off.socdMode = SocdMode::OFF; rcfg::Apply(off);
    const auto beforeInactive = g_sendCalls.load();
    engine::HandleKeyDown(Key::A, false, g_routedTarget);
    diagonal_test::AdvanceUs(600000);
    engine::HandleKeyUp(Key::A, false, g_routedTarget);
    assert(g_sendCalls.load() == beforeInactive && !timing::AreTimersActive());
    AssertSettled();
    rcfg::Init();

    constexpr int kCycles = 20000;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        diagonal_test::SetLiveTarget(g_routedTarget);
        engine::HandleKeyDown(Key::W, true, g_routedTarget);
        AssertLogicalMatchesAcceptedOutput();
        engine::HandleKeyDown(Key::A, true, g_routedTarget);
        AssertLogicalMatchesAcceptedOutput();
        diagonal_test::AdvanceUs(8000 + (cycle % 5000));

        if ((cycle % 3) == 0) g_flipAfterSend.store(true, std::memory_order_release);
        engine::HandleKeyUp(Key::W, true, g_routedTarget);
        AssertLogicalMatchesAcceptedOutput();

        if ((cycle % 5) == 0) diagonal_test::SetLiveTarget(g_foreignTarget);
        engine::HandleKeyUp(Key::A, true, g_routedTarget);
        AssertLogicalMatchesAcceptedOutput();

        diagonal_test::SetLiveTarget(g_routedTarget);
        (void)engine::ReconcilePendingOutput(g_routedTarget);
        Expire(Key::W);
        AssertLogicalMatchesAcceptedOutput();
        Expire(Key::S);
        AssertLogicalMatchesAcceptedOutput();
        Expire(Key::A);
        AssertLogicalMatchesAcceptedOutput();
        Expire(Key::D);
        AssertLogicalMatchesAcceptedOutput();
        (void)engine::ReconcilePendingOutput(g_routedTarget);
        AssertSettled();
    }

    assert(g_foreignDispatches.load(std::memory_order_acquire) == 0);
    assert(g_sendCalls.load(std::memory_order_acquire) > 0);
    injection::ResetForTesting();

    {
        std::lock_guard<std::mutex> lock(watchdogMutex);
        done = true;
    }
    watchdogCv.notify_one();
    watchdog.join();

    std::cout << "test_diagonal_engine_stress: " << kCycles
              << " diagonal/focus cycles stable, foreign dispatches=0\n";
    return 0;
}
