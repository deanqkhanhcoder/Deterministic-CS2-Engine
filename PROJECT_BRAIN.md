# Marco Project Brain

Date: 2026-05-30
Version: V27.4
Mode: Final Consolidation

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

V27.5 RC1

Source:
CERTIFIED

Build:
PASS

Runtime:
PENDING 3-7 DAY VALIDATION

## Current Bugs

* **V27.5 update**: BUG-002/003/004/005/006/007/009/010/011 have source fixes in the V27.5 hardening pass. Keep them as RUNTIME NOT YET VERIFIED until real gameplay evidence exists.

* **BUG-004 Resolver Starvation (CRITICAL)**: Queue resolve window bị giành giật tín hiệu (Condition Variable) gây tịt ngòi nhận diện game.
* **BUG-005 Emergency Unhook Blackhole (HIGH)**: Watchdog cứu nguy nhưng UI nuốt mất message.
* **BUG-002 LUT Data Race (MEDIUM)**: Đổi profile đụng độ hook thread đọc bảng LUT Counter-Strafe.

*(Xem danh sách chi tiết tại `docs/MARCO_KNOWLEDGE_BASE.md` phần BUG REGISTRY).*

## Roadmap

**V27.5 Hardening Execution (Current)**:
1. Synchronous forensic flush was removed from hook/input focus paths.
2. Forensic ring flush now snapshots under lock and writes after unlock.
3. Watchdog health checks read published lock-free state instead of waiting on `s_stateMutex`.
4. Movement LUT reload publishes immutable snapshots atomically.
5. Resolver wakeups use `notify_all()` to avoid scanner/resolver signal theft.
6. Emergency unhook and ETW UI error paths are routed through UI/message-thread handling.
7. Known rotten unit tests were moved to current `movement::` APIs.

**Next**:
1. Real gameplay validation for V27.5.
2. Observability Slimdown after stability evidence: Release minimal telemetry; Debug/Profile full forensic.
3. Architecture freeze only after runtime evidence.

**V27.5 Mùa Dọn Dẹp (Sắp tới)**:
1. Sửa toàn bộ BUG từ 002 đến 008.
2. Xóa bỏ File Test cũ nát (`test_physics`, `test_hybrid`).
3. Dọn code thừa `autofire_controller`.
4. Triển khai "Observability Slimdown": loại bỏ Forensic Spams trên bản Release.

---

**Luật lệ quan trọng cho AI mới**:
- KHÔNG tạo report mới dài dòng. Mọi tri thức mới phải được merge thẳng vào `MARCO_KNOWLEDGE_BASE.md`.
- Các report cũ nằm ở `docs/archive/`, KHÔNG đọc lại chúng nếu không thực sự cần dò lịch sử sâu.
