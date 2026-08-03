# Báo cáo Audit Thread Model - Marco / Deterministic-CS2-Engine

## 1. Phân tích chi tiết các Threads

### 1.1 UI Thread / Hook Thread (Main Thread)
* **Nhiệm vụ:** Do đặc thù kiến trúc của Win32, UI Window (chứa giao diện người dùng) và Low-level Hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) được khởi tạo trên **cùng một luồng chính (Main Thread)**. Luồng này chạy vòng lặp tin nhắn `MsgWaitForMultipleObjectsEx` / `PeekMessageW`. Xử lý các sự kiện vẽ giao diện (UI) và gọi trực tiếp các hàm logic engine (`engine::HandleKeyDown`, `engine::HandleKeyUp`) ngay trong context của các callback Hook (`KeyboardProc`, `MouseProc`).
* **Mutex sử dụng:**
  - Lấy `s_focusMutex` (trong `input_capture.cpp`) để kiểm tra target window.
  - Lấy `s_stateMutex` (trong `state_engine.cpp`) để cập nhật trạng thái phím.
  - Lấy `s_resolverMutex` (thông qua `ResolveTargetAsync` trong `target_platform.cpp`).
* **Queue sử dụng:** Window Message Queue (hàng đợi tin nhắn của hệ điều hành).
* **Condition Variable sử dụng:** Trực tiếp không có std::condition_variable. Hoạt động dựa vào `MsgWaitForMultipleObjectsEx` để đợi tín hiệu event/message.

### 1.2 Worker Thread (Bhop Worker)
* **Nhiệm vụ:** Xử lý và quản lý vòng lặp trạng thái (state machine) của thao tác nhảy liên tục (Bhop / Scroll). Luồng này nằm ngủ chờ tín hiệu phím Space được nhấn, sau đó thực thi các tác vụ mô phỏng phím (inject input) với delay cực kỳ chính xác bằng việc kết hợp `NtDelayExecution` và kĩ thuật QPC Busy-spin.
* **Mutex sử dụng:** `s_mutex` (nằm tại `src/core/bhop.cpp`).
* **Queue sử dụng:** Không dùng cấu trúc Queue (hàng đợi). Giao tiếp tín hiệu thông qua biến atomic `s_spaceHeld` và state atomic.
* **Condition Variable sử dụng:** `s_cv` (kiểu `std::condition_variable` tại `bhop.cpp`).

### 1.3 Timer Thread
* **Nhiệm vụ:** Một luồng có độ ưu tiên cực cao (đã được pinned vào P-Core với cấu hình Pro Audio) phụ trách việc lên lịch các bộ đếm thời gian (timers) độ trễ mức độ microsecond cho Counter-Strafe. Luồng theo dõi các khoảng thời gian chờ (bằng Waitable Timer và Spin-loop ở chặng cuối) sau đó đẩy tin nhắn báo hết hạn (`WM_TIMER_EXPIRED`) về cho UI/Hook Thread.
* **Mutex sử dụng:** `s_spinlock` (kiểu `std::mutex` nhưng được sử dụng để khóa chớp nhoáng vòng lặp tìm slot tại `src/core/timing.cpp`).
* **Queue sử dụng:** Mảng tĩnh `s_slots[NUM_SLOTS]` (đóng vai trò như một Array-based Priority Queue).
* **Condition Variable sử dụng:** Không dùng thư viện chuẩn, mà thay vào đó sử dụng Event Win32 Object `s_cv_event` tương tác với `WaitForMultipleObjects` và `SetEvent`.

---

## 2. Kiểm tra an toàn Threading Model

### 2.1 Lock Ordering (Thứ tự khóa)
Kiến trúc sử dụng Mutex khá rõ ràng và triển khai phân tầng (DAG), không có trường hợp khóa ngược:
- `s_focusMutex` -> `s_resolverMutex` (trong quá trình quét Identity)
- `s_stateMutex` -> `s_spinlock` (khi set timer từ engine)
- Tất cả các locks trên khi cần in log đều có thể truy xuất `s_logMutex` dưới dạng "Leaf Lock" (khóa đáy).
**Kết luận:** Thứ tự cấp phát an toàn, không có chu trình vòng.

### 2.2 Possible Deadlock (Nguy cơ Deadlock)
Vì luồng Timer Thread có thói quen nhả (unlock) `s_spinlock` trước khi thực hiện `PostMessage` ngược về Main Thread, và Lock Ordering là thiết kế một chiều tuyến tính, **KHÔNG phát hiện Deadlock**. 

### 2.3 Shutdown Ordering
Quy trình gọi Shutdown tại `main.cpp` thực hiện tuần tự chặt chẽ:
1. Gỡ Hook (`capture::Uninstall()`) ngắt toàn bộ đầu vào.
2. Signal và Join `bhop::Shutdown()`: Thiết lập cờ `s_running=false` -> `cv` thức dậy -> Thread thoát ngay lập tức.
3. Signal và Join `timing::StopTimerThread()`: Trigger Event `s_cv_event` -> Thread kết thúc gọn gàng.
4. Signal và Join các luồng ngầm định (`target_platform`).
**Kết luận:** Shutdown Ordering được đánh giá là rất an toàn.

---

## 3. Bug / Lỗ hổng phát hiện (KHÔNG SỬA CODE)

### 🚨 Bug 1: Missed Wakeup Condition trong Bhop Worker
- **File:** `src/core/bhop.cpp`
- **Function:** `ToggleEnabled()` và `OnSuspendChanged()`
- **Root cause:** Trong hai hàm trên, các cấu hình Atomic (như `s_waitingForSpaceRepress` hoặc `rcfg`) bị thay đổi, sau đó hàm gọi `s_cv.notify_all()` mà **không nắm giữ/bọc trong Mutex `s_mutex`**. Nếu cùng lúc đó, Worker Thread đang ở trong `s_cv.wait`, đã kiểm tra xong `predicate` (kết quả là false) nhưng chưa kịp thực thi chuyển luồng vào trạng thái Sleep của OS, lệnh `notify_all()` sẽ được kích hoạt vô ích và biến mất. Kết quả là Worker Thread tiếp tục chìm vào Sleep vô thời hạn thay vì nhận ra cấu hình vừa bị thay đổi.
- **Mức độ nghiêm trọng:** Medium (Gây lỗi logic ngắt quãng khi kích hoạt tính năng qua Hotkey).

### 🚨 Bug 2: Possible Priority Inversion trong Target Resolver
- **File:** `src/core/target_platform.cpp`
- **Function:** `ResolveTargetAsync()` và `ResolverWorker()`
- **Root cause:** Mutex `s_resolverMutex` được chia sẻ giữa hai luồng có cấp độ ưu tiên cách biệt khổng lồ:
  1. Main Hook Thread (gọi `ResolveTargetAsync`) được cấp quyền cao nhất (Realtime/High via `PinHookThread`).
  2. `ResolverWorker` chạy ở chế độ Background (via `PinBackgroundThread`).
  Nếu HĐH đẩy luồng `ResolverWorker` ra khỏi CPU (preempt) ngay khi nó đang giữ khóa `s_resolverMutex` để đọc/ghi hàng đợi, Main Hook Thread - dù ở quyền ưu tiên cao - khi có thao tác gõ phím/chuột sẽ bị chặn đứng (block) hoàn toàn vì phải chờ luồng background nhả khóa. 
- **Mức độ nghiêm trọng:** High (Có nguy cơ tạo ra các vi đứt gãy - lag/freeze cục bộ đối với chuột và bàn phím của toàn hệ thống).
