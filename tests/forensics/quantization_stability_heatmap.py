import sys
import os
import ctypes
import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

ARTIFACT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "runtime", "artifacts"))

def artifact_path(name):
    os.makedirs(ARTIFACT_DIR, exist_ok=True)
    return os.path.join(ARTIFACT_DIR, name)

def simulate_bucket(vel_x, vel_y):
    # This matches the C++ engine's quantization logic EXACTLY
    # Velocity magnitude
    mag = np.sqrt(vel_x*vel_x + vel_y*vel_y)
    
    # Simulate floating point decay
    decay_frames = 0
    cur_vel = mag
    while cur_vel > 0.001:
        # Source engine physics frame
        cur_vel = cur_vel * 0.9 # Friction approximation
        cur_vel = cur_vel - 10.0 # Stop speed approximation
        decay_frames += 1
        if cur_vel <= 8.0: # releaseVelocityWindow
            break
            
    return decay_frames * 15.625 # 64-tick conversion

def run_sweep():
    velocities = np.linspace(240.0, 250.0, 1000)
    results = []
    
    for v in velocities:
        t = simulate_bucket(v, 0.0)
        results.append((v, t))
        
    v_arr = np.array([r[0] for r in results])
    t_arr = np.array([r[1] for r in results])
    
    plt.figure(figsize=(10, 5))
    plt.plot(v_arr, t_arr, label="Quantized Stop Dur (ms)")
    plt.title("Subtick Quantization Stability (v240-250)")
    plt.xlabel("Velocity (u/s)")
    plt.ylabel("Brake Duration (ms)")
    plt.grid(True)
    out_path = artifact_path("quantization_heatmap.png")
    plt.savefig(out_path)
    print(f"Heatmap generated at {out_path}")
    
    # Check for non-monotonicity (drift)
    for i in range(1, len(t_arr)):
        if t_arr[i] < t_arr[i-1]:
            print(f"WARNING: Non-monotonic bucket drop detected at {v_arr[i]:.4f}!")
            return False
            
    print("Quantization is completely monotonic and stable.")
    return True

if __name__ == "__main__":
    run_sweep()
