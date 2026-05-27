import ctypes
from ctypes import wintypes
import time
import sys
import threading

# Constants for SendInput
INPUT_KEYBOARD = 1
INPUT_MOUSE = 0
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_SCANCODE = 0x0008

# Scan codes
SCAN_W = 0x11
SCAN_A = 0x1E
SCAN_S = 0x1F
SCAN_D = 0x20

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004

class KEYBDINPUT(ctypes.Structure):
    _fields_ = (("wVk", ctypes.c_ushort),
                ("wScan", ctypes.c_ushort),
                ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)))

class MOUSEINPUT(ctypes.Structure):
    _fields_ = (("dx", ctypes.c_long),
                ("dy", ctypes.c_long),
                ("mouseData", ctypes.c_ulong),
                ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)))

class INPUT_I(ctypes.Union):
    _fields_ = (("ki", KEYBDINPUT),
                ("mi", MOUSEINPUT))

class INPUT(ctypes.Structure):
    _fields_ = (("type", ctypes.c_ulong),
                ("ii", INPUT_I))

def press_key(scan_code):
    extra = ctypes.c_ulong(0)
    ii_ = INPUT_I()
    ii_.ki = KEYBDINPUT(0, scan_code, KEYEVENTF_SCANCODE, 0, ctypes.pointer(extra))
    x = INPUT(INPUT_KEYBOARD, ii_)
    ctypes.windll.user32.SendInput(1, ctypes.pointer(x), ctypes.sizeof(x))

def release_key(scan_code):
    extra = ctypes.c_ulong(0)
    ii_ = INPUT_I()
    ii_.ki = KEYBDINPUT(0, scan_code, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0, ctypes.pointer(extra))
    x = INPUT(INPUT_KEYBOARD, ii_)
    ctypes.windll.user32.SendInput(1, ctypes.pointer(x), ctypes.sizeof(x))

def click_mouse():
    extra = ctypes.c_ulong(0)
    # Down
    ii_down = INPUT_I()
    ii_down.mi = MOUSEINPUT(0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0, ctypes.pointer(extra))
    x_down = INPUT(INPUT_MOUSE, ii_down)
    ctypes.windll.user32.SendInput(1, ctypes.pointer(x_down), ctypes.sizeof(x_down))
    time.sleep(0.05)
    # Up
    ii_up = INPUT_I()
    ii_up.mi = MOUSEINPUT(0, 0, 0, MOUSEEVENTF_LEFTUP, 0, ctypes.pointer(extra))
    x_up = INPUT(INPUT_MOUSE, ii_up)
    ctypes.windll.user32.SendInput(1, ctypes.pointer(x_up), ctypes.sizeof(x_up))

def simulate_case_a():
    print("Simulating Case A: W + Mouse1 (Moving Forward then Shoot)")
    press_key(SCAN_W)
    time.sleep(0.3)
    click_mouse()
    time.sleep(0.2)
    release_key(SCAN_W)
    time.sleep(0.5)

def simulate_case_b():
    print("Simulating Case B: A + D + Mouse1 (Conflict State then Shoot)")
    press_key(SCAN_A)
    time.sleep(0.1)
    press_key(SCAN_D)
    time.sleep(0.2)
    click_mouse()
    time.sleep(0.2)
    release_key(SCAN_A)
    release_key(SCAN_D)
    time.sleep(0.5)

def simulate_case_c():
    print("Simulating Case C: Fast W -> Shot -> Fast W (Jitter/Race)")
    press_key(SCAN_W)
    time.sleep(0.050)
    click_mouse()
    time.sleep(0.01) # Very fast release
    release_key(SCAN_W)
    time.sleep(0.01)
    press_key(SCAN_W)
    time.sleep(0.1)
    release_key(SCAN_W)
    time.sleep(0.5)

if __name__ == "__main__":
    time.sleep(1) # Wait to focus
    simulate_case_a()
    simulate_case_b()
    simulate_case_c()
    print("Simulation complete.")
