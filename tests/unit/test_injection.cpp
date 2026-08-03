#include "injection.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace target_platform {
bool IsExpectedTargetActive(const TargetIdentity&) noexcept { return false; }
} // namespace target_platform

namespace {

struct Call {
    UINT requested;
    std::vector<INPUT> inputs;
};

std::mutex g_fakeMutex;
std::vector<Call> g_calls;
std::vector<UINT> g_returns;
std::atomic<int> g_active{0};
std::atomic<int> g_maxActive{0};

bool ValidateDispatch(const void* context) noexcept {
    return *static_cast<const bool*>(context);
}

UINT FakeSendInput(UINT count, INPUT* inputs, int cbSize) {
    assert(cbSize == static_cast<int>(sizeof(INPUT)));
    const int active = ++g_active;
    int maximum = g_maxActive.load();
    while (active > maximum && !g_maxActive.compare_exchange_weak(maximum, active)) {}

    std::lock_guard<std::mutex> lock(g_fakeMutex);
    g_calls.push_back({count, std::vector<INPUT>(inputs, inputs + count)});
    const UINT result = g_returns.empty() ? count : g_returns.front();
    if (!g_returns.empty()) g_returns.erase(g_returns.begin());
    --g_active;
    return result;
}

void ResetFake(std::vector<UINT> results = {}) {
    std::lock_guard<std::mutex> lock(g_fakeMutex);
    g_calls.clear();
    g_returns = std::move(results);
    g_active = 0;
    g_maxActive = 0;
    injection::ResetForTesting();
    injection::SetSendInputBackendForTesting(FakeSendInput);
}

const INPUT& Sent(std::size_t call, std::size_t input) {
    return g_calls.at(call).inputs.at(input);
}

void AssertTaggedKeyboard(const INPUT& input, WORD scan, DWORD flags) {
    assert(input.type == INPUT_KEYBOARD);
    assert(input.ki.wScan == scan);
    assert(input.ki.dwFlags == flags);
    assert(input.ki.dwExtraInfo == injection::kInjectedInputMarker);
}

void TestGeneratedEventsAndExactResults() {
    ResetFake({1, 2, 0});
    assert(injection::KeyDown(Key::W) == 1);
    assert(injection::PendingReleaseCount() == 0);
    assert(injection::KeyDownUp(Key::A) == 2);
    assert(injection::MouseWheel(-WHEEL_DELTA) == 0);

    assert(g_calls.size() == 3);
    assert(g_calls[0].requested == 1);
    AssertTaggedKeyboard(Sent(0, 0), 0x11, KEYEVENTF_SCANCODE);
    assert(g_calls[1].requested == 2);
    AssertTaggedKeyboard(Sent(1, 0), 0x1E, KEYEVENTF_SCANCODE);
    AssertTaggedKeyboard(Sent(1, 1), 0x1E, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    assert(Sent(2, 0).type == INPUT_MOUSE);
    assert(Sent(2, 0).mi.dwFlags == MOUSEEVENTF_WHEEL);
    assert(Sent(2, 0).mi.mouseData == static_cast<DWORD>(-WHEEL_DELTA));
    assert(Sent(2, 0).mi.dwExtraInfo == injection::kInjectedInputMarker);
}

void TestReconcileIgnoresNormalHeldInputs() {
    ResetFake({1});
    assert(injection::KeyDown(Key::W) == 1);
    assert(injection::PendingReleaseCount() == 0);
    assert(injection::ReconcilePendingReleases() == 0);
    assert(g_calls.size() == 1);

    const bool accept = true;
    ResetFake({1});
    assert(injection::SpaceDownIf(ValidateDispatch, &accept) == 1);
    assert(injection::PendingReleaseCount() == 0);
    assert(injection::ReconcilePendingReleases() == 0);
    assert(g_calls.size() == 1);
}

void TestPartialKeyUpTracksAndReconcilesWithBoundedRetries() {
    ResetFake({1, 0, 0, 1});
    assert(injection::KeyDown(Key::S) == 1);
    assert(injection::PendingReleaseCount() == 0);
    assert(injection::KeyUp(Key::S) == 0);
    assert(injection::PendingReleaseCount() == 1);
    assert(injection::ReconcilePendingReleases() == 1);
    assert(injection::PendingReleaseCount() == 0);
    // One down; KeyUp has exactly two failed retries; reconciliation succeeds once.
    assert(g_calls.size() == 4);
    AssertTaggedKeyboard(Sent(1, 0), 0x1F, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    AssertTaggedKeyboard(Sent(2, 0), 0x1F, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
}

void TestPartialMouseClickTracksAndReconcilesLeftButton() {
    ResetFake({1, 1});
    assert(injection::KeyDownUp(Key::Mouse1) == 1);
    // A down/up helper owns the complete click lifecycle.  If SendInput only
    // accepts the down prefix, it must perform bounded release recovery before
    // returning rather than waiting until process shutdown.
    assert(injection::PendingReleaseCount() == 0);
    assert(g_calls.size() == 2);
    assert(Sent(0, 0).type == INPUT_MOUSE);
    assert(Sent(0, 0).mi.dwFlags == MOUSEEVENTF_LEFTDOWN);
    assert(Sent(0, 1).mi.dwFlags == MOUSEEVENTF_LEFTUP);
    assert(Sent(1, 0).mi.dwFlags == MOUSEEVENTF_LEFTUP);
    assert(Sent(1, 0).mi.dwExtraInfo == injection::kInjectedInputMarker);
}

void TestPartialKeyboardTapReconcilesBeforeReturning() {
    ResetFake({1, 1});
    assert(injection::KeyDownUp(Key::D) == 1);
    assert(injection::PendingReleaseCount() == 0);
    assert(g_calls.size() == 2);
    AssertTaggedKeyboard(Sent(1, 0), 0x20, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
}

void TestFailedRecoveryEntersReleaseOnlyMode() {
    ResetFake({1, 0, 0, 1, 1});
    assert(injection::KeyDownUp(Key::Mouse1) == 0);
    assert(injection::PendingReleaseCount() == 1);
    assert(g_calls.size() == 3);

    // No further non-release event reaches the backend while an accepted
    // synthetic down still lacks a matching up.
    assert(injection::KeyDown(Key::W) == 0);
    assert(injection::MouseWheel(-WHEEL_DELTA) == 0);
    assert(g_calls.size() == 3);

    assert(injection::ReconcilePendingReleases() == 1);
    assert(injection::PendingReleaseCount() == 0);
    assert(injection::KeyDown(Key::W) == 1);
}

void TestDispatchValidatorRunsInsideInjectionBoundary() {
    ResetFake({1});
    const bool reject = false;
    const bool accept = true;

    assert(injection::SpaceDownIf(ValidateDispatch, &reject) == 0);
    assert(g_calls.empty());
    assert(injection::MouseWheelIf(-WHEEL_DELTA,
                                   ValidateDispatch,
                                   &accept) == 1);
    assert(g_calls.size() == 1);
}

void TestGuardedMovementAndReleaseNeverReachRejectedForeground() {
    bool accept = false;
    ResetFake({1, 1});

    assert(injection::KeyDownIf(Key::A, ValidateDispatch, &accept) == 0);
    assert(g_calls.empty());

    accept = true;
    assert(injection::KeyDownIf(Key::A, ValidateDispatch, &accept) == 1);
    assert(g_calls.size() == 1);

    accept = false;
    assert(injection::KeyUpIf(Key::A, ValidateDispatch, &accept) == 0);
    assert(g_calls.size() == 1);
    assert(injection::PendingReleaseCount() == 1);
    assert(injection::ReconcilePendingReleasesIf(ValidateDispatch, &accept) == 0);
    assert(g_calls.size() == 1);

    accept = true;
    assert(injection::ReconcilePendingReleasesIf(ValidateDispatch, &accept) == 1);
    assert(g_calls.size() == 2);
    assert(injection::PendingReleaseCount() == 0);
    AssertTaggedKeyboard(Sent(1, 0), 0x1E,
                         KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
}

void TestFailsafeReleasesAllInputsAndKeepsFailuresPending() {
    ResetFake({1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    assert(injection::ShutdownAndRelease() == 3);
    assert(g_calls.size() == 9); // Successful releases stop; failed inputs use exactly two attempts.
    assert(injection::PendingReleaseCount() == 3);
    AssertTaggedKeyboard(Sent(0, 0), 0x11, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    AssertTaggedKeyboard(Sent(1, 0), 0x1F, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    AssertTaggedKeyboard(Sent(2, 0), 0x1E, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    AssertTaggedKeyboard(Sent(3, 0), 0x20, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    AssertTaggedKeyboard(Sent(5, 0), 0x39, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
    assert(Sent(7, 0).type == INPUT_MOUSE);
    assert(Sent(7, 0).mi.dwFlags == MOUSEEVENTF_LEFTUP);
    assert(Sent(7, 0).mi.dwExtraInfo == injection::kInjectedInputMarker);
}

void TestGuardedFailsafeNeverReleasesIntoRejectedTarget() {
    ResetFake();
    bool accept = false;
    assert(injection::ShutdownAndReleaseIf(ValidateDispatch, &accept) == 0);
    assert(g_calls.empty());
    assert(injection::PendingReleaseCount() == 6);

    accept = true;
    assert(injection::ReconcilePendingReleasesIf(
               ValidateDispatch, &accept) == 6);
    assert(g_calls.size() == 6);
    assert(injection::PendingReleaseCount() == 0);
}

void TestBackendIsSerializedAcrossThreads() {
    ResetFake();
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) {
        workers.emplace_back([] { assert(injection::KeyDown(Key::W) == 1); });
    }
    for (auto& worker : workers) worker.join();
    assert(g_calls.size() == 16);
    assert(g_maxActive == 1);
}

} // namespace

int main() {
    TestGeneratedEventsAndExactResults();
    TestReconcileIgnoresNormalHeldInputs();
    TestPartialKeyUpTracksAndReconcilesWithBoundedRetries();
    TestPartialMouseClickTracksAndReconcilesLeftButton();
    TestPartialKeyboardTapReconcilesBeforeReturning();
    TestFailedRecoveryEntersReleaseOnlyMode();
    TestDispatchValidatorRunsInsideInjectionBoundary();
    TestGuardedMovementAndReleaseNeverReachRejectedForeground();
    TestFailsafeReleasesAllInputsAndKeepsFailuresPending();
    TestGuardedFailsafeNeverReleasesIntoRejectedTarget();
    TestBackendIsSerializedAcrossThreads();
    std::puts("test_injection: all assertions passed");
    return 0;
}
