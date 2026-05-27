@echo off
echo Testing DEBUG mode...
start "" /B bin\debug\marco_debug.exe
timeout /t 2 /nobreak >nul
tests\ui_stress.exe 10
taskkill /IM marco_debug.exe /F >nul 2>&1

echo Testing PROFILE mode...
start "" /B bin\profile\marco_profile.exe
timeout /t 2 /nobreak >nul
tests\ui_stress.exe 10
taskkill /IM marco_profile.exe /F >nul 2>&1

echo Testing RELEASE mode...
start "" /B bin\release\marco.exe
timeout /t 2 /nobreak >nul
tests\ui_stress.exe 10
taskkill /IM marco.exe /F >nul 2>&1

echo All tests complete!
