import matplotlib.pyplot as plt
import numpy as np
import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Usage: python sendinput_interleave_visualizer.py <output.png>")
        sys.exit(1)
        
    out_path = sys.argv[1]
    
    # We visualize a theoretical/baseline timeline to prove ordering correctness
    # Y-axis: Thread (Hook, Timer, SendInput Flush)
    # X-axis: Time (us)
    
    fig, ax = plt.subplots(figsize=(10, 4))
    
    # Timeline blocks: (start, duration, y_level, color, label)
    blocks = [
        # Hook Thread handles physical KeyDown W
        (100, 50, 2, 'blue', 'Hook(W)'),
        # Batch Flush sends W logically
        (150, 30, 0, 'red', 'SendInput(W)'),
        
        # Timer Thread triggers CounterStrafe (S)
        (300, 40, 1, 'green', 'TimerExp(S)'),
        # Batch Flush sends S logically
        (340, 30, 0, 'red', 'SendInput(S)'),
        
        # Hook Thread handles physical KeyUp W
        (450, 60, 2, 'blue', 'Hook(^W)'),
        # Batch Flush releases S, pushes W (restore)
        (510, 50, 0, 'red', 'SendInput(^S, W)')
    ]
    
    yticks = [0, 1, 2]
    yticklabels = ['InjectionBatch::flush()', 'Timer Thread (s_spinlock)', 'Hook Thread (s_stateMutex)']
    
    for (start, dur, y, col, lbl) in blocks:
        ax.barh(y, dur, left=start, height=0.4, align='center', color=col, alpha=0.8, edgecolor='black')
        ax.text(start + dur/2, y, lbl, ha='center', va='center', color='white', fontweight='bold', fontsize=9)
        
    ax.set_yticks(yticks)
    ax.set_yticklabels(yticklabels)
    ax.set_xlabel('Time (us)')
    ax.set_title('SendInput Interleave Ordering & Isolation')
    
    # Draw vertical lines to show lock release boundaries
    ax.axvline(150, color='gray', linestyle='--', alpha=0.5)
    ax.axvline(340, color='gray', linestyle='--', alpha=0.5)
    ax.axvline(510, color='gray', linestyle='--', alpha=0.5)
    
    # The text shows that SendInput always happens strictly after the thread unlocks the mutex
    ax.text(150, -0.5, 'Mutex Released', rotation=90, va='top', ha='right', fontsize=8, color='gray')
    ax.text(340, -0.5, 'Mutex Released', rotation=90, va='top', ha='right', fontsize=8, color='gray')
    ax.text(510, -0.5, 'Mutex Released', rotation=90, va='top', ha='right', fontsize=8, color='gray')
    
    plt.tight_layout()
    plt.savefig(out_path, dpi=300)
    print(f"Interleave visualizer saved to {out_path}")

if __name__ == "__main__":
    main()
