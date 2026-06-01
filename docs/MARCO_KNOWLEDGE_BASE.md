# MARCO KNOWLEDGE BASE (SINGLE SOURCE OF TRUTH)

*Date: 2026-06-01 | Engine Version: V27.6*

## 1. Project Overview
Marco Engine là một Windows C++20 input-processing engine cho game FPS (chủ yếu là CS2 và Roblox). Nó bắt phím cấp thấp (low-level hook), theo dõi trạng thái vật lý và logic độc lập, thực thi các macro (Counter-Strafe, BHOP), và inject phím ảo qua `SendInput`.

## 2. Architecture Overview
Kiến trúc V27.4 loại bỏ các thiết kế subtick/delay cũ, ưu tiên deterministic ordering.
Dữ liệu di chuyển: Hardware -> Hook (`input_capture`) -> Định tuyến Semantic (`input_router`) -> Cập nhật trạng thái (`state_engine`) -> Xử lý Tính năng (`counterstrafe_controller`, `bhop`) -> Bơm sự kiện (`SendInput`) -> UI / Telemetry.

## 3. Thread Model
Kiến trúc cực kỳ khắt khe với đa luồng để tránh block OS hook:
- **UI Thread**: Quản lý Dashboard và config snapshot (Lock-Free Seqlock).
- **Hook Thread**: Chạy `KeyboardProc`/`MouseProc`. Yêu cầu tốc độ nanosecond, tuyệt đối không block.
- **Focus/Scanner/Resolver Threads**: Cập nhật cửa sổ foreground và kiểm tra active profile.
- **Timer Thread**: P-Core pinned. Vòng lặp Spinloop QPC (`_mm_pause`) cho độ trễ < 1ms. Gọi `engine::OnTimerExpired`.
- **BHOP Thread**: Worker độc lập ngủ trên Condition Variable, thức dậy tính toán delay BHOP.

## 4. Input Pipeline
Sử dụng `WH_KEYBOARD_LL` và `WH_MOUSE_LL`. Đầu vào luôn cập nhật trạng thái *Vật Lý* (Physical Truth) bất kể focus. Phân luồng *Semantic* (Logic) chỉ kích hoạt khi Target đang Active. Khái niệm "Swallow" (Nuốt) ngăn game nhận phím thật nếu macro cần xử lý thay (như Space cho BHOP).

## 5. Counter-Strafe Design
Khi phát hiện nhả phím di chuyển, tính toán True Velocity thông qua bảng LUT (Look-up Table) 2D. Bơm ngay lập tức phím ngược lại (Counter key) và đặt một Timer lên `TimerThread` để nhả phím counter, giúp dừng nhân vật nhanh nhất.

## 6. BHOP Design
Khác với AHK cũ, C++ BHOP dùng worker thread riêng biệt. SpaceDown báo hiệu CV. Worker chạy vòng lặp tính thời gian nhảy dựa trên Gaussian Random (Humanized) hoặc tĩnh, gọi `SendInput` để tạo Jump Burst. Khi SpaceUp hoặc mất focus, worker bị khoá lại chờ nhịp mới.

## 7. Runtime Config System
Sử dụng mô hình Seqlock (Write-Release / Read-Acquire) để cấu hình có thể cập nhật từ UI Thread mà không cần khóa Mutex cản trở Hook Thread hay Timer Thread.

## 8. Telemetry / Forensic Architecture
Hệ thống sử dụng Ring Buffer lock-free. Sự kiện vòng đời (Focus, Profile) và lỗi logic (`BHOP_STALL`, `COUNTERSTRAFE_CONFLICT`) được đẩy liên tục vào `ForensicRingBuffer`. Kể từ V27.6, Watchdog đã bị xóa bỏ hoàn toàn để giảm footprint, và Forensic được decouple để chạy cả trên Release build nhằm theo dõi Focus Event.

## 9. Directory Structure
- `src/core/`, `src/ui/`: Logic lõi và Giao diện.
- `include/core/`, `include/ui/`: Headers.
- `runtime/logs/`, `runtime/crash/`, `runtime/bin/`: Mọi output runtime. Tuyệt đối cấm sinh log ở root project.
- `docs/archive/`: Thư mục lưu mọi lịch sử điều tra của V27.4 trở về trước.

## 10. Release Process
Cấm claim `PASS`, `STABLE` nếu không có bằng chứng từ real gameplay. Bản release phải trải qua 3-7 ngày Matchmaking (MM/DM) test. Observability sẽ được hạ xuống mức Production (chỉ lấy ERROR/WARN/CRASH) trong chế độ Release.

## 11. V27.4 Focus Desync Incident
**Triệu chứng**: Counter-strafe ngẫu nhiên chết cứng, đổi profile hoặc alt-tab lại thì hồi phục.
**Hành vi**: Cửa sổ game đang ở foreground nhưng engine cho rằng nó ở background.

