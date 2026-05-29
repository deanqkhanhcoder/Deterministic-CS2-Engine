import sys

with open("c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h", "r", encoding="utf-8") as f:
    h = f.read()

h = h.replace("void ApplyOverlapCounterStrafe(Key releaseKey, Key counterKey, int64_t overlapUs, int64_t brakeUs, InjectionBatch& batch);", "void ApplyOverlapCounterStrafe(Key releaseKey, Key counterKey, int64_t overlapUs, int64_t brakeUs, InjectionBatch& batch, int64_t enqueueUs);")

with open("c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h", "w", encoding="utf-8") as f:
    f.write(h)


with open("c:/Users/toanpq/Desktop/marco/src/core/counterstrafe_controller.cpp", "r", encoding="utf-8") as f:
    cs = f.read()

cs = cs.replace("void ApplyOverlapCounterStrafe(Key releaseKey, Key counterKey, int64_t overlapUs, int64_t brakeUs, InjectionBatch& batch) {", "void ApplyOverlapCounterStrafe(Key releaseKey, Key counterKey, int64_t overlapUs, int64_t brakeUs, InjectionBatch& batch, int64_t enqueueUs) {")
cs = cs.replace("s_state.expectedTimerId[ki_r] = timing::ScheduleTimerUs(releaseKey, overlapUs);", "s_state.expectedTimerId[ki_r] = timing::ScheduleTimerAtUs(releaseKey, enqueueUs + overlapUs);")

cs = cs.replace("ApplyOverlapCounterStrafe(relKey, counterKey, effectiveOverlapUs, effectiveBrakeUs, batch);", "ApplyOverlapCounterStrafe(relKey, counterKey, effectiveOverlapUs, effectiveBrakeUs, batch, enqueueUs);")

with open("c:/Users/toanpq/Desktop/marco/src/core/counterstrafe_controller.cpp", "w", encoding="utf-8") as f:
    f.write(cs)

print("done")
