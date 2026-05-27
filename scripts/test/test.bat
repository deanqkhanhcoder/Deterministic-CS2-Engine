@echo off
set OBJS=
for %%f in (obj\release\core\*.o) do (
    if not "%%f"=="obj\release\core\main.o" (
        set OBJS=!OBJS! %%f
    )
)
g++ -std=c++20 -Iinclude -Iinclude/core -Iinclude/ui -DWIN32_LEAN_AND_MEAN test_hybrid.cpp obj/release/core/bhop.o obj/release/core/config_io.o obj/release/core/injection.o obj/release/core/input_capture.o obj/release/core/physics.o obj/release/core/runtime_config.o obj/release/core/state_engine.o obj/release/core/target_platform.o obj/release/core/telemetry.o obj/release/core/timing.o obj/release/core/topology.o obj/release/core/workspace.o -o test_hybrid.exe -luser32 -lwinmm -lgdi32 -lcomdlg32 -lavrt
if %errorlevel% neq 0 exit /b %errorlevel%
test_hybrid.exe
