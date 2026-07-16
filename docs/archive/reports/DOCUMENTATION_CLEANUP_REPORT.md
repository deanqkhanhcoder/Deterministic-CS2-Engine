# DOCUMENTATION CLEANUP REPORT

*Date: 2026-05-30 | Execution: Final Documentation Consolidation Pass*

## 1. Overview
Dự án Marco Engine V27.4 sinh ra lượng tài liệu khổng lồ (hơn 60 báo cáo) trong đợt Bug Hunt. Đợt dọn dẹp này áp dụng nguyên tắc **Single Source Of Truth (SSOT)**: một chủ đề chỉ được tồn tại ở một nơi duy nhất. Mọi sự trùng lặp, giả thuyết lịch sử, và báo cáo phân mảnh đều bị loại bỏ hoặc lưu trữ.

## 2. File Status Summary

### Files Kept / Created (CORE & REFERENCE)
Chỉ có 6 tệp tin được phép giữ vai trò điều hướng và truyền đạt tri thức dự án:
1. `PROJECT_BRAIN.md` *(Root - Tóm tắt định hướng cực ngắn)*
2. `docs/MARCO_KNOWLEDGE_BASE.md` *(SSOT - Toàn bộ kiến trúc, Bug Registry, Thread Model)*
3. `docs/REPORT_INDEX.md` *(Mục lục trỏ tới SSOT và Archive)*
4. `docs/reports/ARCHITECTURE_MAP.md` *(Gộp từ Structure Audit, Logging, Path Governance)*
5. `docs/reports/RELEASE_HISTORY.md` *(Changelog tổng hợp)*
6. `docs/forensics/V27_4_INVESTIGATION_SUMMARY.md` *(Gộp toàn bộ quá trình tìm Focus Desync)*

*(Các file phụ trợ như `BUG_CERTIFICATION_MATRIX.md` từ đợt trước vẫn được giữ lại do tính chất pháp lý đặc thù).*

### Files Merged & Archived (HISTORICAL / REDUNDANT)
Toàn bộ các file báo cáo lỗi thời, phân mảnh, giả thuyết cũ đã bị đẩy thẳng vào `docs/archive/`. Chúng đã được tước bỏ quyền "nguồn sự thật" và chỉ còn giá trị khảo cổ. Tiêu biểu bao gồm:
- Toàn bộ `STRESS_*_REPORT.md`
- Toàn bộ `FILE_AUDIT_REPORT_*.md`
- `ROOT_CAUSE_VERDICT.md`, `BHOP_STALL_ROOT_CAUSE.md` (Đã gộp vào `V27_4_INVESTIGATION_SUMMARY.md`)
- `PROJECT_STRUCTURE_AUDIT.md`, `LOGGING_ARCHITECTURE.md`, `PATH_GOVERNANCE_REPORT.md` (Đã gộp vào `ARCHITECTURE_MAP.md`)
- `BLIND_SPOT_AUDIT.md`, `FINAL_NEGATIVE_AUDIT.md`, `FINAL_INDEPENDENT_REVIEW.md`, `V27_4_FINAL_DEEP_SCAN.md` (Tri thức đã trích xuất vào mục **KNOWN BUG REGISTRY** trong `MARCO_KNOWLEDGE_BASE.md`).

## 3. Bug Registry Consolidation
11 Bugs (từ BUG-001 đến BUG-011) hiện tại CHỈ tồn tại và được định nghĩa duy nhất tại:
**`docs/MARCO_KNOWLEDGE_BASE.md` (Section 15: KNOWN BUG REGISTRY)**.
Các tài liệu "Fix Report" rác rưởi từng cố định nghĩa lại các bug này đã bị gạch bỏ hoàn toàn khỏi hệ thống cây thư mục chính.

## 4. Final Result
Hệ thống tài liệu của Marco V27.4 đã giảm từ một mạng nhện hỗn loạn xuống còn một luồng đọc tuyến tính có thể hoàn thành trong 15 phút. Kiến trúc thông tin hoàn toàn sạch sẽ, sẵn sàng chuyển giao cho đợt phát triển V27.5.