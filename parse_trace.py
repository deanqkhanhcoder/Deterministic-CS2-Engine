import re

def analyze_trace(filepath):
    events = {} # gen -> { vk: ..., isDown: ..., state_before: ..., state_after: ..., toggle_failed: ... }
    
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            
            gen_match = re.search(r'gen=(\d+)', line)
            if not gen_match: continue
            gen = int(gen_match.group(1))
            
            if gen not in events:
                events[gen] = {}
                
            vk_match = re.search(r'vk=(\d+)', line)
            if vk_match:
                events[gen]['vk'] = int(vk_match.group(1))
                
            isdown_match = re.search(r'isDown=(\d+)', line)
            if isdown_match:
                events[gen]['isDown'] = int(isdown_match.group(1))
                
            state_before = re.search(r'PHASE_16_TOGGLE_BOOL_BEFORE state=(\d+)', line)
            if state_before:
                events[gen]['state_before'] = int(state_before.group(1))
                
            state_after = re.search(r'PHASE_17_TOGGLE_BOOL_AFTER state=(\d+)', line)
            if state_after:
                events[gen]['state_after'] = int(state_after.group(1))
                
    # Now check for stuck states:
    stuck_count = 0
    for gen, data in sorted(events.items()):
        if 'isDown' in data and data['isDown'] == 1 and 'state_before' in data:
            if data['state_before'] == 1:
                print(f"STUCK STATE DETECTED! gen={gen} vk={data.get('vk')} isDown=1 but state_before=1!")
                stuck_count += 1
                
    print(f"Total stuck states detected: {stuck_count}")

if __name__ == "__main__":
    analyze_trace('trace_output_utf8.txt')
