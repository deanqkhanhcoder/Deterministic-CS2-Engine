import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
import os

def main():
    if len(sys.argv) != 4:
        print("Usage: python replay_drift_heatmap.py <golden.csv> <current.csv> <output.png>")
        sys.exit(1)
        
    golden_path = sys.argv[1]
    current_path = sys.argv[2]
    out_path = sys.argv[3]
    
    if not os.path.exists(golden_path) or not os.path.exists(current_path):
        print("Missing input files")
        sys.exit(1)

    df_g = pd.read_csv(golden_path)
    df_c = pd.read_csv(current_path)
    
    if len(df_g) != len(df_c):
        print("Warning: CSVs have different lengths")
        min_len = min(len(df_g), len(df_c))
        df_g = df_g.iloc[:min_len]
        df_c = df_c.iloc[:min_len]

    drift_vx = np.abs(df_g['Vx'] - df_c['Vx'])
    drift_vy = np.abs(df_g['Vy'] - df_c['Vy'])
    
    # Calculate magnitude drift
    drift_mag = np.sqrt(drift_vx**2 + drift_vy**2)
    
    plt.figure(figsize=(12, 6))
    
    # We will plot the drift over time (Tick index)
    plt.subplot(1, 2, 1)
    plt.plot(df_g['Test'], drift_vx, label='Vx Drift')
    plt.plot(df_g['Test'], drift_vy, label='Vy Drift', alpha=0.7)
    plt.title('Velocity Drift over Ticks')
    plt.xlabel('Tick')
    plt.ylabel('Absolute Drift (units/s)')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # Create a 2D histogram (Heatmap) of vx vs vy drift
    plt.subplot(1, 2, 2)
    plt.hexbin(df_g['Vx'], df_g['Vy'], C=drift_mag, gridsize=50, cmap='inferno', reduce_C_function=np.max)
    plt.colorbar(label='Max Drift Magnitude')
    plt.title('Drift Heatmap in Phase Space')
    plt.xlabel('Golden Vx')
    plt.ylabel('Golden Vy')
    
    plt.tight_layout()
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    print(f"Heatmap saved to {out_path}")
    
    max_drift = drift_mag.max()
    print(f"Max Drift Detected: {max_drift}")
    
if __name__ == "__main__":
    main()
