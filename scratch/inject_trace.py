import sys

# 1. Modify counterstrafe_controller.cpp
with open("c:/Users/toanpq/Desktop/marco/src/core/counterstrafe_controller.cpp", "r", encoding="utf-8") as f:
    cs_code = f.read()

# Add LOG_FIRE_TRACE in ResolveAxis
cs_code = cs_code.replace('s_state.generation[ai_a]++;', 's_state.generation[ai_a]++;\n    LOG_FIRE_TRACE("AXIS_TRANSITION_RESOLVE", s_state.autoFire.fireGenerationId);')

# Add LOG_FIRE_TRACE in NeutralizeAxis
cs_code = cs_code.replace('s_state.conflictEnteredTimeMs[ai_a] = timing::NowMs();', 's_state.conflictEnteredTimeMs[ai_a] = timing::NowMs();\n    LOG_FIRE_TRACE("AXIS_TRANSITION_NEUTRALIZE", s_state.autoFire.fireGenerationId);')

with open("c:/Users/toanpq/Desktop/marco/src/core/counterstrafe_controller.cpp", "w", encoding="utf-8") as f:
    f.write(cs_code)


# 2. Modify state_engine.cpp
with open("c:/Users/toanpq/Desktop/marco/src/core/state_engine.cpp", "r", encoding="utf-8") as f:
    se_code = f.read()

validator = """
namespace engine {
void ValidateAxisState() {
    // 1. Logical Array consistency
    bool w = s_state.logical[ki(Key::W)];
    bool s = s_state.logical[ki(Key::S)];
    bool a = s_state.logical[ki(Key::A)];
    bool d = s_state.logical[ki(Key::D)];
    
    auto checkAxis = [&](bool pos, bool neg, AxisState state, const char* axName) {
        if (pos && neg && state != AxisState::Conflict) {
            DLOG_ERR(Runtime, "CORRUPTION: %s axis logical pos+neg but state is %d", axName, (int)state);
        }
        if (pos && !neg && state != AxisState::Positive) {
            DLOG_ERR(Runtime, "CORRUPTION: %s axis logical pos only but state is %d", axName, (int)state);
        }
        if (!pos && neg && state != AxisState::Negative) {
            DLOG_ERR(Runtime, "CORRUPTION: %s axis logical neg only but state is %d", axName, (int)state);
        }
        if (!pos && !neg && state != AxisState::None) {
            DLOG_ERR(Runtime, "CORRUPTION: %s axis logical none but state is %d", axName, (int)state);
        }
    };
    checkAxis(w, s, s_state.axisState[1], "Y");
    checkAxis(d, a, s_state.axisState[0], "X");

    // 2. autoFire mask consistency
    if (s_state.autoFire.state == FireState::Idle) {
        if (s_state.autoFire.injectedCounterMask != 0) {
            DLOG_ERR(Runtime, "CORRUPTION: injectedCounterMask != 0 while Idle");
        }
    }
}
}
"""

if "void ValidateAxisState()" not in se_code:
    se_code += validator

with open("c:/Users/toanpq/Desktop/marco/src/core/state_engine.cpp", "w", encoding="utf-8") as f:
    f.write(se_code)


# 3. Add to engine_internal.h
with open("c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h", "r", encoding="utf-8") as f:
    h_code = f.read()

if "void ValidateAxisState();" not in h_code:
    h_code = h_code.replace("void PublishEngineState();", "void PublishEngineState();\nvoid ValidateAxisState();")

with open("c:/Users/toanpq/Desktop/marco/src/core/engine_internal.h", "w", encoding="utf-8") as f:
    f.write(h_code)

# 4. Add ValidateAxisState() to the end of HandleKeyDown and HandleKeyUp in input_router.cpp
with open("c:/Users/toanpq/Desktop/marco/src/core/input_router.cpp", "r", encoding="utf-8") as f:
    ir_code = f.read()

ir_code = ir_code.replace("PublishEngineState();\n    }\n    batch.flush();", "PublishEngineState();\n        ValidateAxisState();\n    }\n    batch.flush();")

with open("c:/Users/toanpq/Desktop/marco/src/core/input_router.cpp", "w", encoding="utf-8") as f:
    f.write(ir_code)

print("done")
