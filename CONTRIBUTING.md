# Contributing to Deterministic-CS2-Engine

First off, thank you for considering contributing to the Deterministic-CS2-Engine!

## 1. Mathematical Determinism
This project operates on strict mathematical determinism. Any pull request that alters the core physics (`movement_reconstruction.cpp`, `input_router.cpp`) MUST pass the offline regression suite (`scripts/test/run_regression.py`). 

## 2. Build Requirements
- **GCC**: We strictly use MinGW g++ C++20.
- **Flags**: All code MUST compile with `-Wall -Wextra -pedantic` without any warnings.
- **Formatting**: We use `clang-format`. Please run it before submitting.

## 3. Pull Request Process
1. Fork the repo and create your branch from `main`.
2. If you've added code that should be tested, add tests.
3. If you've changed APIs, update the documentation.
4. Ensure the test suite passes (`make clean && make release && python scripts/test/run_regression.py`).
5. Issue that pull request!

## 4. Concurrency Policy
- `s_stateMutex` must be held for State logic.
- `s_spinlock` must be held for Timers.
- `SendInput` must **NEVER** be called while holding any mutex. All inputs must be queued via `InjectionBatch`.
