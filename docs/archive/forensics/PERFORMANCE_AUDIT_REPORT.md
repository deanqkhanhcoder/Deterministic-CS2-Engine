# BÁO CÁO KIỂM TOÁN HIỆU NĂNG (PERFORMANCE AUDIT)

## 1. MỤC ĐÍCH
Rà soát toàn bộ source code của MARCO Engine để tìm ra các nút thắt cổ chai về hiệu năng (bottlenecks) liên quan đến:
- Sự tranh chấp khóa (Mutex contention).
- Đồng bộ hóa nguyên thủy (Atomics) bị lạm dụng.
- Đánh thức luồng không cần thiết (Unnecessary wakeups).
- Cấp phát bộ nhớ không hợp lý (Allocations).
- Chi phí ghi log (Logging overhead).

---

## 2. CÁC VẤN ĐỀ ĐƯỢC PHÁT HIỆN

### [HIGH] 1. Tranh chấp Mutex gây lag Hook (Hook Thread Contention)
- **Vị trí**: `src/core/state_engine.cpp` (hàm `TakeSnapshot` dòng 417), hàm `PublishEngineState` (dòng 88).
- **Mô tả**: Giao diện (UI) gọi hàm `TakeSnapshot` ở tốc độ 60Hz (mỗi 16ms) để vẽ giao diện. Hàm này sử dụng `std::lock_guard<std::mutex> lock(s_stateMutex)` để copy toàn bộ trạng thái. Điều đáng nói là engine đã duy trì sẵn một cấu trúc Ring-Buffer/SeqLock lock-free (`s_pubBuffer`, `s_pubSeq`) tại `PublishEngineState`, nhưng `TakeSnapshot` lại không sử dụng nó mà đi khóa `s_stateMutex`.
- **Hệ quả**: Luồng nhập liệu (Hook thread) liên tục bị block bởi luồng UI vẽ hình, gây ra độ trễ (latency/jitter) vô cớ cho các thao tác gõ phím / chuột.
- **Đánh giá**: **HIGH**

### [HIGH] 2. Ngủ (Sleep) trong khi đang giữ Mutex
- **Vị trí**: `src/core/bhop.cpp` (hàm `ThreadFunc` dòng 294-297).
- **Mô tả**: Trong chế độ Bhop Legit, luồng Bhop Worker lấy khóa `std::lock_guard<std::mutex> lock(s_bhopMutex)`, sau đó gọi `Sleep(randSleep)` (lên tới 17ms) rồi gọi `SendInput`.
- **Hệ quả**: Trong suốt thời gian luồng này đang "ngủ", bất kỳ ai cố gắng gọi `bhop::OnSpaceDown()`, `bhop::OnSpaceUp()` (được gọi từ Low-Level Keyboard Hook), hoặc đổi chế độ Bhop, đều bị block chờ tới 17ms. Điều này làm nghẽn toàn bộ Hook của hệ điều hành, có thể gây kẹt phím hoặc làm Windows hủy Hook.
- **Đánh giá**: **HIGH**

### [HIGH] 3. Nút thắt cổ chai I/O khi ghi Log
- **Vị trí**: `src/core/debug_logger.cpp` (hàm `Log` dòng 19-39).
- **Mô tả**: Dù `debug_logger` chỉ bật ở chế độ Debug, mỗi lần gọi log (ở tốc độ rất cao từ timing/hook), engine đều khóa `s_logMutex`, gọi `fopen()` mở file, `fprintf()`, và `fclose()` đóng file ngay lập tức.
- **Hệ quả**: Hành vi I/O ổ cứng đồng bộ này sẽ chặn luồng gọi nó hàng chục mili-giây, phá vỡ hoàn toàn độ chính xác của high-resolution timer.
- **Đánh giá**: **HIGH**

### [MEDIUM] 4. Đánh thức luồng thừa thãi (Busy/Sleep Loop)
- **Vị trí**: `src/core/target_platform.cpp` (hàm `ProcessScannerWorker` dòng 297-334).
- **Mô tả**: Scanner dùng để duyệt tiến trình game đang chạy. Thay vì sử dụng `std::condition_variable` để có thể sleep sâu và đánh thức khi tắt chương trình, luồng này sử dụng vòng lặp `for (int i = 0; i < 20; ++i) { Sleep(100); }`.
- **Hệ quả**: Luồng nền liên tục thức dậy 10 lần một giây (mỗi 100ms) chỉ để kiểm tra `s_resolverRunning`. Điều này lãng phí chu kỳ CPU và tài nguyên tiết kiệm điện năng của hệ điều hành.
- **Đánh giá**: **MEDIUM**

### [LOW] 5. Lạm dụng Atomic Sequential Consistency
- **Vị trí**: Rải rác, ví dụ `src/core/state_reconciliation.cpp` (dòng 23) và `src/core/target_platform.cpp` (dòng 58-72).
- **Mô tả**: Đa phần hệ thống dùng `std::memory_order_relaxed`, tuy nhiên vẫn còn vài chỗ có rào chắn bộ nhớ (`std::atomic_thread_fence`) hoặc atomic seq-cst có thể thiết kế lại mượt mà hơn. Dù vậy, không nằm trên critical path nên ảnh hưởng không đáng kể.
- **Đánh giá**: **LOW**

---

## 3. TỔNG KẾT
Cấu trúc bất đồng bộ của ứng dụng khá tốt, nhưng việc quản lý Mutex (`s_stateMutex`, `s_bhopMutex`) đang phá hỏng tính chất thời gian thực (real-time) của Hook và Timer. Ưu tiên hàng đầu là gỡ bỏ `Sleep` khi đang giữ khóa trong `bhop.cpp` và tận dụng Lock-free Buffer đã có sẵn cho `TakeSnapshot` trong `state_engine.cpp`.
