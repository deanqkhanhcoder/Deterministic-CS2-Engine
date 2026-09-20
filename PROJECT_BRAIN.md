# Marco Project Brain

Date: 2026-08-03
Version: V27.8
Mode: Input Stabilization & Concurrency Safety

Tài liệu này là lõi trung tâm. 
Bất kỳ AI nào làm việc với dự án này CHỈ CẦN đọc 2 file sau:

1. `PROJECT_BRAIN.md` (Tài liệu này - Tóm tắt định hướng)
2. `docs/MARCO_KNOWLEDGE_BASE.md` (Toàn bộ kiến trúc và kiến thức kỹ thuật duy nhất)

---

## Current Architecture

* **Input Pipeline**: Windows LL Hooks -> Physical State -> Semantic Route (`RoutedInputQueue`) -> Game State -> Target-Bound Injection (`BhopInjectionQueue` / `InjectionBatch`).
* **Thread Model**: Lock-Free UI (Seqlock + `s_writerMutex`), OS Hook Thread (Wait-Free), Background Timer Spinloop, Async BHOP Worker (Owner-Thread Dispatched), Background Focus Resolver.
* **Core Loops**: Counter-Strafe sử dụng 2D physics LUT để tính toán delay tự phanh khẩn cấp.

## Current Status

V27.8

Source:
CERTIFIED

Build:
PASS (make debug, profile, release, check: 18/18 safe tests passed)

Runtime:
Stable Production Baseline

## Current Bugs

* **V27.5 - V27.7 hardening**: BUG-001 through BUG-011 have source fixes implemented and verified by automated unit/integration suites.
* Watchdog architecture completely purged in V27.6.
* DispatchMessage focus-storm stalls eliminated in V27.7.
* Seqlock write racing and unbounded log queue memory leak eliminated in V27.8.

*(Xem danh sách chi tiết tại `docs/MARCO_KNOWLEDGE_BASE.md` phần BUG REGISTRY).*

## Roadmap

**V27.6 Maturity & Observability Slimdown (COMPLETED)**:
1. Purged Watchdog Architecture (heartbeats, fail-safe triggers, UI watchdog).
2. Decoupled ForensicRingBuffer from MARCO_ENABLE_FORENSIC, keeping FOCUS/PROFILE events in production.
3. Cleaned up obsolete files and dead code (e.g., `autofire_controller.cpp`, F8 tracking, `WM_TIMER_EXPIRED`).
4. Retained WARN, ERROR, FATAL logging in Release builds.
5. RC Cleanup: Removed all ghost macros, dead diagnostic UI files, and stale configuration fields.
6. Telemetry Repair: Fixed missing thread health metrics in Release builds by decoupling them from forensic tracing.

**V27.7 Focus-Storm Hardening (COMPLETED)**:
1. Eliminated DispatchMessage stalls during rapid focus flapping by making `OutputDebugStringA` asynchronous in `LogWorker`.
2. Reduced `s_focusMutex` lock contention by collecting focus transition state and executing heavy logging / telemetry emissions outside the critical section in `IsTargetActive()`.

**V27.8 Input Stabilization & Concurrency Safety (COMPLETED)**:
1. Target-Bound Input: Validate target identity and release ownership before synthetic dispatch.
2. Hook-Owner Dispatch: Timer and BHOP execution deferred to owner thread (`BhopInjectionQueue`, `RoutedInputQueue`).
3. Seqlock Writer Mutex: Protected `rcfg::Apply()` and mutable config access with `s_writerMutex`.
4. Bounded Logging Queue: `s_logQueue` capped at 4096 messages in `debug_logger.cpp`.
5. Automated Test & Safe Gate Infrastructure: 18 CTest integration/unit tests (`make check`), PE import verification, private-desktop isolation guard.

**Next**:
1. Monitor production observability (focus/profile events) during live matchmaking.

---

**Luật lệ quan trọng cho AI mới**:
- KHÔNG tạo report mới dài dòng. Mọi tri thức mới phải được merge thẳng vào `MARCO_KNOWLEDGE_BASE.md`.
- Các report cũ nằm ở `docs/archive/`, KHÔNG đọc lại chúng nếu không thực sự cần dò lịch sử sâu.
