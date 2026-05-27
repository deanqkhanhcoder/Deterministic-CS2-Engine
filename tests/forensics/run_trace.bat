@echo off
echo [1] Cleaning up old logs and processes...
taskkill /F /IM cs2.exe >nul 2>&1
taskkill /F /IM marco_debug.exe >nul 2>&1
if exist marco_debug.log del /F /Q marco_debug.log

echo [2] Starting Marco Debug (Background)...
start "" "runtime\bin\marco_debug.exe"
ping 127.0.0.1 -n 3 > nul

echo [3] Starting Dummy CS2 Window...
start "" "tests\forensics\cs2.exe"
ping 127.0.0.1 -n 2 > nul

echo [4] Simulating Inputs...
python tests\forensics\simulate_inputs.py

echo [5] Cleaning up...
taskkill /F /IM cs2.exe >nul 2>&1
taskkill /F /IM marco_debug.exe >nul 2>&1

echo [6] Running Analysis...
if exist marco_debug.log (
    python tests\forensics\fire_stability_replay.py marco_debug.log
) else (
    echo ERROR: marco_debug.log not found!
)

echo Done!
