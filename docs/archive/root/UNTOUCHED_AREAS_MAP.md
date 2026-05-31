# UNTOUCHED AREAS MAP (V27.4)

Bản đồ này liệt kê các phân hệ gần như bị bỏ quên trong toàn bộ quá trình stabilization V27.4, do team chỉ mải tập trung vào Input Hook và Counter-Strafe.

## 🔴 HIGH ATTENTION (Completely Blind Spots)

*   **`src/core/target_platform.cpp` (Resolver & Scanner Workers)**
    *   *Lý do*: Chứa logic đa luồng (Multi-threading), queue, và Condition Variable nhưng chưa từng xuất hiện trong báo cáo Bug Hunt. Có khả năng cao chứa race condition.
*   **`tests/` & `tools/` (Test Infrastructure)**
    *   *Lý do*: Các báo cáo Stress Test (như `STRESS_BHOP_REPORT`) tuyên bố PASS 10.000 iterations, nhưng source code của test harness có dấu hiệu không tương thích ngược với API hiện tại.
*   **`src/core/state_engine.cpp` (Watchdog & Emergency Flush)**
    *   *Lý do*: Watchdog được thiết kế để cứu nguy khi Hook bị kẹt, nhưng luồng xử lý thoát hiểm (Escape path) liên quan đến Window Message có dấu hiệu bị đứt đoạn.

## 🟡 MEDIUM ATTENTION (Under-Reviewed)

*   **`src/ui/ui_analysis.cpp` (ETW Controller UI)**
    *   *Lý do*: Xử lý khởi chạy ETW trace trên background thread và giao tiếp ngược với UI, rủi ro về Cross-thread Win32 API calls.
*   **`src/ui/ui_diagnostics.cpp`**
    *   *Lý do*: Tồn tại một Watchdog thứ 2 độc lập với State Engine. Kiến trúc chồng chéo (Duplicated Abstraction).

## 🟢 LOW ATTENTION (Low Risk / Well Isolated)

*   **`src/ui/ui_dashboard.cpp` & `src/ui/ui_settings.cpp`**
    *   *Lý do*: Chỉ là code vẽ GDI tĩnh, không liên quan đến logic thời gian thực hay độ trễ của game.
*   **`src/core/movement_reconstruction.cpp`**
    *   *Lý do*: Code thuần toán học (Pure math), đã được đóng gói và kiểm thử kỹ từ trước. Ngoại trừ việc thiếu Lock khi update bảng LUT, bản thân logic toán học rất ổn định.