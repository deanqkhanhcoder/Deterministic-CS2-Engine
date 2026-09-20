@echo off
echo Building RELEASE mode...
make release
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b %errorlevel%
)
echo Verifying binary size...
for %%I in (runtime\bin\marco.exe) do (
    set SIZE=%%~zI
    if !SIZE! GTR 3145728 (
        echo WARNING: Release binary %%~nxI is larger than 3MB ^(!SIZE! bytes^). Dead code elimination may have failed.
    ) else (
        echo Binary %%~nxI size is OK ^(!SIZE! bytes^).
    )
)
echo Build succeeded!
