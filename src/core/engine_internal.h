#pragma once
#include "state.h"
#include "state_engine.h"
#include "injection.h"
#include "runtime_config.h"
#include <mutex>
#include <atomic>

namespace cfg_rt {
    inline int    TAP_SPAM_HALF_LIFE_MS() { return rcfg::Get().tapSpamHalfLifeMs; }
    inline int    TAP_SPAM_WINDOW_MS()    { return rcfg::Get().tapSpamWindowMs; }
    inline double TAP_SPAM_ALPHA()        { return rcfg::Get().tapSpamAlpha; }
    inline double CONFLICT_INCREMENT()    { return rcfg::Get().conflictIncrement; }
    inline double CONFLICT_DECREMENT()    { return rcfg::Get().conflictDecrement; }
    inline int64_t MIN_TAP_US()           { return rcfg::Get().minTapUs; }
    inline int    LATENCY_MARGIN_MS()     { return rcfg::Get().latencyMarginMs; }
    inline int    MIN_STOP_MS()           { return rcfg::Get().minStopMs; }

}

namespace engine {

struct alignas(64) InjectionBatch {
    struct Event { Key k; bool down; };
    Event events[16];
    int count = 0;
    void push(Key k, bool down) { if (count < 16) events[count++] = {k, down}; }
    void flush() {
        for (int i = 0; i < count; ++i) {
            if (events[i].down) injection::KeyDown(events[i].k);
            else injection::KeyUp(events[i].k);
        }
    }
};

extern State s_state;
extern std::mutex s_stateMutex;
extern std::atomic<bool> s_suspendedAtomic;
extern bool s_hookInstalled;

void PublishEngineState();
void NotifyUI();

// Core logical handlers
void ResolveAxis(Axis ax, InjectionBatch& batch);
void NeutralizeAxis(Axis ax, InjectionBatch& batch);
void CancelStaleCounterStrafe(Key oppKey, InjectionBatch& batch);
bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch);
void ApplyOverlapCounterStrafe(Key releaseKey, Key counterKey, int64_t overlapUs, int64_t brakeUs, InjectionBatch& batch);
void ReconcileInternal(bool suspending, InjectionBatch& batch);

int64_t CalculateTrueBrakeUs(Key relKey, Axis ax, int64_t heldUs);

bool OnLButtonDown();
void OnLButtonUp();


} // namespace engine
