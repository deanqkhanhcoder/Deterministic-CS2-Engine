# POST-STABILIZATION DEEP AUDIT
*Date: 2026-06-10*

## I. OVERVIEW
Báo cáo này tổng hợp kết quả điều tra chuyên sâu từ 7 Agent độc lập (Log Forensics, Counter-Strafe, BHOP, Threading, Long Session, Observability, Negative Reviewer) sau 1 tuần Marco chạy thực tế. Mục tiêu là xác minh tính ổn định của hệ thống và đánh giá hiệu quả của các bản vá từ V27.4 đến V27.7.

## II. DETAILED FINDINGS

### 1. Core Architecture & Focus 
- **Focus Desync / Routing (REJECTED)**: Không có bất kỳ dấu vết nào của lỗi Focus Desync hay `route=0` bất thường. Logic tái đồng bộ (RebuildState) khi Regain Focus hoạt động chính xác.
- **Counter-Strafe State (REJECTED)**: Các hiện tượng `LOGICAL_PHYSICAL_DIVERGENCE` được ghi nhận trong log là hành vi bình thường của hệ thống xử lý overlap phím, không phải là kẹt phím.

### 2. Concurrency & Threading (CRITICAL)
- **V27.5 / V27.7 Deadlocks (PROVEN FIXED)**: Các rủi ro như Watchdog Mutex Deadlock, CV Starvation, Spinlock I/O Blocking và Lock Inversion đều đã được khắc phục hoàn toàn. Shutdown ordering an toàn tuyệt đối.
- **Lock-Free Writer Violation (PROVEN)**: Trong `runtime_config.cpp`, mô hình Seqlock chỉ xử lý an toàn luồng đọc, tuy nhiên **không có `std::mutex` bảo vệ luồng ghi**. Nếu giao diện người dùng (chạy trên UI Thread) và hotkey chuyển profile (chạy trên Main Thread) cùng kích hoạt `rcfg::Apply()`, sẽ xảy ra Data Race nghiêm trọng, gây rách cấu hình (Tearing) và Memory Corruption đối với Physics Engine.

### 3. Log Forensics & Long Session Stability
- **DispatchMessage Stalls (PROVEN)**: Mặc dù V27.7 đã chuyển `OutputDebugStringA` sang luồng nền, `marco_debug.log` thực tế vẫn ghi nhận 12 lần luồng chính bị treo (stall) từ 100ms lên tới 1.5 giây. Đồng thời, ghi nhận hiện tượng focus chớp nháy liên tục (PID toggling) giữa tiến trình 9600 và 6896. Điều này chứng tỏ nút thắt cổ chai vẫn chưa được xử lý tận gốc.
- **Log Queue Growth / Memory Leak (PROVEN)**: Cấu trúc hàng đợi `s_logQueue` (`debug_logger.cpp`) là unbounded. Với lượng log khổng lồ được nạp vào liên tục từ các OS hook, và việc `OutputDebugStringA` xử lý đồng bộ quá chậm ở đầu ra, tốc độ đẩy vào sẽ vượt xa tốc độ lấy ra, khiến RAM bị phình to vô hạn dẫn đến OOM trong các phiên chạy dài.
- **Stale ETW State (PROVEN)**: Cấu hình file trace ETW giới hạn tối đa 100MB nhưng không bật cờ `CIRCULAR`. Nếu chơi nhiều giờ liền khiến file đầy, hệ thống sẽ ngừng thu thập sự kiện mới vĩnh viễn.

### 4. Mechanics (BHOP & Movement)
- **Timer Drift trong BHOP (PROVEN)**: Ở `bhop.cpp` (`DispatchJump`), thuật toán thực hiện 3 lần `PrecisionWait` (holdT, 1ms, compDelay) nhưng chỉ bù nhiễu jitter `s_jitterAccum` ở lần cuối cùng. Đồng thời, thời gian thực thi của các OS API không được tính đến. Sự sai số này tích lũy dần làm lệch thời gian nhảy BHOP so với tính toán lý thuyết.
- **BHOP Stalls/Leaks (REJECTED)**: Không có lỗi kẹt trạng thái hay rò rỉ bộ đếm. Abort sequence hoạt động chính xác khi cửa sổ mất focus.

### 5. Observability & Telemetry
- **Phân mảnh Metrics trên UI (PROVEN)**: Các chỉ số của thẻ UI Analysis hiển thị hoàn toàn bằng 0 ở bản Release. Nguyên nhân là do UI Analysis sử dụng chung pipeline Forensic (`g_traceBuffer`) nhưng pipeline này bị loại bỏ khi biên dịch (`#if MARCO_ENABLE_FORENSIC = 0`).
- **Telemetry Version Hardcode (PROVEN)**: File `telemetry.cpp` đang hardcode in ra `Version: v27.4.0-stable`, làm sai lệch thông tin versioning của log thực tế (hiện tại là V27.7).

## III. CONCLUSION

Dựa trên các bằng chứng thu thập được từ phân tích mã nguồn và dữ liệu thực tế, Marco vẫn còn những rủi ro cực kỳ nghiêm trọng về luồng và hiệu năng chưa được xử lý.

**REGRESSION DETECTED**

## IV. RECOMMENDATIONS FOR V27.8
1. **Critical:** Bổ sung `std::mutex` bên trong `rcfg::Apply()` để nối tiếp các yêu cầu ghi cấu hình.
2. **Critical:** Xử lý tình trạng tràn RAM của `s_logQueue` (chuyển sang bounded queue hoặc drop message). Điều tra lại nguyên nhân gốc khiến luồng chính bị stall 1.5s dù đã decouple Logger.
3. **High:** Cập nhật cơ chế trừ Jitter cho cả 3 lần gọi `PrecisionWait` trong BHOP.
4. **Medium:** Đồng bộ Metric Pipeline của UI Analysis không phụ thuộc vào `MARCO_ENABLE_FORENSIC`. Bật cờ `CIRCULAR` cho ETW file. Cập nhật Hardcode Version.
