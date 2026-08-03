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

def test_axis_release_desync():
    # Simulate W + D fully saturated (250, 250 internally, but magnitude is 250)
    # Wait, in Source, diagonal max speed is 250. So vx = 176.78, vy = 176.78
    vx = 176.78
    vy = 176.78
    
    # AutoFire injects A and S. Both axes need 103ms.
    # But what if X releases at 30ms and Y releases at 103ms?
    # Let's say user only tapped D (so vx = 64.45), but held W (vy = 250).
    vx = 64.45
    vy = 250.0
    
    # Brake X is ~25ms. Brake Y is ~103ms.
    # From 0 to 25ms, both A and S are injected (wish_x = -1, wish_y = -1).
    # From 25ms to 103ms, only S is injected (wish_x = 0, wish_y = -1).
    
    xs = [vx]
    ys = [vy]
    
    for t in range(0, 120, 15):
        if t < 25:
            wish_x, wish_y = -1, -1
        elif t < 103:
            wish_x, wish_y = 0, -1
        else:
            wish_x, wish_y = 0, 0
            
        vx, vy = simulate_frame(vx, vy, wish_x, wish_y)
        xs.append(vx)
        ys.append(vy)
        
    plt.figure(figsize=(8,8))
    plt.plot(xs, ys, marker='o')
    plt.title('Velocity Trajectory (Independent Axis Release)')
    plt.xlabel('X Velocity')
    plt.ylabel('Y Velocity')
    plt.grid(True)
    plt.savefig(artifact_path('axis_desync_trajectory.png'))

def test_quantization_aliasing():
    # Sweep velocities and plot aligned ms
    windows = [8.0, 16.0, 17.0]
    
    plt.figure(figsize=(10,6))
    for win in windows:
        vels = np.linspace(240, 250, 500)
        aligned = []
        for v in vels:
            # simple stop
            vx = v
            vy = 0
            ticks = 0
            while ticks < 20 and vx > win:
                vx, vy = simulate_frame(vx, vy, -1, 0)
                ticks += 1
            pure = ticks * 15.625
            aligned.append(np.ceil(pure / 15.625) * 15.625)
        plt.plot(vels, aligned, label=f'Window={win}')
        
    plt.title('Quantization Aliasing Near Saturation (240-250)')
    plt.xlabel('Initial Velocity')
    plt.ylabel('Bucket (ms)')
    plt.legend()
    plt.savefig(artifact_path('quantization_aliasing.png'))

if __name__ == '__main__':
    test_axis_release_desync()
    test_quantization_aliasing()
    print("Forensic graphs generated.")
