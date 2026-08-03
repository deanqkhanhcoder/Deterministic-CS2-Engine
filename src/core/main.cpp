// â•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—
// â•‘  CS2 Macro Suite â€” Main Entry Point                                 â•‘
// â•‘  Full native desktop application lifecycle                          â•‘
// â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <thread>
#include <ctime>
#include <atomic>

#include "build_config.h"
#include "message_pump_budget.h"
#include "config.h"
#include "types.h"
#include "timing.h"
#include "movement_reconstruction.h"
#include "state_engine.h"
#include "input_capture.h"
#include "bhop.h"
#include "ui_main.h"

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
#include "injection.h"

// â”€â”€ Crash Resilience & Startup Forensics â”€â”€
using NtSetTimerResolutionFn = LONG (WINAPI *)(ULONG, BOOLEAN, PULONG);

static std::atomic<const char*> g_startupPhase{"INIT"};
static std::atomic<bool> g_timerPeriodActive{false};
static std::atomic<bool> g_ntTimerResolutionActive{false};
static NtSetTimerResolutionFn g_ntSetTimerResolution = nullptr;

static void RestoreTimerResolution() {
    if (g_ntTimerResolutionActive.exchange(false, std::memory_order_acq_rel) &&
        g_ntSetTimerResolution != nullptr) {
        ULONG currentResolution = 0;
        g_ntSetTimerResolution(5000, FALSE, &currentResolution);
    }
    if (g_timerPeriodActive.exchange(false, std::memory_order_acq_rel)) {
        timeEndPeriod(1);
    }
}

#define SAFE_STARTUP_TRACE(phase) do { g_startupPhase.store(phase, std::memory_order_release); DLOG_INFO(Startup, "Startup Phase: %s", phase); } while(0)

