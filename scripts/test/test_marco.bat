@echo off
echo Starting marco_debug.exe...
start "" /B marco_debug.exe
ping 127.0.0.1 -n 3 > nul
echo Killing marco_debug.exe...
taskkill /F /IM marco_debug.exe >nul 2>&1
echo marco_debug test complete.
