import ctypes
import time
import random
import sys

# SendInput structures
PUL = ctypes.POINTER(ctypes.c_ulong)
class KeyBdInput(ctypes.Structure):
    _fields_ = [("wVk", ctypes.c_ushort),
                ("wScan", ctypes.c_ushort),
                ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong),
                ("dwExtraInfo", PUL)]

class HardwareInput(ctypes.Structure):
    _fields_ = [("uMsg", ctypes.c_ulong),
                ("wParamL", ctypes.c_short),
                ("wParamH", ctypes.c_ushort)]

class MouseInput(ctypes.Structure):
    _fields_ = [("dx", ctypes.c_long),
                ("dy", ctypes.c_long),
                ("mouseData", ctypes.c_ulong),
                ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong),
                ("dwExtraInfo", PUL)]

class Input_I(ctypes.Union):
    _fields_ = [("ki", KeyBdInput),
                 ("mi", MouseInput),
                 ("hi", HardwareInput)]

class Input(ctypes.Structure):
    _fields_ = [("type", ctypes.c_ulong),
                ("ii", Input_I)]

# Constants
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_SCANCODE = 0x0008
INPUT_KEYBOARD = 1
INPUT_MOUSE = 0

def press_key(hexKeyCode):
    extra = ctypes.c_ulong(0)
    ii_ = Input_I()
    ii_.ki = KeyBdInput(hexKeyCode, 0, 0, 0, ctypes.pointer(extra))
    x = Input(ctypes.c_ulong(1), ii_)
    ctypes.windll.user32.SendInput(1, ctypes.pointer(x), ctypes.sizeof(x))

def release_key(hexKeyCode):
    extra = ctypes.c_ulong(0)
    ii_ = Input_I()
    ii_.ki = KeyBdInput(hexKeyCode, 0, KEYEVENTF_KEYUP, 0, ctypes.pointer(extra))
    x = Input(ctypes.c_ulong(1), ii_)
    ctypes.windll.user32.SendInput(1, ctypes.pointer(x), ctypes.sizeof(x))

VK_W = 0x57
VK_A = 0x41
VK_S = 0x53
VK_D = 0x44
VK_SPACE = 0x20
VK_MENU = 0x12 # ALT
VK_TAB = 0x09
VK_SHIFT = 0x10

F_KEYS = [0x70, 0x71, 0x72, 0x75] # F1, F2, F3, F6

def main():
    duration = 10
    if len(sys.argv) > 1:
        duration = int(sys.argv[1])
        
    print(f"Starting forensic stress test for {duration} seconds...")
    time.sleep(2)
    
    start_time = time.time()
    
    movement_keys = [VK_W, VK_A, VK_S, VK_D, VK_SPACE, VK_SHIFT]
    active_keys = set()
    
    f_key_count = 0
    
    while time.time() - start_time < duration:
        # Randomly toggle movement keys rapidly
        k = random.choice(movement_keys)
        if k in active_keys:
            release_key(k)
            active_keys.remove(k)
        else:
            press_key(k)
            active_keys.add(k)
            
        # Randomly press an F-key
        if random.random() < 0.05: # 5% chance per loop iteration
            fk = random.choice(F_KEYS)
            press_key(fk)
            time.sleep(0.01)
            release_key(fk)
            f_key_count += 1
            print(f"[{time.time() - start_time:.2f}] Injected F-key: {fk}")
            
        # Randomly alt-tab
        if random.random() < 0.01:
            press_key(VK_MENU)
            press_key(VK_TAB)
            time.sleep(0.05)
            release_key(VK_TAB)
            release_key(VK_MENU)
            print(f"[{time.time() - start_time:.2f}] Alt-Tabbed")
            
        time.sleep(random.uniform(0.001, 0.01))
        
    for k in active_keys:
        release_key(k)
        
    print(f"Done. Injected {f_key_count} F-key events.")

if __name__ == "__main__":
    main()