static LONG WINAPI CrashVectoredExceptionHandler(EXCEPTION_POINTERS* ep) {
    // Only handle severe crashes, ignore debugger events like STATUS_BREAKPOINT.
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW) {

        // A vectored exception handler can run on any thread while locks are held.
        // Do not join workers, flush lock-based queues, or perform input injection
        // here. Record only crash-safe evidence; normal lifecycle cleanup owns
        // release/unhook operations.
        wchar_t modulePath[MAX_PATH] = {};
        const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (pathLength > 0 && pathLength < MAX_PATH) {
            wchar_t* separator = wcsrchr(modulePath, L'\\');
            if (separator != nullptr) {
                *separator = L'\0';
                wcscat_s(modulePath, L"\\startup_crash.log");
                HANDLE crashFile = CreateFileW(modulePath, GENERIC_WRITE, FILE_SHARE_READ,
                                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                               nullptr);
                if (crashFile != INVALID_HANDLE_VALUE) {
                    char crashRecord[256] = {};
                    const int recordLength = _snprintf_s(
                        crashRecord, sizeof(crashRecord), _TRUNCATE,
                        "CRITICAL STARTUP CRASH%c%cPhase: %s%c%cException Code: 0x%08lX%c%c",
                        13, 10, g_startupPhase.load(std::memory_order_acquire), 13, 10,
                        static_cast<unsigned long>(code), 13, 10);
                    if (recordLength > 0) {
                        DWORD written = 0;
                        WriteFile(crashFile, crashRecord,
                                  static_cast<DWORD>(recordLength), &written, nullptr);
                        FlushFileBuffers(crashFile);
                    }
                    CloseHandle(crashFile);
                }
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}



// ── Main window procedure for the hidden message window ──
// This receives timer-thread messages and hotkey commands.
// The UI window (ui_main) handles its own WndProc separately.
static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#if MARCO_ENABLE_HEARTBEATS

#endif
    switch (msg) {
        case WM_TOGGLE_SUSPEND: {
            engine::ToggleSuspend();
            if (engine::IsSuspended()) bhop::OnSpaceUp();
            bhop::OnSuspendChanged();
            ui::OnStateChanged();
            DLOG_WARN(Runtime, "Suspend: %s", (engine::IsSuspended() ? "ON" : "OFF"));
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
            RuntimeConfig cfg = rcfg::GetMutable();
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
            capture::ReconcileTargetFocus();
            ui::OnStateChanged();
            return 0;
        }

        case WM_ROUTED_INPUT_READY: {
            capture::DrainRoutedInputEvents();
            return 0;
        }

        case WM_TIMER_EXPIRED:
            timing::DrainExpiredTimers();
            return 0;

        case WM_BHOP_INJECTION_READY:
            bhop::DrainInjectionRequests();
            return 0;

        case WM_EMERGENCY_UNHOOK: {
            const auto shutdownTarget = target_platform::GetCurrentIdentity();
            capture::Uninstall();
            engine::SetHookInstalled(false);
            bhop::Shutdown();
            timing::StopTimerThread();
            engine::ClearHeldKeys(shutdownTarget);
            const UINT failsafeReleased =
                injection::ShutdownAndReleaseForTarget(shutdownTarget);
            const size_t pendingReleases = injection::PendingReleaseCount();
            if (pendingReleases != 0) {
                DLOG_ERR(Shutdown,
                         "Emergency release incomplete: released=%lld pending=%lld",
                         static_cast<int64_t>(failsafeReleased),
                         static_cast<int64_t>(pendingReleases));
            }
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

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  ENTRY POINT
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // â”€â”€ Crash handler â”€â”€
    AddVectoredExceptionHandler(1, CrashVectoredExceptionHandler);
    SAFE_STARTUP_TRACE("MUTEX_CHECK");

    // â”€â”€ Prevent multiple instances â”€â”€
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"CS2MacroSuite_Mutex");
    if (mutex == nullptr) {
        MessageBoxW(nullptr, L"Failed to create the single-instance mutex.",
                    L"Error", MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"CS2 Macro Suite is already running.", L"Error", MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }

    // â”€â”€ System setup â”€â”€
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        g_ntSetTimerResolution = reinterpret_cast<NtSetTimerResolutionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSetTimerResolution")));
        if (g_ntSetTimerResolution) {
            ULONG currentRes = 0;
            // Set 0.5ms (5000 100-ns units) timer resolution
            if (g_ntSetTimerResolution(5000, TRUE, &currentRes) >= 0) {
                g_ntTimerResolutionActive.store(true, std::memory_order_release);
            }
        }
    }
    if (!g_ntTimerResolutionActive.load(std::memory_order_acquire) &&
        timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_timerPeriodActive.store(true, std::memory_order_release);
    }
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);


    SAFE_STARTUP_TRACE("SUBSYSTEM_INIT");
    // â”€â”€ Initialize subsystems â”€â”€
    dlog::Init();
    timing::Init();
    rcfg::Init();
    target_platform::Init();
    telemetry::Init();
    telemetry::StartTelemetryThread();

    workspace::EnsureRuntimeDirectoriesExist();
    std::string logDir = workspace::GetLogRootA();
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d_%H-%M-%S", &tm);
    std::string forensicLogPath = logDir + "marco_" + timeBuf + ".log";
#if MARCO_ENABLE_FORENSIC
    telemetry::InitForensics(forensicLogPath);
#endif
    DLOG_INFO(Startup, "Forensic log path: %s", forensicLogPath.c_str());
    analysis::Init();
    topology::Init();
    topology::PinHookThread(L"Games");

    // Load config from INI (or use defaults)
    {
        RuntimeConfig cfg{};
        if (config_io::Load(cfg)) {
            rcfg::Apply(cfg);
            DLOG_INFO(Config, "Config loaded from %ls", config_io::GetConfigPath());
        } else {
            DLOG_INFO(Config, "No config file found, using defaults");
        }
    }

    movement::InitLUT();

    SAFE_STARTUP_TRACE("WINDOW_CREATION");
    // â”€â”€ Create hidden message window (timer/hook messages) â”€â”€
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
        telemetry::StopTelemetryThread();
#if MARCO_ENABLE_FORENSIC
        telemetry::ShutdownForensics();
#endif
        telemetry::Shutdown();
        target_platform::Shutdown();
        dlog::Shutdown();
        RestoreTimerResolution();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }

    target_platform::SetNotifyWindow(msgHwnd);
    bhop::Init(msgHwnd);

    // â”€â”€ Init state engine â”€â”€
    engine::Init(msgHwnd);

    // â”€â”€ Start timer thread â”€â”€
    timing::StartTimerThread(msgHwnd);

#if MARCO_ENABLE_UI
    // â”€â”€ Create UI window â”€â”€
    HWND uiHwnd = ui::Create(hInst, msgHwnd);
    if (!uiHwnd) {
        MessageBoxW(nullptr, L"Failed to create UI window.", L"Error", MB_ICONERROR);
        bhop::Shutdown();
        timing::StopTimerThread();
        telemetry::StopTelemetryThread();
#if MARCO_ENABLE_FORENSIC
        telemetry::ShutdownForensics();
#endif
        telemetry::Shutdown();
        target_platform::Shutdown();
        dlog::Shutdown();
        RestoreTimerResolution();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
#endif

    SAFE_STARTUP_TRACE("HOOK_INSTALL");
    // â”€â”€ Install hooks â”€â”€
    if (!capture::Install(msgHwnd)) {
        MessageBoxW(nullptr, L"Failed to install input hooks.\nRun as Administrator?",
                    L"Error", MB_ICONERROR);
        bhop::Shutdown();
        timing::StopTimerThread();
        telemetry::StopTelemetryThread();
#if MARCO_ENABLE_FORENSIC
        telemetry::ShutdownForensics();
#endif
        telemetry::Shutdown();
        target_platform::Shutdown();
#if MARCO_ENABLE_UI
        ui::Destroy();
#endif
        dlog::Shutdown();
        RestoreTimerResolution();
        DestroyWindow(msgHwnd);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
    engine::SetHookInstalled(true);
    DLOG_INFO(Runtime, "TRACE: engine::SetHookInstalled(true) OK");

    // â”€â”€ Start watchdog timer â”€â”€

#if MARCO_ENABLE_ETW
    // â”€â”€ Start Kernel ETW Tracing â”€â”€
    etw::StartGlobalTrace();
    DLOG_INFO(Runtime, "TRACE: etw::StartGlobalTrace OK");
#endif

    SAFE_STARTUP_TRACE("MESSAGE_LOOP");
    DLOG_INFO(Runtime, "CS2 Macro Suite initialized");



    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    //  MESSAGE LOOP (serves both hidden msg window and UI window)
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•


    MSG msg;
    while (true) {
#if MARCO_ENABLE_HEARTBEATS
        telemetry::g_blockedHook.store(true, std::memory_order_relaxed);
#endif

        // [PHASE 3] Hardened MsgPump using waitable objects instead of pure blocking GetMessage
        // A failed synthetic release switches the injection boundary to
        // release-only mode. Wake periodically so reconciliation does not
        // depend on shutdown, focus traffic, or any further input request. The
        // boundary blocks all new non-release events while any accepted down
        // still lacks its matching up, so this timer cannot generate input.
        constexpr DWORD kReleaseReconcileIntervalMs = 50;
        DWORD waitRes = MsgWaitForMultipleObjectsEx(
            0, nullptr, kReleaseReconcileIntervalMs, QS_ALLINPUT,
            MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
        (void)waitRes;

        if (injection::PendingReleaseCount() != 0) {
            const auto activeTarget = target_platform::GetCurrentIdentity();
            if (activeTarget.IsValid()) {
                (void)engine::ReconcilePendingOutput(activeTarget);
            }
        }

#if MARCO_ENABLE_HEARTBEATS
        telemetry::g_blockedHook.store(false, std::memory_order_relaxed);
#endif


        const message_pump::DrainResult drain =
            message_pump::DrainPendingMessages(msg, timing::NowUs);
        const bool quit = drain.quit;
        const int64_t dispatchUs = drain.elapsedUs;
        if (dispatchUs > 100000) {
            DLOG_WARN(
                Runtime,
                "Message pump stalled: batch=%lld us dispatched=%llu "
                "slow_msg=0x%04X slow_hwnd=%p slow=%lld us",
                static_cast<long long>(dispatchUs),
                static_cast<unsigned long long>(drain.dispatched),
                static_cast<unsigned>(drain.slowestMessage),
                static_cast<void*>(drain.slowestHwnd),
                static_cast<long long>(drain.slowestDispatchUs));
        }



        if (quit) break;
    }
    


    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    //  CLEANUP
    // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
    DLOG_INFO(Shutdown, "Shutting down...");
    
    // Stop all producers before the final fail-safe release.
    const auto shutdownTarget = target_platform::GetCurrentIdentity();
    capture::Uninstall();
    engine::SetHookInstalled(false);
    bhop::Shutdown();
    timing::StopTimerThread();

    // Reconcile logical movement releases, then release every synthetic key
    // still owned by the injection backend. Retry transient shutdown failures
    // for a bounded interval; a permanently failing backend remains visible.
    engine::ClearHeldKeys(shutdownTarget);
    UINT failsafeReleased =
        injection::ShutdownAndReleaseForTarget(shutdownTarget);
    constexpr unsigned kShutdownReleaseRetryPasses = 20;
    constexpr DWORD kShutdownReleaseRetryDelayMs = 10;
    for (unsigned pass = 0;
         pass < kShutdownReleaseRetryPasses &&
             injection::PendingReleaseCount() != 0;
         ++pass) {
        Sleep(kShutdownReleaseRetryDelayMs);
        failsafeReleased +=
            injection::ReconcilePendingReleasesForTarget(shutdownTarget);
    }
    const size_t pendingReleases = injection::PendingReleaseCount();
    if (pendingReleases != 0) {
        DLOG_ERR(Shutdown,
                 "Failsafe release incomplete: released=%lld pending=%lld",
                 static_cast<int64_t>(failsafeReleased),
                 static_cast<int64_t>(pendingReleases));
    }

    // [FIX R-5] Shutdown order critical to avoid races
    telemetry::StopTelemetryThread();
    target_platform::Shutdown();
#if MARCO_ENABLE_FORENSIC
    telemetry::ShutdownForensics();
#endif
    telemetry::Shutdown();
#if MARCO_ENABLE_ETW
    etw::StopGlobalTrace();
#endif
    
    // Safe to call Get() now - all threads stopped
    RuntimeConfig finalCfg = rcfg::Get();
    config_io::Save(finalCfg);
#if MARCO_ENABLE_UI
    ui::Destroy();
#endif
    if (!dlog::Flush()) {
        DLOG_ERR(Shutdown, "Logger flush timed out with %lld pending entries",
                 static_cast<int64_t>(dlog::PendingCount()));
    }
    dlog::Shutdown();
    RestoreTimerResolution();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }

    return 0;
}
