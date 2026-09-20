@echo off
echo Running Marco Test Suite...
make check
if %errorlevel% neq 0 exit /b %errorlevel%
echo All tests passed!
