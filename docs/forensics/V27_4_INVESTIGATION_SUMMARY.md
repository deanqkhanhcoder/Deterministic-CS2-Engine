# V27.4 INVESTIGATION SUMMARY (BUG HUNT FORENSICS)

*Date: 2026-05-30*

Tài liệu này tổng hợp lại toàn bộ diễn biến của chiến dịch Bug Hunt V27.4. Nó thay thế cho hàng chục báo cáo phân mảnh về hiện tượng Counter-Strafe/BHOP ngẫu nhiên chết trong quá trình chơi dài (Long Session Failure).

## 1. Triệu chứng ban đầu (The Symptoms)
- **Counter-Strafe Stall**: Nhả phím WASD nhưng macro phanh không kích hoạt.
- **BHOP Aborted**: Giữ Space nhưng nhân vật không nhảy, hoặc thi thoảng kẹt Space.
- **Tính chất**: Ngẫu nhiên. Thường xảy ra sau vài ván đấu, đặc biệt dễ gặp nếu người dùng Alt-Tab nhiều hoặc đổi setting. Đổi Profile (F3) hoặc Alt-Tab ra vào lại sẽ tự động hồi phục. Không có bất kỳ lỗi Crash hay Exception nào.

## 2. Các giả thuyết đã điều tra (The Hypotheses)

### 2.1. BHOP Worker Stall (Đã loại trừ là nguyên nhân gốc)
Ban đầu, team nghi ngờ worker thread của BHOP giữ Mutex quá lâu, làm kẹt luồng Hook khi người dùng nhấn Space. Mã nguồn đã được cấu trúc lại (dùng Condition Variable và giảm tối đa scope của lock). Tuy nhiên, sau khi refactor, lỗi thỉnh thoảng vẫn xảy ra, chứng minh đây không phải nguyên nhân gốc.

### 2.2. Timer Queue Jitter (Đã khắc phục, nhưng không phải nguyên nhân gốc)
Phát hiện hiện tượng `WM_TIMER_EXPIRED` đi qua message queue bị OS delay (oversleep). Kiến trúc được đổi sang **Direct Callback** từ Timer Thread. Giúp cải thiện độ trễ cực tốt (p99 < 50us), nhưng lỗi macro chết cứng vẫn tồn tại.

### 2.3. Logical/Physical Divergence (Hệ quả, không phải nguyên nhân gốc)
Logs ghi nhận hiện tượng Ghost Key ngược: người dùng nhả phím ở ngoài game (Logical = 1, Physical = 0). Dẫn tới conflict. Patch `ReconcileLogicalStateFromPhysical()` được triển khai. Tuy nhiên, nó chỉ giải quyết vấn đề kẹt phím, không giải thích được vì sao toàn bộ Macro tịt ngòi.

## 3. Tìm ra nguyên nhân gốc: Focus Desync (The Root Cause)
Nhờ hệ thống Telemetry mới (`ForensicRingBuffer`), team đã bắt được dấu vết cuối cùng trong log:
- Xuất hiện event `FOCUS_LOST`.
- Bàn phím vật lý vẫn gửi event vào `KeyboardProc` bình thường.
- Tuy nhiên, cờ `routeSemantic` bị đánh dấu là `0` (False).

**Phân tích kịch bản**:
Cơ chế Target Resolution cũ dựa vào Foreground Window. Trong điều kiện tải cao (event queue spam), hàm lấy Foreground trả về sai hoặc bị rớt nhịp, khiến Engine "ảo tưởng" rằng CS2 đang chạy ngầm, từ đó nó TỪ CHỐI định tuyến các phím vật lý vào hệ thống Semantic. Counter-Strafe và BHOP không nhận được tín hiệu -> Tịt ngòi.
Việc đổi Profile vô tình gọi `RebuildState()`, ép engine đọc lại phần cứng và reset các cờ, tạo ra hiện tượng "tự hồi phục".

## 4. The Fix Applied
Thiết kế lại hàm `IsTargetActive()` trong `src/core/input_capture.cpp`. 
- Thêm vòng lặp fallback: Nếu phát hiện `!isActive`, liên tục gửi lại request phân giải `ResolveTargetAsync` để sửa sai cho cache.
- Tái cấu trúc cơ chế Regain Focus: Gọi thẳng `RebuildState()` từ Window Event Hook để tái đồng bộ.

## 5. Kết luận
Chiến dịch V27.4 đã thành công tiêu diệt được Focus Desync. Các tàn dư và nợ kỹ thuật phát hiện thêm (Data Race, Resolver Queue, Watchdog Blackhole) đã được chuyển sang V27.5 (Xem `MARCO_KNOWLEDGE_BASE.md` phần BUG REGISTRY). Mọi tài liệu phân tích nhỏ lẻ cũ đã được Archive.