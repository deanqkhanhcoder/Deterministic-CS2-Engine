import numpy as np
import matplotlib.pyplot as plt
import os

RC_PHYS_FRICTION = 5.2
RC_PHYS_STOP_SPEED = 80.0
RC_PHYS_MAX_SPEED = 250.0
RC_PHYS_ACCELERATE = 5.5
DT = 15.625 / 1000.0

def simulate_stop(vx, vy, mode, window):
    cur_vx = vx
    cur_vy = vy
    wish_x = -1.0
    wish_y = 0.0
    if mode == 1: wish_y = 1.0
    elif mode == 2: wish_y = -1.0
    elif mode == 3: wish_y = -1.0 if vy > 0 else 1.0
    
    w_mag = np.sqrt(wish_x**2 + wish_y**2)
    if w_mag > 0.001:
        wish_x /= w_mag
        wish_y /= w_mag
        
    ticks = 0
    prev_vx = cur_vx
    
    while ticks < 100:
        if cur_vx <= window:
            break
            
        prev_vx = cur_vx
        speed = np.sqrt(cur_vx**2 + cur_vy**2)
        
        control = RC_PHYS_STOP_SPEED if speed < RC_PHYS_STOP_SPEED else speed
        drop = control * RC_PHYS_FRICTION * DT
        newspeed = max(0, speed - drop)
        f_scale = (newspeed / speed) if speed > 0 else 0
        
        f_vx = cur_vx * f_scale
        f_vy = cur_vy * f_scale
        
        currentspeed = f_vx * wish_x + f_vy * wish_y
        addspeed = RC_PHYS_MAX_SPEED - currentspeed
        
        if addspeed > 0:
            accelspeed = min(addspeed, RC_PHYS_ACCELERATE * DT * RC_PHYS_MAX_SPEED)
            f_vx += accelspeed * wish_x
            f_vy += accelspeed * wish_y
            
        cur_vx = f_vx
        cur_vy = f_vy
        ticks += 1
        
    exact_ticks = float(ticks)
    if ticks > 0 and cur_vx <= window and prev_vx > window:
        fraction = (prev_vx - window) / (prev_vx - cur_vx)
        exact_ticks = (ticks - 1) + fraction
        
    return exact_ticks

def get_aligned_ms(pure_ms):
    return np.ceil(pure_ms / 15.625) * 15.625

def generate_report():
    velocities = np.linspace(0, 250, 251)
    
    # 1. Curve for different windows
    plt.figure(figsize=(10, 6))
    for win in [0.0, 8.0, 16.0, 17.0, 20.0]:
        pure_times = []
        aligned_times = []
        for v in velocities:
            exact = simulate_stop(v, 0, 0, win)
            pure_ms = exact * 15.625
            aligned_ms = get_aligned_ms(pure_ms)
            pure_times.append(pure_ms)
            aligned_times.append(aligned_ms)
            
        plt.plot(velocities, pure_times, label=f'Raw (window={win})')
        plt.plot(velocities, aligned_times, linestyle='--', label=f'Aligned (window={win})')
        
    plt.title('Velocity vs Brake Duration (Subtick Quantization)')
    plt.xlabel('Velocity (u/s)')
    plt.ylabel('Duration (ms)')
    plt.legend()
    plt.grid(True)
    plt.savefig('scratch/braking_curve.png')
    
    # 2. Release-window sensitivity graph for v=250
    windows = np.linspace(0, 30, 300)
    pure_w = []
    aligned_w = []
    for w in windows:
        exact = simulate_stop(250.0, 0, 0, w)
        pure_ms = exact * 15.625
        aligned_ms = get_aligned_ms(pure_ms)
        pure_w.append(pure_ms)
        aligned_w.append(aligned_ms)
        
    plt.figure(figsize=(10, 6))
    plt.plot(windows, pure_w, label='Raw pureDurMs')
    plt.plot(windows, aligned_w, label='Aligned bucketMs', linestyle='--')
    plt.title('Release Window Sensitivity (Velocity = 250)')
    plt.xlabel('Release Velocity Window (u/s)')
    plt.ylabel('Duration (ms)')
    plt.axvline(x=17.0, color='r', linestyle=':', label='Breakthrough (17.0)')
    plt.legend()
    plt.grid(True)
    plt.savefig('scratch/window_sensitivity.png')

if __name__ == '__main__':
    generate_report()
    print("Python simulator finished generating reports.")
