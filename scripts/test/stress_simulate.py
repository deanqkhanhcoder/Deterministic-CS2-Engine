import ctypes
import time
import subprocess
import os
import random

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

def main():
    if os.path.exists('marco_debug.log'):
        os.remove('marco_debug.log')
        
    print("Starting marco_debug.exe for STRESS TEST...")
    proc = subprocess.Popen(["runtime/bin/marco_debug.exe"])
    time.sleep(2)
    
    print("Starting 10,000 stress iterations...")
    keys = [0x11, 0x1E, 0x1F, 0x20]
    
    start_time = time.time()
    for i in range(10000):
        if i % 1000 == 0:
            print(f"Iteration {i}/10000...")
        k1 = random.choice(keys)
        k2 = random.choice(keys)
        send_key(k1, False)
        if random.random() > 0.5: send_mouse(True)
        send_key(k2, False)
        if random.random() > 0.5: send_mouse(False)
        else: send_mouse(True)
        send_key(k1, True)
        send_key(k2, True)
        if random.random() > 0.5: send_mouse(False)
        time.sleep(0.001)

    send_mouse(False)
    for k in keys: send_key(k, True)
    print(f"Stress test finished in {time.time() - start_time:.2f} seconds.")
    time.sleep(1)
    print("Stopping marco...")
    proc.terminate()
    proc.wait()
    print("Done")

if __name__ == "__main__":
    main()
