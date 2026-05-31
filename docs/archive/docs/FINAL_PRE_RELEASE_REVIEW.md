# FINAL PRE-RELEASE REVIEW

## 1. Kết quả Audit Cuối Cùng

Qua quá trình dò quét không gian codebase `src/` và `include/`, đưa ra kết luận cho các hạng mục:

* **F8 legacy**: Tuy không còn handler chính thức, `PROJECT_BRAIN.md` ghi nhận F8 state tracking vẫn còn tồn tại (stale state trong `input_capture`). Gắn mác: **CÒN (Technical Debt được defer)**.
* **runtime/runtime path**: Không tìm thấy chuỗi `"runtime/runtime"`. **KHÔNG CÒN**.
* **FORENSIC_CAPTURE.log**: Đã bị xóa bỏ hoàn toàn khỏi source. Log giờ lưu theo format thời gian trong `workspace::GetLogRoot()`. **KHÔNG CÒN**.
* **Hardcoded log path**: Vẫn còn tồn tại các tên file hardcode (ví dụ `marco_debug.log` hoặc cấu trúc định dạng `%04u-%02u-%02u` trong telemetry) nhưng đã được route đúng về `workspace::GetLogRoot()` (log root path). **CÒN TÊN FILE, ĐÃ SỬA ROOT PATH**.
* **Duplicated runtime root**: Project root resolution trong `workspace.cpp` hoạt động chuẩn xác, không còn gây ra duplication chuỗi `runtime` lồng nhau. **KHÔNG CÒN**.
* **Dead forensic code**: Một số file unused flags như `MARCO_ENABLE_LOGGING`, `MARCO_ENABLE_ASSERTS`, `WM_TIMER_EXPIRED` vẫn còn định nghĩa do nằm trong mảng Deferred Technical Debt (nợ kỹ thuật chờ xử lý). **CÒN**.

## 2. Đánh giá Giả thuyết Bug

| Giả thuyết | Evidence (Bằng chứng) | Counter Evidence (Phản biện) | Confidence (Độ tự tin) |
|---|---|---|---|
| **1. Focus Desync** | Tỉ lệ gặp lỗi gắn liền với việc alt-tab, focus transitions. Log bắt được semantic routing `route=0` dù vẫn nhận physical input. Patch rebuild state đã sửa thành công. | Thực tế chưa có thời gian chơi đủ lâu để chứng minh lỗi hoàn toàn biến mất (Pending Runtime Evidence). | **HIGH** |
| **2. Logical/Physical Divergence** | Log bắt được hiện tượng Ghost Key ngược (nhả ngoài game, trong game vẫn hold, logical=1, physical=0). Đã thêm `ReconcileLogicalStateFromPhysical`. | Vẫn chờ xác nhận runtime gameplay sau patch V27.4. | **HIGH** |
| **3. Timer Failure** | Hệ thống queue `WM_TIMER_EXPIRED` cũ gây ra jitter, làm trễ/rớt sự kiện thả phím counter-strafe. | Direct Callback đã loại bỏ độ trễ message queue; Stress test báo 0 errors. | **HIGH** |
| **4. BHOP Worker Stall** | Từng có hiện tượng lock mutex tranh chấp giữa BHOP worker và hook path (OS Hook Stall) làm kẹt state máy chủ BHOP. | Kiến trúc đã tách `ForceSpaceSync` & worker độc lập; stress test pass 100%. | **MEDIUM** |
| **5. Profile Reload Race** | Có report lỗi sau khi đổi profile. | Đổi profile kích hoạt Rebuild State, đây là triệu chứng và là cách workaround tự nhiên của người dùng, không phải Root Cause gốc. | **LOW** |
| **6. Hook Timeout** | Windows tự động drop hook nếu worker block quá lâu. | Các low-level hook (`KeyboardProc`/`MouseProc`) không còn xử lý block/UI I/O. | **LOW** |
| **7. OS Event Drop** | Windows có thể bỏ lỡ input event dưới tải cao. | Physical state layer tracking cho phép catch up (eventual consistency) thông qua reconciliation. | **LOW** |

## 3. Quyết định Release Readiness

* **Có nên release ngay không?**
  **KHÔNG**. Dự án đang ở trạng thái "Pending Long-Term Validation" và cấm tuyệt đối việc claim "PASS/STABLE" khi chưa có gameplay evidence thực tế.

* **Có cần chơi thêm không?**
  **CÓ**. Cần phải cho người dùng / dev test trực tiếp trong thực chiến.

* **Nếu cần chơi thêm thì bao lâu?**
  Cần từ **3 đến 7 ngày** thu thập dữ liệu chơi thật (Real Gameplay trong DM/MM sessions) kết hợp việc đảo profile, alt-tab, và test BHOP kéo dài.

* **Cần thu thêm log gì?**
  **KHÔNG ĐƯỢC XÓA FORENSIC**. Phải giữ nguyên vẹn toàn bộ các infrastructure telemetry đã xây ở V27.4.
  Các log bắt buộc giữ:
  * Sự kiện cơ bản: Chuyển đổi trạng thái `FOCUS_LOST`, `FOCUS_GAINED`, `PROFILE_CHANGED`.
  * Sự kiện Anomaly (Bug Hunting): `BHOP_STALL`, `COUNTERSTRAFE_CONFLICT`, `LOGICAL_PHYSICAL_DIVERGENCE`.
  * Logs hệ thống lưu ở file dạng: `runtime/logs/marco_YYYY-MM-DD_HH-MM-SS.log` và crash dumps. 
  
  Mục tiêu thu thập: Chứng minh lỗi Focus Desync không tái phát. Chỉ tiến hành dọn dẹp (V27.5 Observability Slimdown) sau khi mọi bài test trên đều chứng minh được hệ thống thực sự nguyên vẹn.