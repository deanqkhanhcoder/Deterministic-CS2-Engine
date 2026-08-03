#include "bhop_injection_gate.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

target_platform::TargetIdentity Identity(std::uintptr_t hwnd,
                                         DWORD pid,
                                         DWORD tid,
                                         uint64_t identity,
                                         uint64_t processStartTime = 100) {
    return {reinterpret_cast<HWND>(hwnd), pid, tid, identity,
            processStartTime};
}

void TestRequiresPublishedAndFinalLiveIdentityChecks() {
    const auto expected = Identity(0x101, 41, 73, 1);
    const auto reusedHwnd = Identity(0x101, 42, 74, 2);
    std::vector<int> order;

    const bool reusedResult = bhop::detail::IsExpectedTargetActive(
        expected,
        [&] { order.push_back(1); return reusedHwnd; },
        [&] { order.push_back(2); return expected; });
    assert(!reusedResult);
    assert((order == std::vector<int>{1}));

    order.clear();
    const bool staleResolvedTarget = bhop::detail::IsExpectedTargetActive(
        expected,
        [&] { order.push_back(1); return expected; },
        [&] { order.push_back(2); return reusedHwnd; });
    assert(!staleResolvedTarget);
    assert((order == std::vector<int>{1, 2}));

    order.clear();
    const bool stable = bhop::detail::IsExpectedTargetActive(
        expected,
        [&] { order.push_back(1); return expected; },
        [&] { order.push_back(2); return expected; });
    assert(stable);
    assert((order == std::vector<int>{1, 2, 1}));

    order.clear();
    int liveSamples = 0;
    const bool changedAfterPublication = bhop::detail::IsExpectedTargetActive(
        expected,
        [&] {
            order.push_back(1);
            return ++liveSamples == 1 ? expected : reusedHwnd;
        },
        [&] { order.push_back(2); return expected; });
    assert(!changedAfterPublication);
    assert((order == std::vector<int>{1, 2, 1}));
}

} // namespace

int main() {
    TestRequiresPublishedAndFinalLiveIdentityChecks();
    std::puts("test_bhop_injection_gate: all assertions passed");
    return 0;
}
