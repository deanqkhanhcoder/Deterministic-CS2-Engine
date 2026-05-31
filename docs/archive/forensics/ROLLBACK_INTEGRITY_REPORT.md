# Báo Cáo Tính Toàn Vẹn Rollback V27

## Trạng Thái Các Thành Phần Kiểm Tra
Dựa trên kết quả rà soát codebase (`grep_search` và `view_file` trong thư mục `src/` và `include/`):

1. **FireState đã biến mất hoàn toàn chưa?**
   - **Xác nhận**: Đã biến mất hoàn toàn. Không còn bất kỳ tham chiếu nào đến struct/enum `FireState` trong mã nguồn chính.

2. **delayed fire đã biến mất hoàn toàn chưa?**
   - **Xác nhận**: Đã biến mất hoàn toàn. Mọi logic liên quan đến việc làm trễ hành động bắn (delay fire) đều đã bị loại bỏ sạch sẽ.

3. **subtick fire đã biến mất hoàn toàn chưa?**
   - **Xác nhận**: Đã biến mất hoàn toàn. Hệ thống không còn lưu giữ logic nào về subtick tracking hay subtick quantization cho việc bắn.

4. **ScheduleTimerUs còn được dùng cho firing không?**
   - **Xác nhận**: Không còn dùng cho firing. Hàm `ScheduleTimerUs` hiện tại chỉ được sử dụng để hẹn giờ nhả (release) các phím di chuyển (movement keys) phục vụ cơ chế tự động counter-strafe (trong `autofire_controller.cpp` và `counterstrafe_controller.cpp`).

5. **timer_lifecycle còn giữ logic firing không?**
   - **Xác nhận**: Không. File `timer_lifecycle.cpp` (đặc biệt là hàm `OnTimerExpired`) hiện tại chỉ chứa logic quản lý nhả phím vật lý, giải quyết xung đột trục (AxisState), và khôi phục trạng thái phím đối diện. Không có bất kỳ logic firing nào còn sót lại ở đây.

---

## 🚨 Phát Hiện Lỗi (Bug Report)
Trong quá trình audit việc sử dụng `ScheduleTimerUs` và `timer_lifecycle`, đã phát hiện ra một lỗi cực kỳ nghiêm trọng xuất hiện ở logic auto-fire mới:

- **File**: `src/core/autofire_controller.cpp`
- **Function**: `OnLButtonDown()`
- **Mức độ nghiêm trọng**: **CRITICAL** (Làm kẹt cứng phím di chuyển của người chơi không thể nhả ra).
- **Root Cause**:
  Tại dòng 35 trong file `autofire_controller.cpp`, hệ thống thực hiện gọi `timing::ScheduleTimerUs(counterKey, brakeUs);` để lên lịch nhả phím phanh (counterKey) sau khi bắn. Tuy nhiên, lập trình viên đã **quên lưu lại timer ID trả về** vào biến trạng thái `s_state.expectedTimerId[ki_c]`.
  
  Bởi vì giá trị `expectedTimerId` trong state không được cập nhật (vẫn giữ là 0 hoặc ID cũ), khi timer hết hạn và gọi hàm `OnTimerExpired(Key k, uint64_t expectedTimerId)` bên trong `timer_lifecycle.cpp`, điều kiện kiểm tra độ trễ (stale callback) sau đây sẽ bị vấp:
  ```cpp
  if (expectedTimerId != 0 && s_state.expectedTimerId[ki_k] != expectedTimerId) {
      // Bị lọt vào nhánh này do s_state.expectedTimerId[ki_k] chưa được gán bằng expectedTimerId mới tạo
      return; 
  }
  ```
  Hậu quả là hàm nhả phím bị đánh giá nhầm là "stale" và trả về sớm (return). Nút di chuyển (counter-strafe) đã bị inject xuống (true) nhưng **không bao giờ được gửi lệnh nhả ra (false)**. Trạng thái logical của phím sẽ bị kẹt vĩnh viễn sau mỗi lần bấm chuột trái nếu thỏa mãn điều kiện phanh.
