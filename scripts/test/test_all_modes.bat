@echo off
echo Testing DEBUG mode...
start "" /B runtime\bin\marco_debug.exe
timeout /t 2 /nobreak >nul
taskkill /IM marco_debug.exe /F >nul 2>&1

echo Testing PROFILE mode...
start "" /B runtime\bin\marco_profile.exe
timeout /t 2 /nobreak >nul
taskkill /IM marco_profile.exe /F >nul 2>&1

echo Testing RELEASE mode...
start "" /B runtime\bin\marco.exe
timeout /t 2 /nobreak >nul
taskkill /IM marco.exe /F >nul 2>&1

echo All execution tests complete!
