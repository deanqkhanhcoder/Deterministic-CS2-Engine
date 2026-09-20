@echo off
echo Building PROFILE mode...
make profile
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)
echo Build succeeded!
