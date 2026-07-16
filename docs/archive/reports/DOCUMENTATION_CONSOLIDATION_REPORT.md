# DOCUMENTATION CONSOLIDATION REPORT

Date: 2026-05-30
Target Version: V27.4

## 1. Summary

Quá trình rà soát và gom nhóm (Consolidation) tài liệu đã hoàn tất nhằm dọn dẹp hệ quả của chiến dịch Bug Hunt V27.4. Hàng loạt các báo cáo tĩnh, bản nháp, kết luận phân mảnh đã được đóng gói thành một CSDL (Knowledge Base) duy nhất.

## 2. File Statistics

* **Tổng số file Report/Audit đã review**: 62 file
* **Số file bị đưa vào Archive**: 62 file
* **Số file Core Knowledge được giữ lại / tạo mới**: 4 file (Bao gồm báo cáo này, `PROJECT_BRAIN.md`, `MARCO_KNOWLEDGE_BASE.md`, và `REPORT_INDEX.md`).

## 3. Final Bug Registry (V27.4 -> V27.5)

Tất cả các bug đã được định danh rõ ràng. Không còn khái niệm "có thể" hay "nghi vấn chung chung". 

*   **BUG-001 Focus Desync**: Fixed. Chờ validation.
*   **BUG-002 Movement LUT Reload Race**: Lỗi Data Race khi đọc ghi bảng vật lý 2D trong lúc đổi profile.
*   **BUG-003 Space Swallow Leak**: Quên gán cờ `s_spaceSwallowed = true` khi Regain Focus.
*   **BUG-004 Resolver Starvation**: Lỗi tranh chấp Condition Variable (`s_resolverCv`) giữa 2 luồng background khiến target queue bị kẹt vĩnh viễn. **(Nghiêm trọng nhất)**.
*   **BUG-005 Emergency Unhook Blackhole**: Thông điệp cứu nguy `WM_EMERGENCY_UNHOOK` không được main thread đón nhận. **(Đe dọa Watchdog)**.
*   **BUG-006 Cross-Thread UI Blocking**: Hàm ETW Start gọi `MessageBoxW` sai luồng UI.
*   **BUG-007 Test Infrastructure Rot**: Framework test lỗi thời (API namespace `physics` cũ).
*   **BUG-008 Duplicated Watchdog**: 2 watchdog chạy song song gây chồng chéo.

## 4. Final Technical Debt

*   Bỏ lại rác sau khi tháo dỡ Subtick: `autofire_controller.cpp`.
*   Cờ Build không dùng: `MARCO_ENABLE_LOGGING` và các cờ UI trong `build_config.h`.
*   Message lạc hậu: `WM_TIMER_EXPIRED`.
*   Khóa chặn Legacy F8 tồn tại trong `input_capture.cpp` nhưng không có consumer.

## 5. Conclusion

Dự án đã chính thức đạt ngưỡng 1 Single Source Of Truth (SSOT). Mọi AI mới chỉ cần truy cập `docs/MARCO_KNOWLEDGE_BASE.md` để khởi động (Onboard) thành công. Không cần rà soát lại các thư mục `archive`.