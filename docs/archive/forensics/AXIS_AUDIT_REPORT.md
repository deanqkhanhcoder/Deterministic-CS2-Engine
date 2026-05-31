# BÁO CÁO KIỂM TOÁN HỆ THỐNG AXIS (AXIS SYSTEM AUDIT REPORT)

**Dự án:** Deterministic-CS2-Engine
**Thành phần:** Axis System (X và Y) - Trạng thái None, Positive, Negative, Conflict (W, A, S, D)
**Mục tiêu:** Kiểm toán vòng đời, xử lý xung đột (Snap Tap), focus regain, alt-tab, dead code và duplicated logic.

---

## 1. Phân tích Vòng đời Axis (A, D, W, S Lifecycle Trace)
Hệ thống Axis hiện tại quản lý 4 trạng thái (None, Positive, Negative, Conflict) thông qua hàm `ResolveAxis` (`counterstrafe_controller.cpp`).

- **None:** Không có phím vật lý nào trên trục được nhấn.
- **Positive (W, D):** Phím hướng dương được nhấn vật lý.
- **Negative (S, A):** Phím hướng âm được nhấn vật lý.
- **Conflict (A+D, W+S):** Cả hai phím trên cùng trục đều được nhấn vật lý. Khi xảy ra Conflict, `NeutralizeAxis` lập tức được gọi để triệt tiêu trạng thái logic (`logical = false`) của cả hai phím, ngăn lỗi di chuyển chéo (Snap Tap mitigation).

**Bhop interaction:** Spacebar/Bhop được định tuyến (route) riêng biệt trong `input_capture.cpp` và hoạt động độc lập. Quá trình kiểm tra khẳng định việc spam Bhop hoặc đè Space không can thiệp hay bypass được logic của Axis.

---

## 2. Các Lỗ Hổng & Bug Nghiêm Trọng Phát Hiện

### 🚨 BUG CRITICAL: Bypass Conflict Resolution (Snap Tap) qua Alt-Tab / Focus Regain
- **Mức độ nghiêm trọng:** Critical (Cho phép người chơi bypass hoàn toàn luật giải đấu / Snap Tap mitigation).
- **File:** `src/core/state_reconciliation.cpp`
- **Functions:** `RebuildState`, `ReconcileInternal`, `ResolveAxis`
- **Nguyên nhân cốt lõi (Root Cause):**
  1. Khi người chơi nhấn giữ A và D (hoặc W và S) rồi **Alt-Tab ra ngoài**, `ClearHeldKeys` gọi `ReconcileInternal(true)`. 
  2. `ReconcileInternal` xóa `s_state.axisState` về `None`, nhưng do người chơi vẫn đè phím nên `s_state.phys` vẫn lưu giá trị `true`.
  3. Ở cuối `ReconcileInternal`, hệ thống gọi lại `ResolveAxis(X)`. Do `phys` của A và D vẫn `true`, `ResolveAxis` lập tức set lại `axisState` thành `Conflict` (và gọi `NeutralizeAxis`, tắt `logical`). Vậy lúc mất focus, `axisState` mang giá trị `Conflict`.
  4. Khi người chơi **Alt-Tab quay lại game** (Regain Focus) vẫn đang giữ A và D, hàm `RebuildState` được gọi.
  5. `RebuildState` phát hiện phím vật lý đang giữ, liền **tự ý bật `logical = true`** cho cả A và D rồi mới gọi `ResolveAxis`.
  6. Trong `ResolveAxis`, do `newState` tính ra là `Conflict` (vì đè 2 phím) và `prevState` cũng đang là `Conflict` (từ bước 3), lệnh kiểm tra `if (newState == prevState) return;` kích hoạt. `ResolveAxis` lập tức `return` sớm.
  7. **Hậu quả:** Hàm `NeutralizeAxis` không bao giờ được gọi. Cả 2 phím A và D đều bị gửi `logical = true` vào trong game cùng một lúc, phá vỡ hoàn toàn cơ chế chống trùng lặp.

---

## 3. Các Vấn Đề Kiến Trúc & Code Smell

### ⚠️ Duplicated Logic & State Inconsistency
- **File:** `src/core/state_reconciliation.cpp`
- **Function:** `RebuildState`
- **Vấn đề:** 
  Có sự trùng lặp và thiếu nhất quán trong cách cập nhật trạng thái `logical`. Ở luồng hook bình thường (`input_router.cpp` -> `HandleKeyDown`), hệ thống gọi `ResolveAxis` trước, rồi dựa vào kết quả (`curState != Conflict`) mới quyết định có set `logical = true` hay không. 
  Tuy nhiên, trong `RebuildState`, nó lại set `logical = true` TRƯỚC khi gọi `ResolveAxis`, dẫn đến lệ thuộc hoàn toàn vào khả năng sửa sai của `NeutralizeAxis`. Khi `ResolveAxis` bị exit sớm (như bug trên), `logical` bị lỗi vĩnh viễn (stale state). 

### 🧹 Dead Code / Unused Variables
- **File:** `src/core/counterstrafe_controller.cpp`
- **Function:** `ApplyOverlapCounterStrafe`
- **Vấn đề:** 
  Hàm nhận vào tham số `int64_t brakeUs` nhưng lại bị ép kiểu bỏ qua ngay dòng đầu tiên (`(void)brakeUs;`) và hoàn toàn không được sử dụng bên trong hàm. Việc lên lịch timer (ScheduleTimer) đang được gọi riêng lẻ ở hàm cha `AutoCounterStrafe`. Tham số này hiện diện vô nghĩa và nên bị gỡ bỏ để tránh gây hiểu nhầm cho các quá trình tối ưu hoá sau này.

---
**Kết luận:** Hệ thống ResolveAxis đang có thiết kế gốc đúng đắn, nhưng cơ chế đồng bộ hoá State Synchronization (Reconciliation) khi Regain Focus chưa được đóng gói an toàn, dẫn tới rò rỉ trạng thái Conflict. Yêu cầu vá (patch) gấp `RebuildState` mà không thay đổi triết lý thiết kế.
