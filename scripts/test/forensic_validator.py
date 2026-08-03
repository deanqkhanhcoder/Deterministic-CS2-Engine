import re
import sys

def parse_log(filepath):
    events = []
    trace_pattern = re.compile(r'\[FIRE_TRACE\]\s+(.*)')
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            m = trace_pattern.search(line)
            if m:
                events.append(m.group(1))
    return events

def validate_events(events):
    errors = []
    in_flush = False
    gen_state = {}
    
    for ev in events:
        if "RE-ENTRANCY DETECTED" in ev:
            errors.append(f"Hook Re-entrancy detected: {ev}")
            
        elif "CALLBACK_EXECUTION_US" in ev:
            dur_m = re.search(r'duration=(\d+)', ev)
            if dur_m and int(dur_m.group(1)) > 1000:
                errors.append(f"Callback Execution too long: {dur_m.group(1)}us")
                    
        elif "FLUSH_EXECUTION_US" in ev:
            dur_m = re.search(r'duration=(\d+)', ev)
            if dur_m and int(dur_m.group(1)) > 2000:
                errors.append(f"Flush Execution severely blocked: {dur_m.group(1)}us")
                        
        elif "FIRESTATE_TRANSITION" in ev:
            old_m = re.search(r'old=(\d+)', ev)
            new_m = re.search(r'new=(\d+)', ev)
            gen_m = re.search(r'gen=(\d+)', ev)
            if old_m and new_m and gen_m:
                old_s = int(old_m.group(1))
                new_s = int(new_m.group(1))
                if old_s == new_s:
                    errors.append(f"Split-Brain Transition (old == new): {ev}")
                if old_s == 2 and new_s == 1:
                    errors.append(f"Illegal Transition Fired -> Stabilizing: {ev}")
                    
        elif "phase=" in ev:
            parts = ev.split()
            data = {}
            for p in parts:
                if '=' in p:
                    k, v = p.split('=', 1)
                    data[k] = v
            
            phase = data.get('phase', '')
            gen = data.get('gen', '-1')
            d_dead = int(data.get('d_dead', '0'))
            
            if gen not in gen_state:
                gen_state[gen] = {'dispatched': False, 'restored': False}
                
            if phase == 'BATCH_FLUSH_BEGIN':
                in_flush = True
            elif phase == 'BATCH_FLUSH_COMPLETE':
                in_flush = False
            elif phase == 'SHOT_DISPATCH':
                if d_dead > 500:
                    errors.append(f"Missed deadline (d_dead > 500us): {d_dead}us in gen {gen}")
                gen_state[gen]['dispatched'] = True
                if gen_state[gen]['restored']:
                    errors.append(f"Race Condition: Restore happened before Shot Dispatch in gen {gen}")
            elif phase == 'RESTORE_PHASE':
                gen_state[gen]['restored'] = True
                
    return errors

def main():
    if len(sys.argv) < 2:
        print("Usage: python forensic_validator.py <logfile>")
        sys.exit(1)
        
    logfile = sys.argv[1]
    events = parse_log(logfile)
    print(f"Parsed {len(events)} [FIRE_TRACE] events.")
    
    errors = validate_events(events)
    if errors:
        print("\nFAILED FORENSIC AUDIT FAILED! Found the following hidden instabilities:")
        for e in errors[:50]:
            print(f"  - {e}")
        if len(errors) > 50:
            print(f"  ... and {len(errors) - 50} more errors.")
        sys.exit(1)
    else:
        print("\nPASSED FORENSIC AUDIT PASSED! No instabilities detected.")
        sys.exit(0)

if __name__ == "__main__":
    main()
