import sys

# 1. Update engine_internal.h
with open("c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h", "r", encoding="utf-8") as f:
    code = f.read()

code = code.replace("bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch);", "bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch, int64_t enqueueUs);")

with open("c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h", "w", encoding="utf-8") as f:
    f.write(code)

# 2. Update counterstrafe_controller.cpp
with open("c:/Users/toanpq/Desktop/marco/src/core/counterstrafe_controller.cpp", "r", encoding="utf-8") as f:
    cs = f.read()

cs = cs.replace("bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch) {", "bool AutoCounterStrafe(Key relKey, Key counterKey, Axis ax, int64_t heldUs, InjectionBatch& batch, int64_t enqueueUs) {")
cs = cs.replace("s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerUs(counterKey, effectiveBrakeUs);", "s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerAtUs(counterKey, enqueueUs + effectiveBrakeUs);")

with open("c:/Users/toanpq/Desktop/marco/src/core/counterstrafe_controller.cpp", "w", encoding="utf-8") as f:
    f.write(cs)

# 3. Update input_router.cpp to pass enqueueUs to AutoCounterStrafe
with open("c:/Users/toanpq/Desktop/marco/src/core/input_router.cpp", "r", encoding="utf-8") as f:
    ir = f.read()

ir = ir.replace("AutoCounterStrafe(k, oppK, ax, heldUs, batch);", "AutoCounterStrafe(k, oppK, ax, heldUs, batch, enqueueUs);")

with open("c:/Users/toanpq/Desktop/marco/src/core/input_router.cpp", "w", encoding="utf-8") as f:
    f.write(ir)

print("done")
