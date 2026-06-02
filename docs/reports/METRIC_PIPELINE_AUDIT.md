# METRIC PIPELINE AUDIT

**Investigation Target:** THREAD HEALTH & SAFETY metrics returning constant zero in the UI.

## 1. Scheduler Spikes
* **Source File:** `src/core/timing.cpp` (Producer), `src/ui/ui_dashboard.cpp` (Consumer)
* **Producer Function:** `timing::TimerThreadFunc()`
* **Consumer Function:** `ui_dashboard.cpp` (inside `THREAD HEALTH & SAFETY` panel)
* **Current Status:** **DEAD PRODUCER**

## 2. Peak Oversleep
* **Source File:** `src/core/timing.cpp` (Producer), `src/ui/ui_dashboard.cpp` (Consumer)
* **Producer Function:** `timing::TimerThreadFunc()`
* **Consumer Function:** `ui_dashboard.cpp` (inside `THREAD HEALTH & SAFETY` panel)
* **Current Status:** **DEAD PRODUCER**

## 3. Core Migrations
* **Source File:** `src/core/input_capture.cpp` & `src/core/timing.cpp` (Producers), `src/ui/ui_dashboard.cpp` (Consumer)
* **Producer Function:** `capture::KeyboardProc()` and `timing::TimerThreadFunc()`
* **Consumer Function:** `ui_dashboard.cpp` (inside `THREAD HEALTH & SAFETY` panel)
* **Current Status:** **DEAD PRODUCER**

## 4. Wake Variance
* **Source File:** `src/core/timing.cpp` (Producer), `src/ui/ui_dashboard.cpp` (Consumer)
* **Producer Function:** `timing::UpdateAdaptiveController()`
* **Consumer Function:** `ui_dashboard.cpp` (inside `THREAD HEALTH & SAFETY` panel)
* **Current Status:** **DEAD PRODUCER**

---

## BUG REPORT

### Root Cause
All four metrics suffer from the exact same preprocessor architecture bug: **Mismatched Conditional Compilation**.

In `include/ui/build_config.h`, the release build disables forensics:
`#define MARCO_ENABLE_FORENSIC 0`

Because of this, the entire pipeline for these four metrics is completely excluded from the final Release binary by the C++ preprocessor:
1. The **producers** (`fetch_add`/`store` in `timing.cpp` and `input_capture.cpp`) are wrapped in `#if MARCO_ENABLE_FORENSIC`.
2. The **storage variables** in `telemetry.cpp` and `telemetry.h` are conditionally compiled out.
3. The **publisher** in `state_engine.cpp` (which assigns to `RuntimeSnapshot`) is wrapped inside a massive `#if MARCO_ENABLE_FORENSIC` block spanning lines 273–407.

However, the **consumer** (the UI in `ui_dashboard.cpp`) was recently updated to read from `RuntimeSnapshot` unconditionally. Since `state_engine.cpp` never writes to these struct fields during Release builds, they simply retain their zero-initialized default values (`0`), which the UI faithfully renders.

### Exact Source Locations
* **Configuration:** `include/ui/build_config.h` (Line 27: `#define MARCO_ENABLE_FORENSIC 0`)
* **Producers Stripped:** 
  * `src/core/timing.cpp` (Lines 128, 153, 265, 268)
  * `src/core/input_capture.cpp` (Line 191, 405)
* **Publisher Stripped:** `src/core/state_engine.cpp` (Lines 273-407)
* **Unconditional Consumer:** `src/ui/ui_dashboard.cpp` (Lines 297, 300, 305, 308)

### Minimal Patch Recommendation
Do NOT fake or hardcode the metrics. To restore this telemetry in Release mode:
1. Move the declarations for `g_schedulerSpikes`, `g_coreMigrations`, `g_timerOversleepPeak`, and `g_wakeVarianceUs` outside of the `#if MARCO_ENABLE_FORENSIC` guards in `telemetry.h` and `telemetry.cpp`.
2. Move their producer logic (`fetch_add`, `compare_exchange`, `store`) out of the `#if` guards in `timing.cpp` and `input_capture.cpp`.
3. Move the assignments (`out.schedulerSpikeCount = ...`) out of the `#if MARCO_ENABLE_FORENSIC` block in `src/core/state_engine.cpp` (around line 315) so they are populated unconditionally in all builds.
