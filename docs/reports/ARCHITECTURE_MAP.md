# MARCO ENGINE ARCHITECTURE MAP
*Date: 2026-08-03 | Engine Version: V27.8*

## 1. Project Structure
- `src/core/`: Core logic (Hooks, State Engine, BHOP, Counter-Strafe, Telemetry, Timing, Workspace).
- `src/ui/`: UI components (Dashboard, Settings, Diagnostics, Analysis).
- `include/core/` & `include/ui/`: Public interfaces.
- `docs/`: Central documentation and knowledge base (`MARCO_KNOWLEDGE_BASE.md`).
- `tests/`: Testing infrastructure (Unit, Integration, Regression fixtures).
- `tools/`: Diagnostic and forensic simulation tools (`deterministic_simulator.cpp`, `parse_trace.py`).

## 2. Path Governance (Runtime Paths)
All runtime paths MUST go through `src/core/workspace.cpp`. No hardcoded local paths are permitted.
- `runtime/bin/`: Built executables (`marco.exe`, `marco_debug.exe`, `marco_profile.exe`).
- `runtime/logs/`: Debug logs (`marco_debug.log`) and Forensic session logs.
- `runtime/crash/`: Minidumps and crash handlers.
- `runtime/artifacts/`: ETW traces and CSV exports.
- `runtime/captures/`: Runtime captures.

## 3. Subsystem Ownership
| Subsystem | File(s) | Responsibility |
|---|---|---|
| **Input Capture** | `input_capture.cpp` | OS low-level hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`). Evaluates active focus. |
| **Input Router** | `input_router.cpp` | Semantic routing and `RoutedInputQueue`. Decides if hardware key inputs trigger engine features. |
| **State Engine** | `state_engine.cpp` | Unified physical + logical state, UI publication (`s_pubState`). |
| **State Reconciliation**| `state_reconciliation.cpp` | Re-syncs physical to logical state when transitioning focus or suspend modes. |
| **Counter-Strafe** | `counterstrafe_controller.cpp` | Movement mechanics. Resolves WASD axis conflicts and queues braking. |
| **BHOP** | `bhop.cpp` | Space bar simulation, `BhopInjectionQueue` worker, jitter-compensated timing. |
| **Timing** | `timing.cpp` | Microsecond-precise timer loop using QPC spin-waiting. |
| **Target Platform** | `target_platform.cpp` | Background process resolution (CS2/Roblox). |
| **Runtime Config** | `runtime_config.cpp` | Lock-free Seqlock configuration management protected by `s_writerMutex`. |
| **Telemetry** | `telemetry.cpp` | Forensic event collection, ETW tracing, and auto-flushing logic. |
| **Debug Logger** | `debug_logger.cpp` | Bounded asynchronous logging worker (`kMaxQueuedMessages = 4096`). |

## 4. Observability & Logging Architecture
- **Production Telemetry**: Tracks critical events (`FOCUS_LOST`, `FOCUS_GAINED`, `PROFILE_CHANGED`) via `ForensicRingBuffer` across all builds.
- **Production Logging**: Retains WARN/ERROR/FATAL logs in Release builds while suppressing high-overhead TRACE/INFO.
- **Crash Flushing**: Handled via `SetUnhandledExceptionFilter`, automatically dumps the Ring Buffer to `runtime/crash/` and `runtime/logs/`.
- **ETW Integration**: Real-time DPC/ISR profiling available via `etw_controller.cpp`.

## 5. Threading Model
- **UI Thread**: Handles rendering and config writes (serialized by `s_writerMutex`).
- **Hook Thread**: Dispatches `KeyboardProc` and `MouseProc`. Critical priority, wait-free, never blocks on disk I/O.
- **Focus Thread**: Evaluates active window foreground changes asynchronously.
- **Resolver/Scanner Threads**: Scans process lists to identify the target game, synchronized via `s_resolverCv`.
- **Timer Thread**: Spin-loops for nanosecond precision dispatch of delayed inputs.
- **BHOP Thread**: Sleeps on a condition variable, wakes to execute jump sequences, dispatches synthetic inputs safely through `BhopInjectionQueue`.