## 12. Root Cause (Focus Desync)
Hệ thống lấy thông tin Focus bị "Stale Cache" (lạc hậu) khi event queue quá tải, khiến hàm `IsTargetActive()` đánh giá `routeSemantic = 0`. Tuy nhiên input vật lý vẫn chạy, đẩy logic và physical phân kỳ trầm trọng.

## 13. Fix Applied
Sửa đổi `IsTargetActive()` trong `input_capture.cpp`. Luôn tái đánh giá (Resolve) lại PID liên tục thông qua Async Resolver nếu Foreground thay đổi, và Reconcile (dọn sạch phím kẹt, build lại trạng thái bằng `GetAsyncKeyState`) khi regain focus.

## 14. Validation Status
Đã chứng minh bằng Code Audit (FINAL_INDEPENDENT_REVIEW). Đang trong giai đoạn Pending Real Gameplay Validation (3-7 ngày). 

## 15. KNOWN BUG REGISTRY

* **BUG-001 Focus Desync**
  - *Status*: Fixed in V27.4. Đang lấy validation.
* **BUG-002 Movement LUT Reload Race**
  - *Status*: Deferred V27.5
  - *Detail*: Đổi profile gọi `InitLUT()` ghi đè mảng không có lock, Hook thread đọc đồng thời gây tearing. Không gây crash nhưng làm trượt mili-giây phanh.
* **BUG-003 Space Swallow Leak**
  - *Status*: Deferred V27.5
  - *Detail*: Lấy lại focus lúc đè Space, trigger `OnSpaceDown()` nhưng quên set cờ `s_spaceSwallowed = true`.
* **BUG-004 Resolver Starvation**
  - *Status*: Investigate (CRITICAL)
  - *Detail*: `ResolverWorker` và `ScannerWorker` dùng chung Condition Variable, `notify_one()` đánh thức nhầm luồng làm kẹt Queue.
* **BUG-005 Emergency Unhook Blackhole**
  - *Status*: Investigate (HIGH)
  - *Detail*: Thông điệp `WM_EMERGENCY_UNHOOK` không được handle trong `ui_main.cpp`, làm hỏng hệ thống Watchdog.
* **BUG-006 Cross-Thread UI Blocking**
  - *Status*: Investigate (MEDIUM)
  - *Detail*: Background ETW Thread gọi `MessageBoxW` lên main thread window.
* **BUG-007 Test Infrastructure Rot**
  - *Status*: Investigate (HIGH DEBT)
  - *Detail*: `tests/` và `tools/` gọi sai namespace và API, không thể compile.
* **BUG-008 Duplicated Watchdog**
  - *Status*: Investigate
  - *Detail*: UI và Engine xài 2 watchdog riêng, chồng chéo chức năng.
* **BUG-009 Hook Thread I/O Spinlock Starvation**
  - *Status*: Investigate (CRITICAL)
  - *Detail*: `ForensicRingBuffer::Push` spin-waits trên lock trong khi background thread hold lock để ghi file (`fprintf`). Gây treo Hook Thread khi có event dị thường trùng với auto-flush.
* **BUG-010 Watchdog Self-Deadlock**
  - *Status*: Investigate (HIGH)
  - *Detail*: Watchdog cố acquire `s_stateMutex` để check health. Nếu luồng chính treo khi đang hold `s_stateMutex`, Watchdog cũng kẹt theo, vô hiệu hóa Emergency Flush.
* **BUG-011 Synchronous I/O in Hook**
  - *Status*: Investigate (HIGH)
  - *Detail*: `IsTargetActive()` gọi `FlushForensicLog()` trực tiếp bên trong `KeyboardProc` khi mất focus, thực thi Disk I/O trên luồng OS Hook.

## 16. V27.5 Bug Registry Update

Status vocabulary for this pass:

- IMPLEMENTED: source patch exists.
- TESTED (build): `make debug`, `make profile`, and `make release` are required validation.
- RUNTIME NOT YET VERIFIED: no long gameplay validation has been collected for this pass.

