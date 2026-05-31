# FINAL FORENSIC VERDICT (V27 POST-ROLLBACK)

Sau quá trình điều tra toàn diện mã nguồn thông qua 5 subagents chuyên biệt, đây là kết luận cuối cùng trả lời 5 câu hỏi cốt lõi:

### 1. V27 rollback có thực sự sạch không?
**Về mặt ý định:** Sạch. Các công nghệ gây tranh cãi và không ổn định như `FireState`, `delayed fire`, và `subtick fire` đã được tháo dỡ hoàn toàn khỏi codebase. `ScheduleTimerUs` không còn chứa bất kỳ logic liên quan đến bắn. 
**Về mặt thực thi:** Chưa sạch. Quá trình gỡ bỏ logic phức tạp đã vô tình để lại các lỗ hổng về quản lý state cơ bản.

### 2. Có subsystem nào bị rollback nhầm không?
**Có.** 
Hệ thống **Fail-safe & Gating** đã bị ảnh hưởng nghiêm trọng. Do phụ thuộc vào cờ `MARCO_ENABLE_WATCHDOG` (vốn bị vô hiệu hóa ở chế độ Release), các hàm quan trọng như `TriggerEmergencyFlush()` và `capture::PollTarget()` đã vô tình bị tắt trên bản build thực tế.
Hệ thống **Conflict Resolution (Snap Tap)** cũng bị vô hiệu hóa một phần (bypassed) khi người dùng thực hiện thao tác Alt-Tab (do `RebuildState` áp đặt `logical = true` khiến `ResolveAxis` bỏ qua kiểm tra).

### 3. Có bug nào nghiêm trọng hơn cả FireState không?
**Có. Thậm chí có đến 2 bug mang tính chất phá hoại trải nghiệm (Game-breaking / OS-breaking):**
1. **OS Hook Stall (CRITICAL):** Luồng Bhop Worker thực hiện lệnh `Sleep()` lên tới 17ms *trong khi đang giữ* `s_bhopMutex`. Vì Input Hook của Windows cũng cần lấy Mutex này khi nhấn Space, hệ quả là toàn bộ hệ thống Windows có thể bị lag/kẹt phím nếu người dùng spam Space.
2. **Permanent Movement Stuck (CRITICAL):** Trong `autofire_controller.cpp`, ID của timer không được lưu vào `s_state.expectedTimerId` khi gọi `ScheduleTimerUs`. Kết quả là khi timer nổ, hàm callback không thể xác thực ID và từ chối nhả phím di chuyển, dẫn đến việc nhân vật bị kẹt trôi đi vĩnh viễn.

### 4. Có nên freeze branch v27-stable-freeze ngay bây giờ không?
**TẤT NHIÊN LÀ KHÔNG.** 
Branch `v27-stable-freeze` hiện tại đạt được mục tiêu về kiến trúc (loại bỏ Fire Delay), nhưng lại thất bại hoàn toàn về tính ổn định ứng dụng (Stability). Đóng băng một branch có khả năng làm lag hệ điều hành và kẹt phím người chơi là một quyết định thảm họa.

### 5. Nếu phải sửa đúng 3 bug cuối cùng trước khi đóng băng branch, đó là bug nào?
Để đạt được trạng thái thực sự "Stable", 3 lỗi sau phải được khắc phục ngay lập tức:
1. **Sửa lỗi kẹt phím di chuyển (Movement Stuck):** Phải gán đúng `s_state.expectedTimerId = timerId;` khi thiết lập bộ đếm giờ ngắt counter-strafe.
2. **Sửa lỗi lag hệ điều hành (Hook Stall):** Thiết kế lại luồng Bhop Worker để tuyệt đối KHÔNG gọi `Sleep()` trong vùng tranh chấp (Critical Section) của `s_bhopMutex`.
3. **Sửa lỗi Alt-Tab phá Snap Tap:** Dọn dẹp lại hàm `RebuildState` sao cho nó chỉ đồng bộ trạng thái *physical*, và để hàm `ResolveAxis` tự nhiên quyết định trạng thái *logical* thay vì can thiệp cứng.
