#include "message_pump_budget.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iterator>

namespace {

void PostMessages(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        assert(PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0, 0));
    }
}

std::size_t DrainAllWithZeroClock() {
    MSG msg{};
    std::size_t total = 0;
    for (;;) {
        const auto result = message_pump::DrainPendingMessages(msg, [] {
            return int64_t{0};
        });
        total += result.dispatched;
        if (result.quit || result.dispatched == 0) return total;
    }
}

} // namespace

int main() {
    MSG msg{};
    (void)PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    PostMessages(1000);

    int64_t clockUs = 0;
    const auto timeLimited = message_pump::DrainPendingMessages(msg, [&] {
        const int64_t current = clockUs;
        clockUs += 500;
        return current;
    });
    assert(timeLimited.dispatched == 5);
    assert(timeLimited.dispatched < 1000);
    assert(timeLimited.elapsedUs >= message_pump::kDispatchBudgetUs);

    const auto countLimited = message_pump::DrainPendingMessages(msg, [] {
        return int64_t{0};
    });
    assert(countLimited.dispatched == message_pump::kMaxMessagesPerPass);

    const std::size_t remaining = DrainAllWithZeroClock();
    assert(remaining == 995 - message_pump::kMaxMessagesPerPass);

    constexpr UINT kSlowMessage = WM_APP + 2;
    assert(PostThreadMessageW(GetCurrentThreadId(), kSlowMessage, 0, 0));
    const int64_t slowClock[] = {0, 0, 300000, 300000};
    std::size_t slowClockIndex = 0;
    const auto slow = message_pump::DrainPendingMessages(msg, [&] {
        assert(slowClockIndex < std::size(slowClock));
        return slowClock[slowClockIndex++];
    });
    assert(slow.dispatched == 1);
    assert(slow.slowestMessage == kSlowMessage);
    assert(slow.slowestDispatchUs == 300000);

    assert(PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, 0, 0));
    const auto quit = message_pump::DrainPendingMessages(msg, [] {
        return int64_t{0};
    });
    assert(quit.quit);

    std::printf("message_pump_budget: time=5 count=32 remaining=%zu\n", remaining);
    return 0;
}
