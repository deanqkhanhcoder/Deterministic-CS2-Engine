@echo off
echo Testing DEBUG mode...
start "" /B marco_debug.exe
timeout /t 2 /nobreak >nul
taskkill /IM marco_debug.exe /F >nul 2>&1

echo Testing RELEASE mode...
start "" /B marco.exe
timeout /t 2 /nobreak >nul
taskkill /IM marco.exe /F >nul 2>&1

echo All execution tests complete!

