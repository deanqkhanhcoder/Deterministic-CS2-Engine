import json

def dump_bursts():
    bursts = []
    current_burst = []
    
    with open('marco_debug.log', 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            if '[FIRE_TRACE]' in line:
                current_burst.append(line.strip())
            
            if 'TIMER_WAKE_ACTUAL' in line or 'TIMER_CANCEL' in line:
                if current_burst:
                    bursts.append(current_burst)
                    current_burst = []
                    
    print(f"Analyzed {len(bursts)} bursts.")
    if len(bursts) > 0:
        print("\n--- BURST 0 ---")
        for line in bursts[0]:
            print(line)
            
    if len(bursts) > 1:
        print("\n--- BURST 1 ---")
        for line in bursts[1]:
            print(line)

if __name__ == '__main__':
    dump_bursts()
