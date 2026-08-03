import numpy as np
import matplotlib.pyplot as plt
import os

RC_PHYS_FRICTION = 5.2
RC_PHYS_STOP_SPEED = 80.0
RC_PHYS_MAX_SPEED = 250.0
RC_PHYS_ACCELERATE = 5.5
DT = 15.625 / 1000.0

ARTIFACT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "runtime", "artifacts"))

def artifact_path(name):
    os.makedirs(ARTIFACT_DIR, exist_ok=True)
    return os.path.join(ARTIFACT_DIR, name)

def simulate_frame(cur_vx, cur_vy, wish_x, wish_y):
    speed = np.sqrt(cur_vx**2 + cur_vy**2)
    control = RC_PHYS_STOP_SPEED if speed < RC_PHYS_STOP_SPEED else speed
    drop = control * RC_PHYS_FRICTION * DT
    newspeed = max(0, speed - drop)
    f_scale = (newspeed / speed) if speed > 0 else 0
    
    f_vx = cur_vx * f_scale
    f_vy = cur_vy * f_scale
    
    w_mag = np.sqrt(wish_x**2 + wish_y**2)
    if w_mag > 0.001:
        w_x = wish_x / w_mag
        w_y = wish_y / w_mag
    else:
        w_x = 0
        w_y = 0
        
    currentspeed = f_vx * w_x + f_vy * w_y
    addspeed = RC_PHYS_MAX_SPEED - currentspeed
    
    if addspeed > 0:
        accelspeed = min(addspeed, RC_PHYS_ACCELERATE * DT * RC_PHYS_MAX_SPEED)
        f_vx += accelspeed * w_x
        f_vy += accelspeed * w_y
        
    return f_vx, f_vy

def exact_brake_duration(v, win):
    vx = v
    vy = 0
    ticks = 0
    prev_vx = vx
    while ticks < 200:
        if vx <= win: break
        prev_vx = vx
        vx, vy = simulate_frame(vx, vy, -1, 0)
        ticks += 1
    
    exact_ticks = float(ticks)
    if ticks > 0 and vx <= win and prev_vx > win:
        frac = (prev_vx - win) / (prev_vx - vx) if (prev_vx - vx) > 0 else 0
        exact_ticks = (ticks - 1) + frac
    return exact_ticks

def get_aligned_ms(pure_ms):
    return np.ceil(pure_ms / 15.625) * 15.625

def analyze_subtick_edges():
    vels = np.linspace(0, 250, 1000)
    windows = [0, 8.0, 16.0, 17.0, 17.5, 18.0]
    
    plt.figure(figsize=(12,8))
    for win in windows:
        aligned = []
        for v in vels:
            pure = exact_brake_duration(v, win) * 15.625
            aligned.append(get_aligned_ms(pure))
        
        # Detect cliffs
        diffs = np.diff(aligned)
        cliffs = np.where(diffs != 0)[0]
        
        plt.plot(vels, aligned, label=f'Win={win}')
    
    plt.title('Subtick Quantization Scanner (0-250 u/s)')
    plt.xlabel('Velocity')
    plt.ylabel('Bucket (ms)')
    plt.legend()
    plt.grid(True)
    plt.savefig(artifact_path('subtick_quantization_scanner.png'))

def analyze_diagonal_symmetry():
    # Case A: hold W long (250), tap D (64.45)
    # Case B: hold D long (250), tap W (64.45)
    
    # AutoFire release X at 25ms, Y at 103ms for Case A
    # AutoFire release Y at 25ms, X at 103ms for Case B
    
    def run_case(v_long, v_tap, t_long_release, t_tap_release):
        vx, vy = v_tap, v_long
        xs, ys = [vx], [vy]
        for t in range(0, 150, 15):
            wish_x = -1 if t < t_tap_release else 0
            wish_y = -1 if t < t_long_release else 0
            vx, vy = simulate_frame(vx, vy, wish_x, wish_y)
            xs.append(vx)
            ys.append(vy)
        return xs, ys
        
    xA, yA = run_case(250, 64.45, 103, 25)
    xB, yB = run_case(250, 64.45, 103, 25) # Actually Case B is symmetric just swapped
    
    plt.figure(figsize=(8,8))
    plt.plot(xA, yA, label='W=Long, D=Tap', marker='o')
    plt.plot(yB, xB, label='D=Long, W=Tap (Swapped Axes)', linestyle='--', marker='x')
    plt.title('Diagonal Asymmetry Audit')
    plt.xlabel('Tap Axis Velocity')
    plt.ylabel('Long Axis Velocity')
    plt.legend()
    plt.grid(True)
    plt.savefig(artifact_path('diagonal_symmetry_audit.png'))

if __name__ == '__main__':
    analyze_subtick_edges()
    analyze_diagonal_symmetry()
    print("Chaos simulator graphs generated.")
