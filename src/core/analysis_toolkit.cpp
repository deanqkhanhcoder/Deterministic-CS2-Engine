#include "analysis_toolkit.h"
#include "timing.h"
#include <algorithm>
#include <fstream>
#include <vector>
#include <cmath>
#include <cwchar>

namespace analysis {

TraceBuffer g_traceBuffer;
static uint64_t s_sessionStartTimeUs = 0;

void Init() {
    g_traceBuffer.head.store(0, std::memory_order_relaxed);
    s_sessionStartTimeUs = timing::NowUs();
}

void RecordEvent(EventType type, uint8_t core, int32_t value) {
    uint32_t idx = g_traceBuffer.head.fetch_add(1, std::memory_order_relaxed) % TRACE_BUFFER_SIZE;
    TraceEvent& ev = g_traceBuffer.events[idx];
    ev.timestampUs = timing::NowUs();
    ev.eventType = static_cast<uint8_t>(type);
    ev.coreIndex = core;
    ev.value = value;
}

static void CalculatePercentiles(std::vector<int32_t>& samples, LatencyStats& stats) {
    if (samples.empty()) {
        stats = LatencyStats{};
        return;
    }
    std::sort(samples.begin(), samples.end());
    stats.p50 = samples[samples.size() / 2];
    stats.p95 = samples[(samples.size() * 95) / 100];
    stats.p99 = samples[(samples.size() * 99) / 100];
    stats.p999 = samples[(samples.size() * 999) / 1000];
    stats.maxVal = samples.back();
}

void AnalyzeSession(LatencyStats& hook, LatencyStats& jitter, LatencyStats& oversleep,
                    SpikeCluster& spikes, CoreStats coreStats[64], uint32_t& activeCoresCount,
                    uint32_t& totalMigrations, uint32_t& smtCollisions) {
    uint32_t count = g_traceBuffer.head.load(std::memory_order_relaxed);
    uint32_t limit = count < TRACE_BUFFER_SIZE ? count : TRACE_BUFFER_SIZE;

    std::vector<int32_t> hookSamples;
    std::vector<int32_t> jitterSamples;
    std::vector<int32_t> oversleepSamples;
    std::vector<uint64_t> spikeTimestamps;

    for (int i = 0; i < 64; ++i) coreStats[i] = CoreStats{};
    totalMigrations = 0;
    smtCollisions = 0;
    bool activeCoresFlags[64] = {false};

    uint64_t lastSpikeUs = 0;
    uint32_t burstSpikeCount = 0;

    for (uint32_t i = 0; i < limit; ++i) {
        const TraceEvent& ev = g_traceBuffer.events[i];
        if (ev.coreIndex < 64) {
            activeCoresFlags[ev.coreIndex] = true;
            coreStats[ev.coreIndex].residencySamples++;
        }

        switch (ev.eventType) {
            case EVENT_HOOK_KEYBOARD:
            case EVENT_HOOK_MOUSE:
                hookSamples.push_back(ev.value);
                break;
            case EVENT_TIMER_JITTER:
                jitterSamples.push_back(ev.value);
                if (ev.coreIndex < 64 && ev.value > coreStats[ev.coreIndex].maxJitter) {
                    coreStats[ev.coreIndex].maxJitter = ev.value;
                }
                break;
            case EVENT_TIMER_OVERSLEEP:
                oversleepSamples.push_back(ev.value);
                break;
            case EVENT_CORE_MIGRATION:
                totalMigrations++;
                if (ev.coreIndex < 64) coreStats[ev.coreIndex].migrations++;
                break;
            case EVENT_SPIKE:
                spikeTimestamps.push_back(ev.timestampUs);
                if (lastSpikeUs > 0 && (ev.timestampUs - lastSpikeUs < 100000)) { // <100ms
                    burstSpikeCount++;
                }
                lastSpikeUs = ev.timestampUs;
                break;
        }
    }

    CalculatePercentiles(hookSamples, hook);
    CalculatePercentiles(jitterSamples, jitter);
    CalculatePercentiles(oversleepSamples, oversleep);

    activeCoresCount = 0;
    for (int i = 0; i < 64; ++i) {
        if (activeCoresFlags[i]) activeCoresCount++;
    }

    spikes.totalSpikes = (uint32_t)spikeTimestamps.size();
    spikes.burstCount = burstSpikeCount;
    uint64_t elapsedUs = timing::NowUs() - s_sessionStartTimeUs;
    double elapsedSec = elapsedUs / 1000000.0;
    spikes.frequencyHz = elapsedSec > 0.1 ? (spikes.totalSpikes / elapsedSec) : 0.0;
    spikes.sustainedDegradation = (spikes.totalSpikes > 5 && spikes.frequencyHz > 0.5);
}

void ExportCSV(const wchar_t* filePath) {
    std::ofstream out(filePath);
    if (!out.is_open()) return;
    out << "TimestampUs,EventType,Core,Value\n";
    uint32_t count = g_traceBuffer.head.load(std::memory_order_relaxed);
    uint32_t limit = count < TRACE_BUFFER_SIZE ? count : TRACE_BUFFER_SIZE;
    for (uint32_t i = 0; i < limit; ++i) {
        const TraceEvent& ev = g_traceBuffer.events[i];
        out << ev.timestampUs << "," << (int)ev.eventType << "," << (int)ev.coreIndex << "," << ev.value << "\n";
    }
}

void ExportJSON(const wchar_t* filePath) {
    std::ofstream out(filePath);
    if (!out.is_open()) return;
    out << "[\n";
    uint32_t count = g_traceBuffer.head.load(std::memory_order_relaxed);
    uint32_t limit = count < TRACE_BUFFER_SIZE ? count : TRACE_BUFFER_SIZE;
    for (uint32_t i = 0; i < limit; ++i) {
        const TraceEvent& ev = g_traceBuffer.events[i];
        out << "  {\"timestampUs\": " << ev.timestampUs 
            << ", \"type\": " << (int)ev.eventType 
            << ", \"core\": " << (int)ev.coreIndex 
            << ", \"value\": " << ev.value << "}";
        if (i < limit - 1) out << ",\n";
    }
    out << "\n]\n";
}

void SaveBaseline(const wchar_t* filePath) {
    LatencyStats hook, jitter, oversleep;
    SpikeCluster spikes;
    CoreStats cores[64];
    uint32_t activeCores = 0, migrations = 0, smtCollisions = 0;
    AnalyzeSession(hook, jitter, oversleep, spikes, cores, activeCores, migrations, smtCollisions);

    std::ofstream out(filePath);
    if (!out.is_open()) return;
    out << "{\n"
        << "  \"hook_p99\": " << hook.p99 << ",\n"
        << "  \"jitter_p99\": " << jitter.p99 << ",\n"
        << "  \"oversleep_p99\": " << oversleep.p99 << "\n"
        << "}\n";
}

bool CompareSession(const wchar_t* baselinePath, wchar_t* outResult, size_t maxLen) {
    std::ifstream in(baselinePath);
    if (!in.is_open()) {
        swprintf_s(outResult, maxLen, L"No baseline profile found.");
        return false;
    }
    int32_t baseHook = 0, baseJitter = 0, baseOversleep = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("hook_p99") != std::string::npos) {
            sscanf_s(line.c_str(), "  \"hook_p99\": %d", &baseHook);
        } else if (line.find("jitter_p99") != std::string::npos) {
            sscanf_s(line.c_str(), "  \"jitter_p99\": %d", &baseJitter);
        } else if (line.find("oversleep_p99") != std::string::npos) {
            sscanf_s(line.c_str(), "  \"oversleep_p99\": %d", &baseOversleep);
        }
    }

    LatencyStats hook, jitter, oversleep;
    SpikeCluster spikes;
    CoreStats cores[64];
    uint32_t activeCores = 0, migrations = 0, smtCollisions = 0;
    AnalyzeSession(hook, jitter, oversleep, spikes, cores, activeCores, migrations, smtCollisions);

    bool regression = false;
    if (baseHook > 0 && hook.p99 > (baseHook * 115) / 100) regression = true;
    if (baseJitter > 0 && jitter.p99 > (baseJitter * 115) / 100) regression = true;
    if (baseOversleep > 0 && oversleep.p99 > (baseOversleep * 115) / 100) regression = true;

    swprintf_s(outResult, maxLen, L"Base p99 Jitter: %d.%d us | Current: %d.%d us -> %ls",
               baseJitter / 10, baseJitter % 10,
               jitter.p99 / 10, jitter.p99 % 10,
               regression ? L"REGRESSION" : L"OK");
    return regression;
}

} // namespace analysis
