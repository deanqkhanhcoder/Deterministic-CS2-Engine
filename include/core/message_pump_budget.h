#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace message_pump {

constexpr std::size_t kMaxMessagesPerPass = 32;
constexpr int64_t kDispatchBudgetUs = 5000;

struct DrainResult {
    bool quit = false;
    std::size_t dispatched = 0;
    int64_t elapsedUs = 0;
    UINT slowestMessage = 0;
    HWND slowestHwnd = nullptr;
    int64_t slowestDispatchUs = 0;
};

template <typename NowUs>
DrainResult DrainPendingMessages(MSG& msg, NowUs&& nowUs) {
    DrainResult result;
    const int64_t startUs = nowUs();

    while (result.dispatched < kMaxMessagesPerPass &&
           PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            result.quit = true;
            break;
        }
        const UINT dispatchedMessage = msg.message;
        const HWND dispatchedHwnd = msg.hwnd;
        const int64_t dispatchStartUs = nowUs();
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        const int64_t dispatchEndUs = nowUs();
        const int64_t dispatchUs = dispatchEndUs - dispatchStartUs;
        if (dispatchUs > result.slowestDispatchUs) {
            result.slowestMessage = dispatchedMessage;
            result.slowestHwnd = dispatchedHwnd;
            result.slowestDispatchUs = dispatchUs;
        }
        ++result.dispatched;
        if (dispatchEndUs - startUs >= kDispatchBudgetUs) break;
    }

    result.elapsedUs = nowUs() - startUs;
    return result;
}

} // namespace message_pump
