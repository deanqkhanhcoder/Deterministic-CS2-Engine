#include "target_platform.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

namespace {

target_platform::TargetIdentity Identity(std::uintptr_t hwnd,
                                         DWORD pid,
                                         DWORD tid,
                                         uint64_t identity,
                                         uint64_t processStartTime = 100) {
    return {reinterpret_cast<HWND>(hwnd), pid, tid, identity,
            processStartTime};
}

void TestCompleteIdentityPublication() {
    using namespace target_platform;
    detail::TargetPublicationStore store;
    TargetProfile profile{L"fake", {}, L"fake.exe", CAP_CSTRAFE};
    const TargetIdentity expected = Identity(0x101, 41, 73, 0xAABBCCDD);

    store.Store(&profile, expected);
    const TargetPublication actual = store.Load();
    assert(actual.profile == &profile);
    assert(actual.target == expected);
    assert(actual.target.pid == expected.pid);
    assert(actual.target.tid == expected.tid);
    assert(actual.target.processStartTime == expected.processStartTime);
}

void TestStaleResolutionPolicy() {
    using namespace target_platform;
    const TargetIdentity first = Identity(0x101, 41, 73, 1);
    const TargetIdentity second = Identity(0x202, 42, 74, 2);
    const TargetIdentity none{};

    assert(detail::ShouldPublishResolution(first, first));
    assert(!detail::ShouldPublishResolution(first, second));
    assert(!detail::ShouldPublishResolution(first, none));
    assert(detail::ShouldPublishResolution(none, none));
    assert(!detail::ShouldPublishResolution(none, second));
}

void TestIdentityComparisonRejectsHwndReuseEvenIfHashCollides() {
    using namespace target_platform;
    const auto first = Identity(0x101, 41, 73, 0xAABBCCDD);
    const auto reused = Identity(0x101, 42, 72, 0xAABBCCDD);
    assert(IsForegroundTarget(first, first));
    assert(!IsForegroundTarget(reused, first));

    const auto recycledProcess = Identity(0x101, 41, 73, 0xAABBCCDD, 101);
    assert(!IsForegroundTarget(recycledProcess, first));
}

void TestStableForegroundSampleRejectsIdentityChangeBehindSameHwnd() {
    using namespace target_platform;
    const HWND hwnd = reinterpret_cast<HWND>(0x101);
    const auto first = Identity(0x101, 41, 73, 0xAABBCCDD);
    const auto reused = Identity(0x101, 42, 74, 0xAABBCCDE);

    assert(detail::IsStableForegroundSample(hwnd, first, hwnd, first));
    assert(!detail::IsStableForegroundSample(hwnd, first, hwnd, reused));
    assert(!detail::IsStableForegroundSample(hwnd, first,
                                              reinterpret_cast<HWND>(0x202), first));
}

void TestReadersNeverObserveMixedPublication() {
    using namespace target_platform;
    detail::TargetPublicationStore store;
    TargetProfile firstProfile{L"first", {}, L"first.exe", CAP_BHOP};
    TargetProfile secondProfile{L"second", {}, L"second.exe", CAP_SCROLL};
    const TargetIdentity first = Identity(0x101, 41, 73, 1);
    const TargetIdentity second = Identity(0x202, 42, 74, 2);
    store.Store(&firstProfile, first);

    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < 100000; ++i) {
            store.Store((i & 1) == 0 ? &secondProfile : &firstProfile,
                        (i & 1) == 0 ? second : first);
        }
    });
    std::thread reader([&] {
        start.store(true, std::memory_order_release);
        for (int i = 0; i < 100000; ++i) {
            const TargetPublication value = store.Load();
            const bool isFirst = value.profile == &firstProfile && value.target == first &&
                                 value.target.pid == first.pid && value.target.tid == first.tid;
            const bool isSecond = value.profile == &secondProfile && value.target == second &&
                                  value.target.pid == second.pid && value.target.tid == second.tid;
            if (!isFirst && !isSecond) failed.store(true, std::memory_order_relaxed);
        }
    });
    writer.join();
    reader.join();
    assert(!failed.load(std::memory_order_relaxed));
}

} // namespace

int main() {
    TestCompleteIdentityPublication();
    TestStaleResolutionPolicy();
    TestIdentityComparisonRejectsHwndReuseEvenIfHashCollides();
    TestStableForegroundSampleRejectsIdentityChangeBehindSameHwnd();
    TestReadersNeverObserveMixedPublication();
    return 0;
}
