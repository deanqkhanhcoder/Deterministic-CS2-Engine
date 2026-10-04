# Marco Engine — Deterministic CS2 Movement Engine

Marco is a high-performance Windows C++20 sub-tick movement reconstruction and counter-strafe engine designed for Counter-Strike 2. It captures raw hardware input, estimates 2D continuous velocity based on the exact Source SDK physics pipeline (`PmFriction` → `PmAccelerate`), and computes microsecond-precise opposing brake durations to eliminate early-firing inaccuracy.

Operating as a headless background daemon with a local loopback IPC REST/SSE server (`127.0.0.1:47650`), Marco is paired with a modern React + Tailwind Web Dashboard (`ui/`) to provide real-time telemetry visualizers, live weapon profile tuning, and safe configuration management.

---

## Key Highlights

- **Headless Daemon Architecture**: The core C++ engine runs as a zero-jitter, background daemon. No legacy GDI windows or message loop UI stalls interfere with sub-tick input processing.
- **Local IPC & Real-Time Telemetry Stream**: Exposes a local-only REST API and Server-Sent Events (SSE) stream (`127.0.0.1:47650`) with auto-generated session token authentication (`./marco.token`).
- **Modern Esports Web UI**: Built with React 19, TypeScript, Tailwind CSS, and Vite in `ui/`. Provides a live sub-tick key visualizer, microsecond jitter/oversleep meters, weapon profile tuning, and safe mode controls.
- **Exact Valve Physics Simulation**:
  - Sequential physics order: runs friction reduction followed by active opposite acceleration.
  - Zero-overlap counter-strafe: eliminates overlapping conflicting keys (overlap = 0 µs) to ensure maximum deceleration per tick.
  - Diagonal counter-strafe normalization: scales component velocity by $1/\sqrt{2} \approx 0.707$ and trims 1 tick (~15.6 ms) of excess brake hold to eliminate over-counter-strafe and reverse recoil jerk.
  - Directional momentum memory across rapid A-D strafes.
- **Handover Documentation**: See [`docs/HANDOVER_CONTEXT.md`](file:///c:/Users/toanpq/Desktop/marco/docs/HANDOVER_CONTEXT.md) for full architectural blueprints, mathematical specifications, and historical bug resolutions for incoming engineers/AI agents.
- **Safe Mode with Snapshot Reversion**: Entering Safe Mode applies temporary clamp overlays without destroying user settings. Exiting or triggering Revert to Snapshot instantly restores the pre-safemode configuration.
- **Microsecond Precision Hybrid Timing**: Utilizes `timeBeginPeriod(1)` and high-resolution hardware counters (`QPC`/`QPF`) paired with spin-wait loops (`_mm_pause`) for microsecond key release accuracy.
- **Two Lean Build Targets**: Directly outputs `./marco.exe` (Release) and `./marco_debug.exe` (Debug) at the workspace root.

---

## Architecture Overview

```text
Hardware Input (LL Hook)
   │
   ▼
[Physical Key Tracking] ──► [Input Router & Focus Filter]
                                   │
                                   ▼
                       [State Reconstruction Engine]
                                   │
               ┌───────────────────┴───────────────────┐
               ▼                                       ▼
    [Continuous Velocity Model]             [BHOP State Machine]
    (Friction -> Accelerate)                           │
               │                                       │
               ▼                                       ▼
    [Counter-Strafe Controller]             [Cadence Generator]
               │                                       │
               └───────────────────┬───────────────────┘
                                   │
                                   ▼
                       [High-Precision Timer (QPC)]
                                   │
                                   ▼
                       [SendInput Injection Boundary]
                                   ▲
                                   │
                    ┌──────────────┴──────────────┐
                    ▼                             ▼
         [Telemetry / Forensics]        [Local IPC Daemon]
                                        (REST / SSE :47650)
                                                  │
                                                  ▼
                                         [React Web Dashboard]
                                         (Vite / Tailwind)
```

---

## Build and Run

### Prerequisites
- Windows 10/11 x64
- MinGW-w64 with C++20 support (`g++`)
- GNU Make (`mingw32-make`)
- CMake 3.20+ (for test suite)
- Node.js 18+ and npm (for Web UI)

### 1. Build Core Engine
Build either the Release headless daemon or the Debug diagnostic binary directly to the project root:

```powershell
# Build Release daemon (./marco.exe)
mingw32-make release

# Build Debug binary with diagnostics (./marco_debug.exe)
mingw32-make debug
```

### 2. Run Test Suite
Run the 18-test regression and integration suite:

```powershell
mingw32-make check
```

### 3. Build & Run Web UI
The Web UI is located in `ui/`:

```powershell
cd ui
npm install
npm run build     # Generates production bundle in ui/dist (served directly by marco.exe)

# Or run Vite dev server for hot reloading:
npm run dev       # Starts dev server on http://localhost:5173
```

---

## Local IPC REST/SSE API

The core daemon binds strictly to `127.0.0.1:47650`. On startup, a random 32-character hexadecimal token is generated and written to `./marco.token`. All API requests require the `X-Marco-Token` header or `?token=<token>` query parameter.

| Endpoint | Method | Description |
|---|---|---|
| `/` | `GET` | Serves compiled React Web UI (`ui/dist/index.html`) |
| `/api/config` | `GET` | Returns pure user configuration JSON |
| `/api/config` | `POST` | Updates and persists configuration to `marco.ini` |
| `/api/telemetry` | `GET` | Returns latest snapshot of latency, jitter, and key states |
| `/api/events` | `GET` | SSE stream pushing real-time telemetry updates (~30Hz) |
| `/api/profile` | `POST` | Changes active weapon profile (`1`=Rifle, `2`=Pistol, `3`=Sniper, `4`=SMG) |
| `/api/safemode` | `POST` | Toggles safe mode with automatic snapshot backup |
| `/api/revert` | `POST` | Reverts current configuration to the pre-safemode snapshot |
| `/api/suspend` | `POST` | Toggles engine input processing suspend state |
| `/api/quit` | `POST` | Gracefully shuts down the background daemon |

---

## Weapon Profiles & Physical Parameters

| Profile | Accuracy Threshold | Overlap Window | Brake Bias | Aggr. Curve |
|---|---|---|---|---|
| **Rifle (AK/M4)** | 34.0 u/s | 0 µs | 1.0x | 1.0x |
| **Pistol (USP/Glock)** | 34.0 u/s | 0 µs | 1.0x | 1.0x |
| **Sniper (AWP/SSG)** | 17.0 u/s | 0 µs | 1.0x | 1.0x |
| **SMG (MP9/Mac10)** | 34.0 u/s | 0 µs | 1.0x | 1.0x |

---

## License

This project is licensed under the MIT License. See `LICENSE`.
