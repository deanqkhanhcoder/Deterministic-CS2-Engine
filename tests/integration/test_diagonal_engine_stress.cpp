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
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace diagonal_test {
void SetLiveTarget(const target_platform::TargetIdentity& target);
target_platform::TargetIdentity GetLiveTarget();
void AdvanceUs(int64_t deltaUs);
uint64_t TakeTimer(Key key);
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
        assert(state.logical[i] == ((heldMask & (1u << i)) != 0));
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
