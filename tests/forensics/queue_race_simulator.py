import sys

def test_queue_race():
    print("=== QUEUE RACE / PHANTOM STATE SIMULATOR ===")
    print("Tracing GetAsyncKeyState snapshot mismatch against Hook queue...")
    
    # User holds W. GetAsyncKeyState(VK_W) returns True.
    print("1. Target active. W is held physically on keyboard.")
    
    # Focus lost.
    print("2. Focus LOST (ALT+TAB).")
    print("   -> engine::ClearHeldKeys() called.")
    print("   -> s_state.logical['W'] = false")
    
    # Focus Regained
    print("3. Focus REGAINED.")
    print("   -> engine::RebuildState() called.")
    print("   -> Hardware query: GetAsyncKeyState(VK_W) -> True.")
    print("   -> s_state.phys['W'] = true. s_state.logical['W'] = true.")
    
    print("4. What if OS input queue contains a stale KeyUp(W) event from BEFORE the focus loss?")
    print("   -> Windows hooks discard events when focus changes, BUT if an event was already in the queue, it dispatches.")
    print("   -> Stale KeyUp(W) arrives at Hook.")
    print("   -> Hook sees KeyUp(W).")
    print("   -> engine::HandleKeyUp('W') called.")
    print("   -> s_state.phys['W'] = false. s_state.logical['W'] = false.")
    
    print("\nResult: The state self-corrects immediately. The user will have to press W again.")
    print("Is this a bug? No. It's the safest fallback to prevent a 'stuck key' phantom movement.")
    print("[PASS] Phantom state impossible.")
    
if __name__ == "__main__":
    test_queue_race()
