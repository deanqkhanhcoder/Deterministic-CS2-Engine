#include "../../src/core/engine_internal.h"
#include "injection.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

target_platform::TargetIdentity g_liveTarget;
std::vector<INPUT> g_sent;

UINT CaptureInputs(UINT count, INPUT* inputs, int inputSize) {
    assert(inputSize == static_cast<int>(sizeof(INPUT)));
    g_sent.insert(g_sent.end(), inputs, inputs + count);
    return count;
}

target_platform::TargetIdentity Target(std::uintptr_t hwnd, DWORD pid, DWORD tid) {
    return {reinterpret_cast<HWND>(hwnd), pid, tid,
            static_cast<uint64_t>(hwnd) ^ (static_cast<uint64_t>(pid) << 32) ^ tid,
            1000 + pid};
}

bool IsKeyUp(const INPUT& input) {
    return input.type == INPUT_KEYBOARD &&
           (input.ki.dwFlags & KEYEVENTF_KEYUP) != 0;
}

} // namespace

namespace target_platform {

bool IsExpectedTargetActive(const TargetIdentity& expected) noexcept {
    return expected.IsValid() && expected == g_liveTarget;
}

} // namespace target_platform

int main() {
    injection::ResetForTesting();
    injection::SetSendInputBackendForTesting(CaptureInputs);

    const auto routedTarget = Target(1, 10, 20);
    const auto foreignTarget = Target(2, 11, 21);
    g_liveTarget = routedTarget;

    engine::InjectionBatch diagonalDown(routedTarget);
    diagonalDown.push(Key::W, true);
    diagonalDown.push(Key::A, true);
    diagonalDown.flush();
    assert(g_sent.size() == 2);
    assert(!IsKeyUp(g_sent[0]) && !IsKeyUp(g_sent[1]));

    g_liveTarget = foreignTarget;
    engine::InjectionBatch rejectedRetarget(foreignTarget);
    rejectedRetarget.push(Key::W, true);
    rejectedRetarget.flush();
    assert(g_sent.size() == 2);

    engine::InjectionBatch rejectedRelease(routedTarget);
    rejectedRelease.push(Key::W, false);
    rejectedRelease.push(Key::A, false);
    rejectedRelease.flush();
    assert(g_sent.size() == 2);
    assert(injection::PendingReleaseCount() == 2);

    engine::InjectionBatch rejectedDiagonal(routedTarget);
    rejectedDiagonal.push(Key::S, true);
    rejectedDiagonal.push(Key::D, true);
    rejectedDiagonal.flush();
    assert(g_sent.size() == 2);

    // Pending releases remain bound to target that accepted corresponding down.
    assert(injection::ReconcilePendingReleasesForTarget(foreignTarget) == 0);
    assert(injection::PendingReleaseCount() == 2);
    assert(g_sent.size() == 2);

    g_liveTarget = routedTarget;
    const UINT released =
        injection::ReconcilePendingReleasesForTarget(routedTarget);
    assert(released == 2);
    assert(injection::PendingReleaseCount() == 0);
    assert(g_sent.size() == 4);
    assert(IsKeyUp(g_sent[2]) && IsKeyUp(g_sent[3]));

    injection::ResetForTesting();
    std::cout << "test_engine_dispatch_gate: diagonal focus race contained\n";
    return 0;
}
