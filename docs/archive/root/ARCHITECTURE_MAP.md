# MARCO ENGINE V27.4 ARCHITECTURE MAP

## Subsystems & Ownership

| Subsystem | Primary Owner (Files) | Responsibility |
|---|---|---|
| **Input Capture** | `input_capture.cpp` | OS low-level hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`), OS event handling (`EVENT_SYSTEM_FOREGROUND`). Handles Swallow state. |
| **Input Router** | `input_router.cpp` | Semantic routing. Decides if a physical key event should be routed into gameplay features. |
| **State Engine** | `state_engine.cpp` | Thread-safe, unified physical + logical state management (`s_stateMutex`), UI publication (`s_pubState`), Watchdog. |
| **State Reconciliation**| `state_reconciliation.cpp` | Cleans up and re-syncs physical to logical state when transitioning focus or suspend modes. |
| **Counter-Strafe** | `counterstrafe_controller.cpp` | Listens to routed WASD edges, resolves axes, calculates true physical intent via `movement_reconstruction`, and queues counter-key injections. |
| **BHOP** | `bhop.cpp` | Runs an isolated worker thread (`BhopThreadFunc`) that wakes on Space edge, calculates randomized timing logic, and manages Space injection. |
| **Timing** | `timing.cpp` | High-resolution microsecond timer loop (`TimerThreadFunc`). Utilizes NtDelayExecution + QPC Spinloop + Adaptive Jitter Compensation. |
| **Target Platform** | `target_platform.cpp` | Background window evaluation (`ResolverWorker`, `ProcessScannerWorker`), lock-free publication of the active target identity. |
| **Runtime Config** | `runtime_config.cpp` | Seqlock-protected, lock-free config distribution. |
| **Telemetry** | `telemetry.cpp` | Forensic event collection, tracing, and log dumping. |

## Thread Model

The architecture utilizes a strict, highly concurrent thread model to isolate OS blocking from precision timing:

1. **Main UI Thread** (`ui_main.cpp`): Handles Window messages, Settings, and triggers `rcfg::Apply`.
2. **Hook Thread**: Dispatches `KeyboardProc` and `MouseProc`. Executed synchronously by the OS. Must NEVER block.
3. **Focus Thread** (`input_capture.cpp: s_focusThread`): Background thread bound to `WinEventProc`. Evaluates active window foreground changes.
4. **Target Resolver Thread** (`target_platform.cpp: ResolverWorker`): Consumes the `ResolveTargetAsync` queue to run heavy Win32 queries (`OpenProcess`, `GetClassNameW`) to verify game identity.
5. **Timer Thread** (`timing.cpp: TimerThreadFunc`): P-Core pinned, critical priority loop. Sleeps via `WaitableTimer`, spins via `_mm_pause`, dispatches `engine::OnTimerExpired`.
6. **BHOP Worker Thread** (`bhop.cpp: BhopThreadFunc`): Sleeps on a Condition Variable. Awoken when Space is held; executes precise QPC wait logic for jumps.
7. **Watchdog Thread** (Optional): Assesses heartbeat stamps to catch thread starvation or logical state corruption.

## Execution Chains

### Input Path (Semantic Routing)
`Hardware Keyboard` → `KeyboardProc`
→ calls `IsTargetActive()` (Checks lock-free Target Publication)
→ If Active: `engine::HandleKeyDown` (takes `s_stateMutex`)
→ Evaluates `s_state.phys`
→ Evaluates `routeSemantic`
→ Calls `ResolveAxis`
→ Injects KeyDown via `InjectionBatch` / Schedules KeyUp via `timing::ScheduleTimerUs`.

### Timer Path
`ScheduleTimerUs` (inserts into `s_slots`) → `s_dirty = true` → wakes `TimerThreadFunc` (CV Event)
→ Spin-waits using QPC → Fires `engine::OnTimerExpired` (takes `s_stateMutex`)
→ Validates `expectedTimerId`
→ Updates `s_state.logical`
→ Injects KeyUp via `InjectionBatch`.

### Focus Transition Path (Alt-Tab)
`User Alt-Tabs` → OS fires `EVENT_SYSTEM_FOREGROUND`
→ `WinEventProc` runs on Focus Thread
→ `IsTargetActive()` detects `fg != pub.hwnd` (`!isActive`)
→ `engine::ClearHeldKeys()` clears logical state and sends KeyUps
→ `KeyboardProc` runs on user releasing keys outside game -> `routeSemantic = 0`, ignores semantic logic.
→ `User Alt-Tabs back` → `WinEventProc` detects `isActive == true`
→ `engine::RebuildState()` queries `GetAsyncKeyState`, rebuilds `s_state.phys`, injects logical state to resume movement.

### Config Application Path
`UI Thread` → User changes setting → `rcfg::Apply`
→ Updates Seqlock buffer
→ Calls `movement::InitLUT()`
→ Hook Thread and Timer Thread seamlessly read from new config via `rcfg::Get()`.