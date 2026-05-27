<div align="center">
  
# Deterministic CS2 Engine (V26.1 Stable Release)
### *Production-Grade Mathematical Physics Engine for Counter-Strike 2*

[![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Determinism](https://img.shields.io/badge/Physics-100%25_Deterministic-blue.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-orange.svg)]()

</div>

---

## 📖 Project Overview

The **V26.1 Deterministic-CS2-Engine** is a heavily optimized, mathematically deterministic input simulation framework designed to perfectly synchronize hardware HID inputs with the Sub-Tick kinematics of Counter-Strike 2. 

Unlike conventional macro engines that rely on arbitrary sleep timers, this engine operates on a mathematically proven Look-Up Table (LUT) driven by the actual CS2 Source 2 physics formulas. It guarantees exact, frame-perfect braking and strafing interpolation.

---

## 🎯 Architecture & Determinism

### 1. Mathematical Physics Engine
The core of V26 is the `input_router` and `movement_reconstruction`. The engine interprets raw keystrokes and computes the player's exact logical velocity vector (`Vx`, `Vy`). When a counter-strafe is requested, the engine calculates the exact theoretical deceleration curve and injects sub-tick opposing forces to achieve instant 0.0 velocity.
- **Zero Mathematical Drift:** Offline regressions (`run_regression.py`) prove that our C++ physics model perfectly mirrors the 64-bit Python baseline with 0.0 drift.
- **Sub-Tick Quantization:** Timer dispatchers are pinned to P-Cores utilizing custom spinlocks to circumvent standard OS scheduler jitter (typically 15.6ms), achieving sub-millisecond precision.

### 2. Thread Model & Deadlock Elimination
The engine runs asynchronously across specialized threads:
1. **Low-Level Keyboard Hook Thread (`LowLevelKeyboardProc`):** Captures hardware input via `SetWindowsHookEx`.
2. **Timer Spinlock Thread:** Executes scheduled counter-forces with extreme precision.
3. **ImGui UI Thread:** Operates out-of-band to prevent UI lag from delaying physics.

**Concurrency Invariants:** 
- `s_stateMutex` (State Engine Lock) always precedes `s_spinlock` (Timing Lock). Circular waits are formally impossible.
- **OS Re-entrancy Protection:** `SendInput` is *never* called while holding a mutex. All OS-bound events are buffered into an `InjectionBatch` and flushed after unlocking, eliminating all vectors for Ring-0/User-Mode deadlocks.

### 3. Features
- **Perfect Counter-Strafe:** Calculates optimal reverse-key duration to perfectly cancel momentum.
- **AutoFire Management:** Independent timer lifecycle management that defers to WASD priority.
- **Bhop Interpolation:** Precision space-bar spam simulation decoupled from strafe vectors.
- **Forensic Toolkit:** A full suite of Python analysis tools for empirical latency plotting.

---

## 🚀 Build Instructions

We strictly enforce **Out-of-Source** builds. The repository must remain pristine.

### Prerequisites
- **MinGW-w64** (GCC 13+ with C++20 support)
- **GNU Make**
- Windows 10/11 x64

### Compilation
Open a PowerShell terminal in the repository root:

```bash
# Debug Mode (Forensic Logging Enabled)
make debug

# Profile Mode (-O2, Optimized, retains some symbols)
make profile

# Release Mode (-O3, LTO, Zero Dead Code)
make release
```

The compiled binary will be placed safely in `runtime/bin/marco.exe`.

---

## 🧪 Forensic Validation Suite

To ensure absolute determinism is maintained by all contributors, you must run the regression suite before submitting any Pull Requests.

```bash
python scripts/test/run_regression.py
```
This script compiles the `deterministic_simulator`, runs thousands of simulated ticks, and asserts that the C++ mathematical output precisely matches the `physics_v26_baseline.csv` golden master.

You can also generate heatmaps of your performance drift:
```bash
python tests/forensics/replay_drift_heatmap.py runtime/artifacts/golden_output.csv runtime/artifacts/current_output.csv runtime/artifacts/heatmap.png
```

---

## 📂 Repository Structure
```text
📦 Deterministic-CS2-Engine
 ┣ 📂 include/        # C++ Headers (Core Engine, UI)
 ┣ 📂 src/            # C++ Source Code
 ┣ 📂 scripts/        # Build & Regression Scripts
 ┣ 📂 tests/          # Forensic Python Tools & Baselines
 ┣ 📂 docs/           # Architecture Invariants & Reports
 ┣ 📂 runtime/        # Output directory (.gitignore'd)
 ┣ 📜 Makefile        # Multi-Configuration GNU Make
 ┗ 📜 README.md
```

---

## 🤝 Contributing

We welcome pull requests! However, please read the [CONTRIBUTING.md](CONTRIBUTING.md) strictly. Any PR that breaks mathematical determinism or violates the concurrency lock DAG will be rejected. 

Please review the [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## 🛡️ Security

If you discover an OS-level vulnerability, lock inversion, or hook escape, please review [SECURITY.md](SECURITY.md) for responsible disclosure.

## ⚖️ License
This project is licensed under the [MIT License](LICENSE).

---
*Disclaimer: This software simulates HID hardware input. Use responsibly. The authors assume no liability for account penalties or external consequences incurred through the use of this engine.*
