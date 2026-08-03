#include "../../src/core/engine_internal.h"
#include "runtime_config.h"
#include "timing.h"

#include <array>
#include <atomic>
#include <mutex>

namespace {
std::atomic<int64_t> g_nowUs{0};
std::atomic<uint64_t> g_nextTimerId{1};
std::array<std::atomic<uint64_t>, 5> g_timers{};
std::array<Key, 8> g_scheduleTrace{};
size_t g_scheduleTraceCount = 0;
// -1 accepts indefinitely; 0 rejects; positive values count admissions left.
std::atomic<int> g_timerSchedulesBeforeFailure{-1};
target_platform::TargetIdentity g_liveTarget{};
RuntimeConfig g_config{};
}

namespace diagonal_test {
void SetLiveTarget(const target_platform::TargetIdentity& target) {
    g_liveTarget = target;
}
target_platform::TargetIdentity GetLiveTarget() {
    return g_liveTarget;
}
void AdvanceUs(int64_t deltaUs) {
    g_nowUs.fetch_add(deltaUs, std::memory_order_relaxed);
}
uint64_t TakeTimer(Key key) {
    return g_timers[ki(key)].exchange(0, std::memory_order_acq_rel);
}
void SetTimerSchedulesBeforeFailure(int admissions) {
    g_timerSchedulesBeforeFailure.store(admissions, std::memory_order_release);
}
void ResetScheduleTrace() {
    g_scheduleTraceCount = 0;
}
size_t ScheduleTraceCount() {
    return g_scheduleTraceCount;
}
Key ScheduledKey(size_t index) {
    return g_scheduleTrace[index];
}
}

namespace target_platform {
bool IsExpectedTargetActive(const TargetIdentity& expected) noexcept {
    return expected.IsValid() && expected == g_liveTarget;
}
}

namespace rcfg {
RuntimeConfig Get() { return g_config; }
RuntimeConfig Sanitize(const RuntimeConfig& config) { return config; }
void Apply(const RuntimeConfig& config) { g_config = config; }
RuntimeConfig GetMutable() { return g_config; }
void Init() { g_config = RuntimeConfig{}; }
}

namespace timing {
void Init() { g_nowUs.store(0, std::memory_order_release); }
int64_t NowUs() { return g_nowUs.load(std::memory_order_acquire); }
int64_t NowMs() { return NowUs() / 1000; }
void StartTimerThread() {}
void StopTimerThread() {
    for (auto& timer : g_timers) timer.store(0, std::memory_order_release);
}
uint64_t ScheduleTimerUs(Key key, int64_t) {
    int remaining = g_timerSchedulesBeforeFailure.load(std::memory_order_acquire);
    while (remaining >= 0) {
        if (remaining == 0) return 0;
        if (g_timerSchedulesBeforeFailure.compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_acq_rel)) {
            break;
        }
    }
    const uint64_t id = g_nextTimerId.fetch_add(1, std::memory_order_relaxed);
    if (g_scheduleTraceCount < g_scheduleTrace.size()) {
        g_scheduleTrace[g_scheduleTraceCount++] = key;
    }
    g_timers[ki(key)].store(id, std::memory_order_release);
    return id;
}
uint64_t ScheduleTimer(Key key, int durationMs) {
    return ScheduleTimerUs(key, static_cast<int64_t>(durationMs) * 1000);
}
void CancelTimer(Key key) {
    g_timers[ki(key)].store(0, std::memory_order_release);
}
bool AreTimersActive() {
    for (const auto& timer : g_timers) {
        if (timer.load(std::memory_order_acquire) != 0) return true;
    }
    return false;
}
}

namespace engine {
State s_state;
std::mutex s_stateMutex;
std::mutex s_operationMutex;
std::atomic<bool> s_suspendedAtomic{false};
bool s_hookInstalled = true;
void PublishEngineState() {}
void NotifyUI() {}
State GetState() {
    std::lock_guard<std::mutex> lock(s_stateMutex);
    return s_state;
}
}
