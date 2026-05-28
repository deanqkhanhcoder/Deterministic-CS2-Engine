import time
import subprocess
import os
import random
import re
import ctypes
import sys

# Constants for inputs
INPUT_MOUSE = 0
INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_SCANCODE = 0x0008

class MOUSEINPUT(ctypes.Structure):
    _fields_ = (("dx", ctypes.c_long), ("dy", ctypes.c_long), ("mouseData", ctypes.c_ulong), ("dwFlags", ctypes.c_ulong), ("time", ctypes.c_ulong), ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)))

class KEYBDINPUT(ctypes.Structure):
    _fields_ = (("wVk", ctypes.c_ushort), ("wScan", ctypes.c_ushort), ("dwFlags", ctypes.c_ulong), ("time", ctypes.c_ulong), ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)))

class HARDWAREINPUT(ctypes.Structure):
    _fields_ = (("uMsg", ctypes.c_ulong), ("wParamL", ctypes.c_ushort), ("wParamH", ctypes.c_ushort))

class _INPUTunion(ctypes.Union):
    _fields_ = (("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT))

class INPUT(ctypes.Structure):
    _fields_ = (("type", ctypes.c_ulong), ("union", _INPUTunion))

def send_key(scancode, release=False):
    x = INPUT(type=INPUT_KEYBOARD, union=_INPUTunion(ki=KEYBDINPUT(wVk=0, wScan=scancode, dwFlags=KEYEVENTF_SCANCODE | (KEYEVENTF_KEYUP if release else 0), time=0, dwExtraInfo=None)))
    ctypes.windll.user32.SendInput(1, ctypes.byref(x), ctypes.sizeof(x))

def send_mouse(down=True):
    x = INPUT(type=INPUT_MOUSE, union=_INPUTunion(mi=MOUSEINPUT(dx=0, dy=0, mouseData=0, dwFlags=0x0002 if down else 0x0004, time=0, dwExtraInfo=None)))
    ctypes.windll.user32.SendInput(1, ctypes.byref(x), ctypes.sizeof(x))

def run_stress_test(duration_minutes=30):
    if os.path.exists('marco_debug.log'):
        os.remove('marco_debug.log')
        
    print(f"Starting marco_debug.exe for {duration_minutes} MINUTE CERTIFICATION STRESS TEST...")
    exe_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../runtime/bin/marco_debug.exe")
    proc = subprocess.Popen([exe_path])
    time.sleep(3) # Wait for startup
    
    start_time = time.time()
    duration_secs = duration_minutes * 60
    keys = [0x11, 0x1E, 0x1F, 0x20] # W, A, S, D
    
    print("Stress test running... Do not touch mouse/keyboard.")
    try:
        iteration = 0
        while time.time() - start_time < duration_secs:
            iteration += 1
            k1 = random.choice(keys)
            k2 = random.choice(keys)
            
            # Rapid diagonal, cancel, M1 spam
            send_key(k1, False)
            send_key(k2, False)
            if random.random() > 0.3: send_mouse(True)
            time.sleep(0.005) # 5ms
            send_key(k1, True)
            if random.random() > 0.5: send_mouse(False)
            time.sleep(0.002)
            send_key(k2, True)
            send_mouse(False)
            time.sleep(0.010)
            
            if iteration % 1000 == 0:
                elapsed = time.time() - start_time
                print(f"[{elapsed:.1f}s] Iterations: {iteration}")
    finally:
        print("Stress test finished. Shutting down...")
        send_mouse(False)
        for k in keys: send_key(k, True)
        proc.terminate()
        proc.wait()

def analyze_logs():
    print("Analyzing logs...")
    stats = {
        'syscall_latencies': [],
        'click_to_shot_latencies': [],
        'd_dead_jitters': [],
        'hook_stalls': 0,
        're_entrancy_blocks': 0,
        'split_brain': 0
    }
    
    try:
        with open('marco_debug.log', 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                if 'RE-ENTRANCY DETECTED' in line:
                    stats['re_entrancy_blocks'] += 1
                elif 'SENDINPUT_SYSCALL_US' in line:
                    m = re.search(r'duration=(\d+)', line)
                    if m: stats['syscall_latencies'].append(int(m.group(1)))
                elif 'PHYSICAL_CLICK_TO_SHOT_US' in line:
                    m = re.search(r'latency=(\d+)', line)
                    if m: stats['click_to_shot_latencies'].append(int(m.group(1)))
                elif 'd_dead=' in line:
                    m = re.search(r'd_dead=(\d+)', line)
                    if m: stats['d_dead_jitters'].append(int(m.group(1)))
                elif 'HOOK_CALLBACK_US' in line:
                    m = re.search(r'duration=(\d+)', line)
                    if m and int(m.group(1)) > 1000:
                        stats['hook_stalls'] += 1
                elif 'FIRESTATE_TRANSITION' in line:
                    old_m = re.search(r'old=(\d+)', line)
                    new_m = re.search(r'new=(\d+)', line)
                    if old_m and new_m and old_m.group(1) == new_m.group(1):
                        stats['split_brain'] += 1
    except FileNotFoundError:
        print("Log file not found.")
        return
        
    print("\n" + "="*50)
    print("FINAL CERTIFICATION REPORT")
    print("="*50)
    
    if stats['syscall_latencies']:
        avg_sys = sum(stats['syscall_latencies']) / len(stats['syscall_latencies'])
        max_sys = max(stats['syscall_latencies'])
        print(f"SendInput Syscall: Avg {avg_sys:.1f}us | Max {max_sys}us")
        
    if stats['click_to_shot_latencies']:
        avg_cts = sum(stats['click_to_shot_latencies']) / len(stats['click_to_shot_latencies'])
        max_cts = max(stats['click_to_shot_latencies'])
        print(f"Physical Click to Shot: Avg {avg_cts:.1f}us | Max {max_cts}us")
        
    if stats['d_dead_jitters']:
        avg_dead = sum(stats['d_dead_jitters']) / len(stats['d_dead_jitters'])
        max_dead = max(stats['d_dead_jitters'])
        print(f"Timer Deadline Jitter (d_dead): Avg {avg_dead:.1f}us | Max {max_dead}us")
        
    print(f"Hook Stalls (>1000us): {stats['hook_stalls']}")
    print(f"Re-entrancy Blocks: {stats['re_entrancy_blocks']}")
    print(f"Split-Brain Transitions: {stats['split_brain']}")
    print("="*50)

if __name__ == '__main__':
    ctypes.windll.winmm.timeBeginPeriod(1)
    try:
        duration = 30
        if len(sys.argv) > 1:
            duration = int(sys.argv[1])
        run_stress_test(duration)
        analyze_logs()
    finally:
        ctypes.windll.winmm.timeEndPeriod(1)