| Bug ID | V27.5 status | Source-level fix |
| --- | --- | --- |
| BUG-002 Movement LUT Reload Race | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `movement_reconstruction.cpp` now builds a new LUT snapshot off-thread/local to `InitLUT()` and publishes it through `std::atomic<std::shared_ptr<const MovementLutSnapshot>>`; readers load immutable snapshots. |
| BUG-003 Space Swallow Leak | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `input_capture.cpp` now sets `s_spaceSwallowed = true` when focus regain sees physical Space down and BHOP should own Space. |
| BUG-004 Resolver Starvation | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `ResolveTargetAsync()` now uses `s_resolverCv.notify_all()` so the resolver cannot permanently lose a signal to the scanner waiter. |
| BUG-005 Emergency Unhook Blackhole | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `ui_main.cpp` now forwards `WM_EMERGENCY_UNHOOK` to the hidden message window; the existing hidden window handler performs `capture::Uninstall()`. |
| BUG-006 Cross-Thread UI Blocking | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `ui_analysis.cpp` no longer calls `MessageBoxW` from the ETW worker thread; it posts `WM_ANALYSIS_ETW_START_FAILED` and `ui_main.cpp` shows the dialog on the UI thread. |
| BUG-007 Test Infrastructure Rot | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `tests/unit/test_physics.cpp` and `tests/unit/test_hybrid.cpp` now use the current `movement::` API instead of the removed `physics::SimulateTrueStopDuration()` path. |
| BUG-009 Hook I/O Spinlock Starvation | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | `ForensicRingBuffer::Push()` no longer spins; `FlushToFile()` snapshots under the ring lock, unlocks, then performs file I/O. |
| BUG-010 Watchdog Self-Deadlock | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | Watchdog health checks read the published seqlock snapshot instead of locking `s_stateMutex`; emergency recovery uses `try_to_lock` so unhook signaling can proceed if state mutex is stuck. |
| BUG-011 Synchronous I/O In Hook | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | Focus-loss/focus-gain paths now call `telemetry::RequestForensicFlush()` instead of `FlushForensicLog()`; the telemetry thread performs disk flush asynchronously. |

| BUG-011 Synchronous I/O In Hook | IMPLEMENTED, TESTED (build), RUNTIME NOT YET VERIFIED | Focus-loss/focus-gain paths now call `telemetry::RequestForensicFlush()` instead of `FlushForensicLog()`; the telemetry thread performs disk flush asynchronously. |

**V27.6 Maturity Update**:
- Removed Watchdog Architecture (fixes BUG-008, BUG-010). UI Watchdog also removed.
- Cleaned up `autofire_controller` (dead code), F8 tracking, and `WM_TIMER_EXPIRED`.
- Decoupled `ForensicRingBuffer` from `MARCO_ENABLE_FORENSIC` for production observability, preserving WARN/ERROR/FATAL logging in Release builds.

## 17. Technical Debt

- Macro cờ build (`MARCO_ENABLE_LOGGING` v.v) chưa bị xoá ở `build_config.h` dù không có người dùng (Đã dọn dẹp ở V27.6).
- Constant thông điệp Windows thừa thãi (`WM_TIMER_EXPIRED`) (Đã xoá ở V27.6).
- F8 tracking vô hình còn tồn tại trong `input_capture.cpp` (Đã xoá ở V27.6).
- Các đoạn dead code dư thừa do xoá Subtick Autofire (`autofire_controller.cpp`) (Đã dọn ở V27.6).

## 18. Deferred After V27.6
- Real gameplay validation for BUG-002/003/004/005/006/007/009/010/011 remains required.
- Viết lại toàn bộ `tests/` để hoạt động với API V27.6 mới.

## 19. Risk Register
- Nếu BUG-004 (Resolver Starvation) xảy ra, target window không được nhận diện, toàn bộ macro tắt ngóm. Cực kỳ dễ xảy ra nếu người dùng spam Alt-tab.
- Nếu BUG-005 xảy ra kèm với Thread Stall, Engine sẽ treo cứng không thể tự sát (Self-Terminate), buộc người dùng phải Task Manager.

## 20. Development Rules
1. KHÔNG thêm path vào ổ cứng ngoài `runtime/`. Tất cả đi qua `workspace.cpp`.
2. Không tự ý log mọi frame. Ưu tiên Forensic Trap (Anomaly Logging).
3. Đổi logic State phải tuân thủ phân cấp: `s_focusMutex` -> `s_stateMutex` -> Spinlocks.
4. Mọi bằng chứng BUG FIXED đều phải dựa trên source code VÀ runtime/log. Không đoán mò.

## 21. Debug Workflow
1. Khi có report bug, yêu cầu file `.zip` toàn bộ `runtime/logs/` và `runtime/crash/`.
2. Mở file `marco_YYYY-MM-DD.log`. Tìm kiếm các trap: `FOCUS_LOST`, `BHOP_STALL`, `COUNTERSTRAFE_CONFLICT`, `LOGICAL_PHYSICAL_DIVERGENCE`.
3. Kiểm tra xem người dùng có đổi config profile trước lúc bị không (`PROFILE_CHANGED`).
4. Theo dõi dấu vết `KeyboardProc` trong `runtime/logs/marco_debug.log`. Nếu có event ` route=0`, là do Focus Desync. Nếu không có Timer Event, là do Timer Thread treo.
