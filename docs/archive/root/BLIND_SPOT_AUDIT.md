# MARCO V27.4 BLIND SPOT AUDIT

Tài liệu này phơi bày những góc khuất chưa từng được ánh sáng của các đợt Bug Hunt V27.4 chiếu tới. Các khu vực này chứa những rủi ro ngầm định có thể phá vỡ tính ổn định của hệ thống.

## 1. PREVIOUSLY UNTOUCHED AREAS

1.  **Target Platform Queue (Resolver & Scanner)**: Bị bỏ quên do tưởng chừng như là một tiến trình nền vô hại.
2.  **Watchdog Fallback Path**: Cơ chế cứu hộ khẩn cấp (Emergency Flush) chưa từng được trigger thử trong thực tế.
3.  **Test Infrastructure (`tests/` & `tools/`)**: Các bài test được chạy tự động nhưng không ai kiểm tra mã nguồn của chính các bài test đó.
4.  **UI Diagnostics / Analysis**: Vùng mã liên quan đến profiling và ETW diagnostics.

## 2. NEW RISKS FOUND (CRITICAL & HIGH)

### 2.1. Target Platform Resolver Starvation (HIGH)
*   **File**: `src/core/target_platform.cpp`
*   **Logic Flaw**: Cả `ResolverWorker` và `ProcessScannerWorker` đều gọi `wait` và `wait_for` trên **cùng một Condition Variable** (`s_resolverCv`). Khi `ResolveTargetAsync` gọi `s_resolverCv.notify_one()`, hệ điều hành có thể đánh thức `ScannerWorker` thay vì `ResolverWorker`.
*   **Execution Path**: 
    1. Input tới -> Gọi `ResolveTargetAsync` -> Thêm vào `s_resolverQueue` -> Gọi `notify_one()`.
    2. `ScannerWorker` tỉnh dậy, kiểm tra predicate `!s_resolverRunning`. Vì engine đang chạy (false), nó tiếp tục đi ngủ.
    3. Mũi tên (Signal) bị nuốt. `ResolverWorker` vĩnh viễn không tỉnh dậy cho tới khi có một Input khác may mắn đánh thức đúng nó.
*   **Hậu quả**: Hàng đợi phân giải Target bị kẹt. Game có thể không nhận diện được `CS2` hoặc `Roblox` kịp thời.

### 2.2. Watchdog Emergency Unhook Ignored (HIGH)
*   **File**: `src/core/state_engine.cpp` (TriggerEmergencyFlush) & `src/ui/ui_main.cpp` (WndProc)
*   **Logic Flaw**: Khi các thread quan trọng bị kẹt, Engine Watchdog gọi `PostMessage(s_hwnd, WM_EMERGENCY_UNHOOK, 0, 0)` để gỡ Hook an toàn từ UI thread.
*   **Execution Path**: Mở `ui_main.cpp`, trong hàm `WndProc` (nơi nhận mọi message), **hoàn toàn KHÔNG CÓ `case WM_EMERGENCY_UNHOOK:`**. Message này rơi vào hư vô (blackhole) thông qua `DefWindowProcW`.
*   **Hậu quả**: Chức năng cứu hộ cốt lõi của Watchdog hoàn toàn vô dụng. Nếu Hook bị kẹt, process sẽ treo vĩnh viễn không thể phục hồi.

### 2.3. Cross-Thread UI Blocking (MEDIUM)
*   **File**: `src/ui/ui_analysis.cpp` (IDB_ETW_START)
*   **Logic Flaw**: Nhấn nút Start ETW sẽ tạo một `std::thread` chạy ngầm. Nếu ETW start lỗi, thread này gọi `MessageBoxW(hwnd, ...)` với `hwnd` của main thread.
*   **Hậu quả**: Gọi Win32 MessageBox từ một background thread gán cho một owner window thuộc thread khác là hành vi vi phạm Thread Affinity, có thể gây Deadlock toàn bộ UI queue nếu người dùng thao tác vào cửa sổ chính lúc đó.

## 3. TECHNICAL DEBT

1.  **Test Infrastructure Rot (Thối rữa mã kiểm thử)**
    *   *Bằng chứng*: `tests/unit/test_physics.cpp`, `test_hybrid.cpp` và `tools/forensics/deterministic_simulator.cpp` vẫn sử dụng `namespace physics` và hàm `SimulateTrueStopDuration`. Trong V27.4, chúng đã được đổi thành `namespace movement` và `LookupStopDur2D`.
    *   *Kết luận*: Các Unit Test và Tool này **không thể compile** được với codebase hiện tại.
2.  **Duplicated Watchdog**
    *   *Bằng chứng*: Tồn tại một Watchdog trong `state_engine.cpp` (Engine Health) và một Watchdog riêng rẽ trong `ui_diagnostics.cpp` (UI Health). Việc phân mảnh này làm tiêu tốn thêm thread và gây khó khăn trong việc thiết lập chính sách Fail-Safe đồng nhất.

## 4. SUSPICIOUS BUT UNPROVEN

*   **Fake Stress Tests (Ảo tưởng an toàn)**:
    *   Trong `tests/test_stress.cpp`, tác giả cố gắng mock `GetAsyncKeyState` bằng cách export symbol `__imp_MockGetAsyncKeyState`. Tuy nhiên, với trình biên dịch hiện đại (MSVC/MinGW), việc chặn IAT (Import Address Table) theo cách thủ công này thường thất bại thảm hại mà không báo lỗi.
    *   *Nghi vấn*: Hàm `engine::RebuildState()` vẫn đang đọc trực tiếp từ phím vật lý thực của người chạy test. Nếu bàn phím không được bấm, test luôn "PASS" ảo. Điều này lý giải tại sao các Report trước đây ghi nhận "STRESS_BHOP_REPORT: 10000 iterations, 0 fails" — thực chất bài test không sinh ra tải thực.

## 5. THINGS SURPRISINGLY WELL DESIGNED

Những vùng code này được thiết kế xuất sắc và đạt tiêu chuẩn chất lượng rất cao:

1.  **Seqlock Publication (`s_pubState`)**: Áp dụng chuẩn xác thuật toán Lock-Free Seqlock kết hợp với Memory Barriers (`std::atomic_thread_fence`) để chuyển dữ liệu từ luồng ưu tiên cao (Hook/Timer) sang luồng ưu tiên thấp (UI) mà không gây bất kỳ độ trễ (Lock Contention) nào.
2.  **Adaptive Spinloop (`timing.cpp`)**: Cơ chế Jitter Compensation tính toán độ lệch (oversleep) kết hợp `_mm_pause` và `SetWaitableTimer` được thiết kế cực kỳ khéo léo để bypass Windows OS Scheduler, đảm bảo Timer kích hoạt chính xác ở cấp độ Microsecond.

## 6. RECOMMENDED V27.5 CLEANUP TARGETS

1.  Đổi `s_resolverCv.notify_one()` thành `notify_all()` HOẶC tách `ProcessScannerWorker` ra dùng một CV riêng biệt.
2.  Bổ sung `case WM_EMERGENCY_UNHOOK:` vào `ui_main.cpp` gọi `capture::Uninstall()`.
3.  Cập nhật toàn bộ các file trong thư mục `tests/` và `tools/` để sử dụng đúng namespace và API mới (`movement::`).
4.  Bọc `MessageBoxW` trong `ui_analysis.cpp` vào `PostMessage` để đẩy việc hiển thị hộp thoại về lại Main UI Thread.
5.  Gộp chung 2 Watchdog lại thành một Service duy nhất để quản lý vòng đời Process.