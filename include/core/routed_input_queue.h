#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

template <typename T, std::size_t Capacity>
class RoutedInputQueue {
    static_assert(Capacity > 0);

public:
    bool TryPush(const T& value) noexcept {
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false;
        entries_[static_cast<std::size_t>(head % Capacity)] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& value) noexcept {
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const uint64_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        value = entries_[static_cast<std::size_t>(tail % Capacity)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::size_t Size() const noexcept {
        const uint64_t head = head_.load(std::memory_order_acquire);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(head - tail);
    }

    void Clear() noexcept {
        T ignored{};
        while (TryPop(ignored)) {}
    }

    template <typename Handler, typename NowUs>
    std::size_t DrainBounded(std::size_t maxEvents, int64_t budgetUs,
                             Handler&& handle, NowUs&& nowUs) {
        if (maxEvents == 0 || budgetUs <= 0) return 0;
        const int64_t startUs = nowUs();
        std::size_t drained = 0;
        T value{};
        while (drained < maxEvents && TryPop(value)) {
            handle(value);
            ++drained;
            if (nowUs() - startUs >= budgetUs) break;
        }
        return drained;
    }

private:
    std::array<T, Capacity> entries_{};
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
};

class RoutedInputWakeGate {
public:
    bool RequestWake() noexcept {
        return !pending_.exchange(true, std::memory_order_acq_rel);
    }

    template <typename Empty>
    bool CompleteDrain(Empty&& empty) noexcept {
        if (!empty()) return true;
        pending_.store(false, std::memory_order_release);
        if (empty()) return false;
        return !pending_.exchange(true, std::memory_order_acq_rel);
    }

    void Reset() noexcept {
        pending_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> pending_{false};
};
