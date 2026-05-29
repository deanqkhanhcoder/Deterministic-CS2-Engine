// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Main Entry Point                                 ║
// ║  Full native desktop application lifecycle                          ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <thread>

#include "build_config.h"
#include "config.h"
#include "types.h"
#include "timing.h"
#include "movement_reconstruction.h"
#include "state_engine.h"
#include "input_capture.h"
#include "bhop.h"
#include "ui_main.h"
#include "ui_diagnostics.h"
#include "runtime_config.h"
#include "config_io.h"
#include "debug_logger.h"
#include "target_platform.h"
#include "workspace.h"
#include "timing.h"
#include "telemetry.h"
#include "analysis_toolkit.h"
#include "topology.h"
#include "etw_controller.h"

// ── Crash Resilience & Startup Forensics ──
static const char* g_startupPhase = "INIT";
#define SAFE_STARTUP_TRACE(phase) do { g_startupPhase = phase; DLOG_INFO(Startup, "Startup Phase: %s", reinterpret_cast<int64_t>(phase)); } while(0)

static LONG WINAPI CrashVectoredExceptionHandler(EXCEPTION_POINTERS* ep) {
    // Only handle severe crashes, ignore debugger events like STATUS_BREAKPOINT
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || 
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW) {
        
        // Unhook instantly to prevent system-wide stuck keys
        capture::Uninstall();
        timeEndPeriod(1);

        workspace::EnsureLogDirectoryExists();
        std::string crashLog = workspace::GetLogRootA() + "startup_crash.log";
        FILE* f = fopen(crashLog.c_str(), "w");
        if (f) {
            fprintf(f, "CRITICAL FAULT: 0x%08lX\n", code);
            fprintf(f, "Last Startup Phase: %s\n", g_startupPhase);
            fclose(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}


// ── Globals ──
static UINT_PTR g_watchdogTimerId = 0;

// ── Watchdog timer callback ──
static void CALLBACK WatchdogTimerProc(HWND, UINT, UINT_PTR, DWORD) {
#if MARCO_ENABLE_HEARTBEATS
    telemetry::g_heartbeatHook.store(timing::NowMs(), std::memory_order_relaxed);
#endif
#if MARCO_ENABLE_WATCHDOG
    engine::RunWatchdog();
#endif
    capture::PollTarget();
}

// ── Main window procedure for the hidden message window ──
// This receives timer-thread messages and hotkey commands.
// The UI window (ui_main) handles its own WndProc separately.
static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#if MARCO_ENABLE_HEARTBEATS
    telemetry::g_heartbeatHook.store(timing::NowMs(), std::memory_order_relaxed);
#endif
    switch (msg) {
        case WM_TOGGLE_SUSPEND: {
            engine::ToggleSuspend();
            if (engine::IsSuspended()) bhop::OnSpaceUp();
            bhop::OnSuspendChanged();
            ui::OnStateChanged();
            DLOG_WARN(Runtime, "Suspend: %s", reinterpret_cast<int64_t>(engine::IsSuspended() ? "ON" : "OFF"));
            return 0;
        }

        case WM_BHOP_TOGGLE: {
            bhop::ToggleEnabled();
            ui::OnStateChanged();
            return 0;
        }

        case WM_BHOP_CYCLE_MODE: {
            bhop::CycleMode();
            ui::OnStateChanged();
            return 0;
        }

        case WM_CYCLE_PROFILE: {
            RuntimeConfig& cfg = rcfg::GetMutable();
            cfg.activeBrakeProfileIndex = (cfg.activeBrakeProfileIndex % 4) + 1;
            rcfg::Apply(cfg);
            config_io::Save(cfg);
            ui::OnStateChanged();
            return 0;
        }

        case WM_STATE_DIRTY: {
            engine::ClearStateDirty();
            ui::OnStateChanged();
            return 0;
        }

        case WM_TARGET_REFRESH_REQUEST: {
            HWND targetHwnd = reinterpret_cast<HWND>(wParam);
            target_platform::ResolveTargetAsync(target_platform::TargetIdentity::FromWindow(targetHwnd));
            ui::OnStateChanged();
            return 0;
        }

        case WM_EMERGENCY_UNHOOK: {
            capture::Uninstall();
            engine::SetHookInstalled(false);
            ui::OnStateChanged();
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ════════════════════════════════════════════════════════════════
//  ENTRY POINT
// ════════════════════════════════════════════════════════════════

typedef LONG (WINAPI *NTSETTIMERRESOLUTION)(ULONG, BOOLEAN, PULONG);

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // ── Crash handler ──
    AddVectoredExceptionHandler(1, CrashVectoredExceptionHandler);
    SAFE_STARTUP_TRACE("MUTEX_CHECK");

    // ── Prevent multiple instances ──
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"CS2MacroSuite_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"CS2 Macro Suite is already running.", L"Error", MB_ICONERROR);
        return 1;
    }

    // ── System setup ──
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto NtSetTimerResolution = reinterpret_cast<NTSETTIMERRESOLUTION>(reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSetTimerResolution")));
        if (NtSetTimerResolution) {
            ULONG currentRes;
            // Set 0.5ms (5000 100-ns units) timer resolution
            NtSetTimerResolution(5000, TRUE, &currentRes);
        } else {
            timeBeginPeriod(1);
        }
    } else {
        timeBeginPeriod(1);
    }
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    SAFE_STARTUP_TRACE("SUBSYSTEM_INIT");
    // ── Initialize subsystems ──
    dlog::Init();
    timing::Init();
    rcfg::Init();
    target_platform::Init();
    telemetry::Init();
    telemetry::StartTelemetryThread();
    analysis::Init();
    topology::Init();
    topology::PinHookThread(L"Games");

    // Load config from INI (or use defaults)
    {
        RuntimeConfig cfg{};
        if (config_io::Load(cfg)) {
            rcfg::Apply(cfg);
            DLOG_INFO(Config, "Config loaded from %ls", reinterpret_cast<int64_t>(config_io::GetConfigPath()));
        } else {
            DLOG_INFO(Config, "No config file found, using defaults");
        }
    }

    movement::InitLUT();
    bhop::Init();

    SAFE_STARTUP_TRACE("WINDOW_CREATION");
    // ── Create hidden message window (timer/hook messages) ──
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MsgWndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = L"CS2MsgClass";
    RegisterClassExW(&wc);

    HWND msgHwnd = CreateWindowExW(0, L"CS2MsgClass", L"CS2Msg",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    if (!msgHwnd) {
        MessageBoxW(nullptr, L"Failed to create message window.", L"Error", MB_ICONERROR);
        bhop::Shutdown();
        target_platform::Shutdown();
        timeEndPeriod(1);
        return 1;
    }

    // ── Init state engine ──
    engine::Init(msgHwnd);

    // ── Start timer thread ──
    timing::StartTimerThread(msgHwnd);

#if MARCO_ENABLE_UI
    // ── Create UI window ──
    HWND uiHwnd = ui::Create(hInst, msgHwnd);
    if (!uiHwnd) {
        MessageBoxW(nullptr, L"Failed to create UI window.", L"Error", MB_ICONERROR);
        bhop::Shutdown();
        target_platform::Shutdown();
        timing::StopTimerThread();
        timeEndPeriod(1);
        return 1;
    }
#endif

    SAFE_STARTUP_TRACE("HOOK_INSTALL");
    // ── Install hooks ──
    if (!capture::Install(msgHwnd)) {
        MessageBoxW(nullptr, L"Failed to install input hooks.\nRun as Administrator?",
                    L"Error", MB_ICONERROR);
        bhop::Shutdown();
        target_platform::Shutdown();
        timing::StopTimerThread();
#if MARCO_ENABLE_UI
        ui::Destroy();
#endif
        timeEndPeriod(1);
        return 1;
    }
    engine::SetHookInstalled(true);
    DLOG_INFO(Runtime, "TRACE: engine::SetHookInstalled(true) OK");

    // ── Start watchdog timer ──
#if MARCO_ENABLE_WATCHDOG
    g_watchdogTimerId = SetTimer(msgHwnd, 1, rcfg::Get().watchdogIntervalMs, WatchdogTimerProc);
    engine::StartWatchdog();
    DLOG_INFO(Runtime, "TRACE: Watchdog started OK");
#endif

#if MARCO_ENABLE_ETW
    // ── Start Kernel ETW Tracing ──
    etw::StartGlobalTrace();
    DLOG_INFO(Runtime, "TRACE: etw::StartGlobalTrace OK");
#endif

    SAFE_STARTUP_TRACE("MESSAGE_LOOP");
    DLOG_INFO(Runtime, "CS2 Macro Suite initialized");



    // ════════════════════════════════════════════════════════════════
    //  MESSAGE LOOP (serves both hidden msg window and UI window)
    // ════════════════════════════════════════════════════════════════
#if MARCO_ENABLE_DIAGNOSTICS
    ui_diagnostics::StartWatchdog();
#endif

    MSG msg;
    while (true) {
#if MARCO_ENABLE_HEARTBEATS
        telemetry::g_blockedHook.store(true, std::memory_order_relaxed);
#endif

        // [PHASE 3] Hardened MsgPump using waitable objects instead of pure blocking GetMessage
        DWORD waitRes = MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
        (void)waitRes;
        
        int64_t startUs = timing::NowUs();

#if MARCO_ENABLE_DIAGNOSTICS
        ui_diagnostics::StartDispatch();
#endif
#if MARCO_ENABLE_HEARTBEATS
        telemetry::g_blockedHook.store(false, std::memory_order_relaxed);
#endif
#if MARCO_ENABLE_DIAGNOSTICS
        ui_diagnostics::g_uiHeartbeatUs.store(timing::NowUs(), std::memory_order_relaxed);
#endif

        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        int64_t dispatchUs = timing::NowUs() - startUs;
        if (dispatchUs > 100000) {
            DLOG_WARN(Runtime, "DispatchMessage stalled for %lld us!", dispatchUs);
        }

#if MARCO_ENABLE_DIAGNOSTICS
        ui_diagnostics::EndDispatch();
#endif

        if (quit) break;
    }
    
#if MARCO_ENABLE_DIAGNOSTICS
    ui_diagnostics::StopWatchdog();
#endif

    // ════════════════════════════════════════════════════════════════
    //  CLEANUP
    // ════════════════════════════════════════════════════════════════
    DLOG_INFO(Shutdown, "Shutting down...");
#if MARCO_ENABLE_WATCHDOG
    engine::StopWatchdog();
    if (g_watchdogTimerId) KillTimer(msgHwnd, g_watchdogTimerId);
#endif
    
    // Ensure all injected keys are released before unhooking
    engine::ClearHeldKeys();
    
    // [FIX R-5] Shutdown order critical to avoid races
    capture::Uninstall();
    engine::SetHookInstalled(false);
    bhop::Shutdown();
    timing::StopTimerThread();
    target_platform::Shutdown();
#if MARCO_ENABLE_TELEMETRY
    telemetry::StopTelemetryThread();
    telemetry::Shutdown();
#endif
#if MARCO_ENABLE_ETW
    etw::StopGlobalTrace();
#endif
    
    // Safe to call Get() now - all threads stopped
    RuntimeConfig finalCfg = rcfg::Get();
    config_io::Save(finalCfg);
#if MARCO_ENABLE_UI
    ui::Destroy();
#endif
    dlog::Shutdown();
    timeEndPeriod(1);
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }

    return 0;
}
