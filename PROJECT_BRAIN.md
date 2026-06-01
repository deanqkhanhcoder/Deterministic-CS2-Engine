# Marco Project Brain

Date: 2026-06-01
Version: V27.6
Mode: Maturity & Observability Slimdown

Tài liệu này là lõi trung tâm. 
Bất kỳ AI nào làm việc với dự án này CHỈ CẦN đọc 2 file sau:

1. `PROJECT_BRAIN.md` (Tài liệu này - Tóm tắt định hướng)
2. `docs/MARCO_KNOWLEDGE_BASE.md` (Toàn bộ kiến trúc và kiến thức kỹ thuật duy nhất)

---

## Current Architecture

* **Input Pipeline**: Windows LL Hooks -> Physical State -> Semantic Route -> Game State.
* **Thread Model**: Lock-Free UI (Seqlock), OS Hook Thread, Background Timer Spinloop, Async BHOP Worker, Background Focus Resolver.
* **Core Loops**: Counter-Strafe sử dụng 2D physics LUT để tính toán delay tự phanh khẩn cấp.

## Current Status

V27.6

Source:
CERTIFIED

Build:
PASS

Runtime:
PENDING VALIDATION

## Current Bugs

* **V27.5 update**: BUG-002/003/004/005/006/007/009/010/011 have source fixes in the V27.5 hardening pass. Keep them as RUNTIME NOT YET VERIFIED until real gameplay evidence exists.

* **BUG-004 Resolver Starvation (CRITICAL)**: Queue resolve window bị giành giật tín hiệu (Condition Variable) gây tịt ngòi nhận diện game.
* **BUG-005 Emergency Unhook Blackhole (HIGH)**: Watchdog cứu nguy nhưng UI nuốt mất message.
* **BUG-002 LUT Data Race (MEDIUM)**: Đổi profile đụng độ hook thread đọc bảng LUT Counter-Strafe.

*(Xem danh sách chi tiết tại `docs/MARCO_KNOWLEDGE_BASE.md` phần BUG REGISTRY).*

## Roadmap

**V27.6 Maturity & Observability Slimdown (COMPLETED)**:
1. Purged Watchdog Architecture (heartbeats, fail-safe triggers, UI watchdog).
2. Decoupled ForensicRingBuffer from MARCO_ENABLE_FORENSIC, keeping FOCUS/PROFILE events in production.
3. Cleaned up obsolete files and dead code (e.g., `autofire_controller.cpp`, F8 tracking, `WM_TIMER_EXPIRED`).
4. Retained WARN, ERROR, FATAL logging in Release builds.

**Next**:
1. Real gameplay validation for V27.6.
2. Monitor production observability (focus/profile events).

---

**Luật lệ quan trọng cho AI mới**:
- KHÔNG tạo report mới dài dòng. Mọi tri thức mới phải được merge thẳng vào `MARCO_KNOWLEDGE_BASE.md`.
- Các report cũ nằm ở `docs/archive/`, KHÔNG đọc lại chúng nếu không thực sự cần dò lịch sử sâu.
