#include "routed_input_queue.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    RoutedInputQueue<int, 4> bounded;
    assert(bounded.TryPush(1));
    assert(bounded.TryPush(2));
    assert(bounded.TryPush(3));
    assert(bounded.TryPush(4));
    assert(!bounded.TryPush(5));
    assert(bounded.Size() == 4);

    for (int expected = 1; expected <= 4; ++expected) {
        int value = 0;
        assert(bounded.TryPop(value));
        assert(value == expected);
    }
    int empty = 0;
    assert(!bounded.TryPop(empty));

    RoutedInputQueue<int, 64> budgeted;
    for (int value = 0; value < 48; ++value) assert(budgeted.TryPush(value));
    int handled = 0;
    int64_t fakeNowUs = 0;
    const std::size_t firstPass = budgeted.DrainBounded(
        8, 100,
        [&](int value) {
            assert(value == handled);
            ++handled;
            fakeNowUs += 30;
        },
        [&] { return fakeNowUs; });
    assert(firstPass == 4);
    assert(handled == 4);
    assert(budgeted.Size() == 44);

    RoutedInputWakeGate wakeGate;
    assert(wakeGate.RequestWake());
    assert(!wakeGate.RequestWake());
    assert(wakeGate.CompleteDrain([&] { return budgeted.Size() == 0; }));
    while (budgeted.DrainBounded(8, 1000, [&](int) {}, [&] { return fakeNowUs; }) != 0) {}
    assert(!wakeGate.CompleteDrain([&] { return budgeted.Size() == 0; }));
    assert(wakeGate.RequestWake());

    assert(budgeted.TryPush(99));
    budgeted.Clear();
    assert(budgeted.Size() == 0);

    RoutedInputQueue<int, 256> concurrent;
    constexpr int kEvents = 100000;
    std::atomic<bool> start{false};
    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int value = 0; value < kEvents; ++value) {
            while (!concurrent.TryPush(value)) std::this_thread::yield();
        }
    });
    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int expected = 0; expected < kEvents; ++expected) {
            int value = -1;
            while (!concurrent.TryPop(value)) std::this_thread::yield();
            assert(value == expected);
        }
    });
    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    assert(concurrent.Size() == 0);

    std::cout << "test_routed_input_queue: bounded FIFO stable\n";
    return 0;
}
