import sys
import re
import os
import matplotlib.pyplot as plt
import numpy as np

def main():
    if len(sys.argv) < 2:
        print("Usage: python fire_stability_replay.py <path_to_marco.log>")
        sys.exit(1)

    log_path = sys.argv[1]
    if not os.path.exists(log_path):
        print(f"File not found: {log_path}")
        sys.exit(1)

    # Regex to match: [FIRE_TRACE] t=123456us tid=123 gen=456 phase=BRAKE_INJECT vx=0 vy=0 logical=W0 A1 S1 D0
    trace_pattern = re.compile(
        r"\[FIRE_TRACE\] t=(\d+)us tid=(\d+) gen=(\d+) phase=(\w+) vx=(-?\d+) vy=(-?\d+) logical=W(\d) A(\d) S(\d) D(\d)"
    )

    events_by_gen = {}

    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            match = trace_pattern.search(line)
            if match:
                t = int(match.group(1))
                tid = int(match.group(2))
                gen = int(match.group(3))
                phase = match.group(4)
                vx = int(match.group(5))
                vy = int(match.group(6))
                logical = f"W{match.group(7)}A{match.group(8)}S{match.group(9)}D{match.group(10)}"

                if gen not in events_by_gen:
                    events_by_gen[gen] = []
                
                events_by_gen[gen].append({
                    "t": t,
                    "tid": tid,
                    "phase": phase,
                    "vx": vx,
                    "vy": vy,
                    "logical": logical
                })

    if not events_by_gen:
        print("No [FIRE_TRACE] events found in the log.")
        sys.exit(0)

    # Analysis
    brake_to_shot_deltas = []
    restore_overlaps = 0
    split_brain_mutations = 0

    for gen, events in events_by_gen.items():
        if gen == 0:
            continue # skip generation 0 (unrelated physical mutations)

        brake_t = None
        shot_sched_t = None
        shot_disp_t = None
        restore_t = None
        
        for e in events:
            phase = e["phase"]
            t = e["t"]
            
            if phase == "BRAKE_INJECT":
                brake_t = t
            elif phase == "SHOT_SCHEDULED":
                shot_sched_t = t
            elif phase == "SHOT_DISPATCH":
                shot_disp_t = t
            elif phase == "RESTORE_PHASE" or phase == "MOVEMENT_RESTORE":
                restore_t = t
            elif phase == "PHYS_MUTATION_DOWN" or phase == "PHYS_MUTATION_UP":
                # If physical mutation happens between scheduled and dispatch without cancel
                if shot_sched_t is not None and shot_disp_t is None and restore_t is None:
                    split_brain_mutations += 1

        if brake_t is not None and shot_disp_t is not None:
            delta = shot_disp_t - brake_t
            brake_to_shot_deltas.append(delta)
        
        if shot_disp_t is not None and restore_t is not None:
            if restore_t < shot_disp_t:
                restore_overlaps += 1

    print(f"--- FIRE STABILITY ANALYSIS ---")
    print(f"Total Fire Generations Analysed: {len(events_by_gen) - 1}")
    
    if brake_to_shot_deltas:
        avg_delta = np.mean(brake_to_shot_deltas)
        std_delta = np.std(brake_to_shot_deltas)
        min_delta = np.min(brake_to_shot_deltas)
        max_delta = np.max(brake_to_shot_deltas)
        print(f"\nBrake -> Shot Delta (Target ~15625us):")
        print(f"  Avg: {avg_delta:.2f} us")
        print(f"  Std: {std_delta:.2f} us")
        print(f"  Min: {min_delta} us")
        print(f"  Max: {max_delta} us")
    else:
        print("\nNo complete Brake -> Shot sequences found.")

    print(f"\nRestore Overlaps (Restore < Shot): {restore_overlaps}")
    print(f"Split-Brain Mutations (Phys during Stabilizing): {split_brain_mutations}")

    # Plot histogram if we have data
    if brake_to_shot_deltas:
        plt.hist(brake_to_shot_deltas, bins=20, color='blue', alpha=0.7)
        plt.axvline(15625, color='red', linestyle='dashed', linewidth=1, label='1 Quantum (15625us)')
        plt.title('Brake -> Shot Dispatch Delta')
        plt.xlabel('Delta (us)')
        plt.ylabel('Count')
        plt.legend()
        plt.grid(True)
        out_img = os.path.join(os.path.dirname(log_path), "fire_stability_hist.png")
        plt.savefig(out_img)
        print(f"\nHistogram saved to: {out_img}")

if __name__ == "__main__":
    main()
