@echo off
echo Building DEBUG mode...
make debug
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)
echo Build succeeded!
