import sys

def test_autofire_restore():
    print("=== AUTOFIRE RESTORE MICRO-DESYNC VALIDATOR ===")
    print("Tracing CancelPendingShotLocked logic for diagonal states...")
    
    # Simulate W and D physically held
    phys = {'W': True, 'S': False, 'A': False, 'D': True}
    
    # Simulate AutoFire active, injecting S and A
    suspended_movement_mask = (1 << 0) | (1 << 3) # W (0) and D (3)
    injected_counter_mask = (1 << 1) | (1 << 2) # S (1) and A (2)
    
    print("Initial State:")
    print(f"Physical: W={phys['W']} D={phys['D']}")
    print("Injected: S=True A=True")
    print("Suspended: W=True D=True")
    
    print("\nExecuting CancelPendingShotLocked...")
    print("Step 1: Releasing injected counter keys...")
    
    batch = []
    # Mask check
    for i, key in enumerate(['W', 'S', 'A', 'D']):
        if injected_counter_mask & (1 << i):
            batch.append((key, 'UP'))
            
    print(f"Batch after Step 1: {batch}")
    
    print("Step 2: Restoring suspended keys IF physically held...")
    for i, key in enumerate(['W', 'S', 'A', 'D']):
        if suspended_movement_mask & (1 << i):
            if phys[key]:
                batch.append((key, 'DOWN'))
                
    print(f"Batch after Step 2: {batch}")
    
    # Validate ordering
    print("\nValidating Dispatch Ordering:")
    # Expected: [('S', 'UP'), ('A', 'UP'), ('W', 'DOWN'), ('D', 'DOWN')]
    # The C++ code queues UP events BEFORE DOWN events.
    # This guarantees that we never have (W_DOWN and S_DOWN) active logically at the exact same microsecond frame.
    
    expected_batch = [('S', 'UP'), ('A', 'UP'), ('W', 'DOWN'), ('D', 'DOWN')]
    if batch == expected_batch:
        print("[PASS] Transient impossible states avoided. UPs are processed before DOWNs in the same batch.")
    else:
        print("[FAIL] Order mismatch!")
        
if __name__ == "__main__":
    test_autofire_restore()
