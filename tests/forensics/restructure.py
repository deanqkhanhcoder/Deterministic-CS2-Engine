import re
import sys

def main():
    path = "src/core/state_engine.cpp"
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
        
    # 1. Fix batch.flush() ordering
    # We want to change:
    #     PublishEngineState();
    # }
    # batch.flush();
    # To:
    #     batch.flush();
    #     PublishEngineState();
    # }
    #
    # Wait, there might be other stuff after batch.flush(), like _doNotify = true or NotifyUI()
    # We can just match the closing brace and batch.flush()
    
    # We'll use a regex for the batch.flush() relocation
    pattern_flush = re.compile(r"(\s+PublishEngineState\(\);\s*)\}\s*batch\.flush\(\);")
    content = pattern_flush.sub(r"\n        batch.flush();\1}", content)
    
    # Let's fix CancelTimer ABA issue
    # Everywhere timing::CancelTimer(k) is called, if it's for WASD, we should also do s_state.expectedTimerId[ki_k] = 0;
    # But sometimes the variable is `k`, sometimes `oppKey`, sometimes `(Key)i`
    
    # Replace CancelTimer(k); with CancelTimer(k); s_state.expectedTimerId[ki(k)] = 0;
    content = re.sub(r"timing::CancelTimer\(k\);", r"timing::CancelTimer(k);\n        s_state.expectedTimerId[ki(k)] = 0;", content)
    content = re.sub(r"timing::CancelTimer\(oppKey\);", r"timing::CancelTimer(oppKey);\n        s_state.expectedTimerId[ki(oppKey)] = 0;", content)
    
    # 2. Fix AutoFire (OnLButtonDown) and CancelPendingShotLocked
    
    cancel_shot_locked_old = """static void CancelPendingShotLocked(InjectionBatch& batch) {
    if (s_state.autoFire.active) {
        timing::CancelTimer(Key::Mouse1);
        s_state.autoFire.active = false;
        
        // 1. Release injected counter keys
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.injectedCounterMask & (1 << i)) {
                batch.push((Key)i, false);
                s_state.logical[i] = false;
            }
        }
        
        // 2. Restore suspended keys IF STILL PHYSICALLY HELD
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.suspendedMovementMask & (1 << i)) {
                if (s_state.phys[i]) {
                    batch.push((Key)i, true);
                    s_state.logical[i] = true;
                }
            }
        }
        
        // 3. Re-evaluate axis states
        for (int ax = 0; ax < 2; ++ax) {
            Key posK = keymap::AxisPosKey[ax];
            Key negK = keymap::AxisNegKey[ax];
            bool posL = s_state.logical[ki(posK)];
            bool negL = s_state.logical[ki(negK)];
            
            if (posL && negL) {
                s_state.axisState[ax] = AxisState::Conflict;
            } else if (posL) {
                s_state.axisState[ax] = AxisState::Positive;
            } else if (negL) {
                s_state.axisState[ax] = AxisState::Negative;
            } else {
                s_state.axisState[ax] = AxisState::Neutral;
            }
        }
        
        s_state.autoFire.suspendedMovementMask = 0;
        s_state.autoFire.injectedCounterMask = 0;
        
        DLOG_TRACE(Runtime, "AutoFire Cancelled & Movement Restored");
    }
}"""
    
    cancel_shot_locked_new = """static void CancelPendingShotLocked(InjectionBatch& batch) {
    if (s_state.autoFire.active) {
        timing::CancelTimer(Key::Mouse1);
        s_state.autoFire.expectedShotId = 0;
        s_state.autoFire.active = false;
        
        // 1. Release injected counter keys
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.injectedCounterMask & (1 << i)) {
                timing::CancelTimer((Key)i);
                s_state.expectedTimerId[i] = 0;
                if (s_state.logical[i] && !s_state.phys[i]) {
                    batch.push((Key)i, false);
                    s_state.logical[i] = false;
                }
            }
        }
        
        // 2. Restore suspended keys IF STILL PHYSICALLY HELD
        for (int i = 0; i < 4; ++i) {
            if (s_state.autoFire.suspendedMovementMask & (1 << i)) {
                if (s_state.phys[i]) {
                    batch.push((Key)i, true);
                    s_state.logical[i] = true;
                }
            }
        }
        
        // 3. Re-evaluate axis states
        for (int ax = 0; ax < 2; ++ax) {
            Key posK = keymap::AxisPosKey[ax];
            Key negK = keymap::AxisNegKey[ax];
            bool posL = s_state.logical[ki(posK)];
            bool negL = s_state.logical[ki(negK)];
            
            if (posL && negL) {
                s_state.axisState[ax] = AxisState::Conflict;
            } else if (posL) {
                s_state.axisState[ax] = AxisState::Positive;
            } else if (negL) {
                s_state.axisState[ax] = AxisState::Negative;
            } else {
                s_state.axisState[ax] = AxisState::Neutral;
            }
        }
        
        s_state.autoFire.suspendedMovementMask = 0;
        s_state.autoFire.injectedCounterMask = 0;
        
        DLOG_TRACE(Runtime, "AutoFire Cancelled & Movement Restored");
    }
}"""
    
    content = content.replace(cancel_shot_locked_old, cancel_shot_locked_new)
    
    autofire_apply_old = """        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    int64_t brakeUs = InjectAutoFireBrake(key, batch);
                    if (brakeUs > maxBrakeUs) maxBrakeUs = brakeUs;
                }
            }
        };"""
        
    autofire_apply_new = """        auto applyBrake = [&](Axis ax) {
            for (int i = 0; i < 2; ++i) {
                Key key = (ax == Axis::Y) ? (i == 0 ? Key::W : Key::S) : (i == 0 ? Key::A : Key::D);
                int ki_k = ki(key);
                if (s_state.phys[ki_k] && s_state.axisState[ai(ax)] != AxisState::Conflict) {
                    int64_t brakeUs = InjectAutoFireBrake(key, batch);
                    if (brakeUs > 0) {
                        Key counterKey = (ax == Axis::Y) ? (i == 0 ? Key::S : Key::W) : (i == 0 ? Key::D : Key::A);
                        s_state.expectedTimerId[ki(counterKey)] = timing::ScheduleTimerUs(counterKey, brakeUs);
                    }
                    if (brakeUs > maxBrakeUs) maxBrakeUs = brakeUs;
                }
            }
        };"""
        
    content = content.replace(autofire_apply_old, autofire_apply_new)
    
    # 3. Fix RebuildState
    
    rebuild_state_old = """// Unified Focus Reconciliation
void RebuildState() {
    DLOG_INFO(Runtime, "Rebuilding semantic state from physical truth...");
    
    // Sync cached physical states with hardware physical truth (prevents stuck keys via Admin window hook bypass)
    s_state.spacePhys = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    s_state.phys[ki(Key::W)] = (GetAsyncKeyState('W') & 0x8000) != 0;
    s_state.phys[ki(Key::S)] = (GetAsyncKeyState('S') & 0x8000) != 0;
    s_state.phys[ki(Key::A)] = (GetAsyncKeyState('A') & 0x8000) != 0;
    s_state.phys[ki(Key::D)] = (GetAsyncKeyState('D') & 0x8000) != 0;
    
    s_state.walk.shiftDown = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    s_state.sysLCtrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    s_state.sysC = (GetAsyncKeyState('C') & 0x8000) != 0;
    
    // Re-sync Bhop if Space is physically held
    if (s_state.spacePhys) {
        bhop::OnSpaceDown();
    } else {
        bhop::OnSpaceUp();
    }

    // Re-sync WASD
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        for (int i = 0; i < 4; ++i) {
            Key k = static_cast<Key>(i);
            if (s_state.phys[i]) {
                if (!s_state.logical[i]) {
                    if (s_state.walk.shiftDown) s_state.walk.startTimeUs[i] = timing::NowUs();
                    batch.push(k, true);
                    s_state.logical[i] = true;
                }
            } else {
                if (s_state.logical[i]) {
                    timing::CancelTimer(k);
                    if (s_state.logical[i]) {
                        batch.push(k, false);
                        s_state.logical[i] = false;
                    }
                }
            }
        }
        
        ResolveAxis(Axis::X, batch);
        ResolveAxis(Axis::Y, batch);
        batch.flush();
        PublishEngineState();
    }
    NotifyUI();
}"""
    
    rebuild_state_new = """// Unified Focus Reconciliation
void RebuildState() {
    DLOG_INFO(Runtime, "Rebuilding semantic state from physical truth...");
    
    bool snapSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    bool snapW = (GetAsyncKeyState('W') & 0x8000) != 0;
    bool snapS = (GetAsyncKeyState('S') & 0x8000) != 0;
    bool snapA = (GetAsyncKeyState('A') & 0x8000) != 0;
    bool snapD = (GetAsyncKeyState('D') & 0x8000) != 0;
    
    bool snapLShift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    bool snapLCtrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
    bool snapC = (GetAsyncKeyState('C') & 0x8000) != 0;
    
    InjectionBatch batch;
    {
        std::lock_guard<std::mutex> lock(s_stateMutex);
        
        s_state.spacePhys = snapSpace;
        s_state.phys[ki(Key::W)] = snapW;
        s_state.phys[ki(Key::S)] = snapS;
        s_state.phys[ki(Key::A)] = snapA;
        s_state.phys[ki(Key::D)] = snapD;
        
        s_state.walk.shiftDown = snapLShift;
        s_state.sysLCtrl = snapLCtrl;
        s_state.sysC = snapC;
        
        if (s_state.spacePhys) bhop::OnSpaceDown();
        else bhop::OnSpaceUp();
        
        // Re-sync WASD
        for (int i = 0; i < 4; ++i) {
            Key k = static_cast<Key>(i);
            if (s_state.phys[i]) {
                if (!s_state.logical[i]) {
                    if (s_state.walk.shiftDown) s_state.walk.startTimeUs[i] = timing::NowUs();
                    batch.push(k, true);
                    s_state.logical[i] = true;
                }
            } else {
                if (s_state.logical[i]) {
                    timing::CancelTimer(k);
                    s_state.expectedTimerId[i] = 0;
                    batch.push(k, false);
                    s_state.logical[i] = false;
                }
            }
        }
        
        ResolveAxis(Axis::X, batch);
        ResolveAxis(Axis::Y, batch);
        batch.flush();
        PublishEngineState();
    }
    NotifyUI();
}"""
    
    if rebuild_state_old in content:
        content = content.replace(rebuild_state_old, rebuild_state_new)
    else:
        # We might have regex'd it partially, let's just do a manual replace
        print("RebuildState old block not found exactly. Needs regex.")
        # Fallback to regex
        pass

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
        
    print("Done restructuring!")

if __name__ == "__main__":
    main()
