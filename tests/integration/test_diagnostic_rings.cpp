#include "analysis_toolkit.h"
#include "telemetry.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace timing {

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace timing

int main() {
    telemetry::MetricBuffer metric;
    analysis::Init();

    constexpr int kWriters = 4;
    constexpr int kSamplesPerWriter = 20000;
    std::atomic<bool> running{true};

    std::thread reader([&] {
        while (running.load(std::memory_order_acquire)) {
            int64_t p50 = 0, p99 = 0, average = 0;
            metric.GetStats(p50, p99, average);
            assert(average >= 0 && average <= kWriters);

            analysis::LatencyStats hook{}, jitter{}, oversleep{};
            analysis::SpikeCluster spikes{};
            analysis::CoreStats cores[64]{};
            uint32_t active = 0, migrations = 0, smt = 0;
            analysis::AnalyzeSession(hook, jitter, oversleep, spikes, cores,
                                     active, migrations, smt);
            assert(active <= 64);
        }
    });

    std::vector<std::thread> writers;
    for (int writer = 0; writer < kWriters; ++writer) {
        writers.emplace_back([&, writer] {
            for (int sample = 0; sample < kSamplesPerWriter; ++sample) {
                metric.Add(writer + 1);
                analysis::RecordEvent(analysis::EVENT_HOOK_KEYBOARD,
                                      static_cast<uint8_t>(writer), writer + 1);
            }
        });
    }
    for (auto& writer : writers) writer.join();
    running.store(false, std::memory_order_release);
    reader.join();

    int64_t p50 = 0, p99 = 0, average = 0;
    metric.GetStats(p50, p99, average);
    assert(p50 >= 1 && p99 <= kWriters && average >= 1);
    int64_t copied[telemetry::MetricBuffer::SIZE]{};
    const int copiedCount = metric.CopySamples(copied);
    assert(copiedCount == telemetry::MetricBuffer::SIZE);
    uint32_t cursor = 0;
    bool cursorInitialized = false;
    assert(!metric.AnySince(cursor, cursorInitialized, 100));
    metric.Add(101);
    assert(metric.AnySince(cursor, cursorInitialized, 100));
    assert(analysis::g_traceBuffer.head.load(std::memory_order_relaxed) ==
           static_cast<uint32_t>(kWriters * kSamplesPerWriter));
    std::cout << "test_diagnostic_rings: concurrent snapshots stable\n";
    return 0;
}
