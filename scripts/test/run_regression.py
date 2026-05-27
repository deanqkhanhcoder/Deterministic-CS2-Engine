import os
import subprocess
import sys
import filecmp

def main():
    base_dir = r"c:\Users\toanpq\Desktop\marco"
    simulator_cpp = os.path.join(base_dir, r"tools\forensics\deterministic_simulator.cpp")
    simulator_exe = os.path.join(base_dir, r"runtime\bin\simulator.exe")
    
    # Compile the simulator
    print("Compiling deterministic simulator...")
    mov_obj = os.path.join(base_dir, r"build\obj\release\core\movement_reconstruction.o")
    cfg_obj = os.path.join(base_dir, r"build\obj\release\core\runtime_config.o")
    if not os.path.exists(mov_obj):
        mov_obj = os.path.join(base_dir, r"build\obj\debug\core\movement_reconstruction.o")
        cfg_obj = os.path.join(base_dir, r"build\obj\debug\core\runtime_config.o")
    if not os.path.exists(mov_obj):
        mov_obj = os.path.join(base_dir, r"build\obj\profile\core\movement_reconstruction.o")
        cfg_obj = os.path.join(base_dir, r"build\obj\profile\core\runtime_config.o")
        
    objs = [mov_obj, cfg_obj]
    if "debug" in mov_obj:
        objs.append(os.path.join(base_dir, r"build\obj\debug\core\debug_logger.o"))

    cmd = [
        "g++", "-std=c++20", "-O3",
        "-I" + os.path.join(base_dir, "include"),
        "-I" + os.path.join(base_dir, "include/core"),
        "-I" + os.path.join(base_dir, "include/ui"),
        simulator_cpp
    ] + objs + [
        "-o", simulator_exe
    ]
    
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError:
        print("ERROR: Failed to compile simulator.")
        sys.exit(1)
        
    print("Compilation successful.")
    
    # Run the simulator to generate current output
    current_output = os.path.join(base_dir, r"runtime\artifacts\current_output.csv")
    print(f"Running simulator to generate {current_output}...")
    
    try:
        subprocess.run([simulator_exe, current_output], check=True)
    except subprocess.CalledProcessError:
        print("ERROR: Failed to run simulator.")
        sys.exit(1)
        
    # Compare against golden output
    golden_output = os.path.join(base_dir, r"tests\regression\golden_outputs\physics_v26_baseline.csv")
    
    if not os.path.exists(golden_output):
        print(f"ERROR: Golden output {golden_output} not found!")
        sys.exit(1)
        
    print("Comparing outputs...")
    if filecmp.cmp(golden_output, current_output, shallow=False):
        print("\n[PASS] REGRESSION PASSED: Current physics matches V26 baseline exactly.")
    else:
        print("\n[FAIL] REGRESSION FAILED: Physics delta detected!")
        sys.exit(1)

if __name__ == "__main__":
    main()
