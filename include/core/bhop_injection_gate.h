#pragma once

#include "target_platform.h"

#include <utility>

namespace bhop::detail {

template <typename LiveIdentityFn, typename PublishedIdentityFn>
bool IsExpectedTargetActive(
    const target_platform::TargetIdentity& expected,
    LiveIdentityFn&& sampleLiveIdentity,
    PublishedIdentityFn&& loadPublishedIdentity) {
    if (!expected.IsValid()) return false;
    const auto live = std::forward<LiveIdentityFn>(sampleLiveIdentity)();
    if (live != expected) return false;
    if (std::forward<PublishedIdentityFn>(loadPublishedIdentity)() != expected) {
        return false;
    }
    // Re-sample after the publication check so the final operation before a
    // guarded backend call is a live full-identity validation.
    return std::forward<LiveIdentityFn>(sampleLiveIdentity)() == expected;
}

} // namespace bhop::detail