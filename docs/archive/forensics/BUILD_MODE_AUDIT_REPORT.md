# BÁO CÁO KIỂM TOÁN BUILD MODE (RELEASE VS DEBUG)

## 1. MỤC ĐÍCH
Phân tích sự khác biệt giữa các macro cấu hình build (chủ yếu là `MARCO_DEBUG_FORENSIC` và `MARCO_RELEASE`) để tìm ra các đoạn mã chỉ chạy ở Debug hoặc Release, và đặc biệt là phát hiện các logic gameplay bị vô tình vô hiệu hóa khi build ở chế độ Release.

## 2. PHÂN TÍCH CÁC MACRO
### 2.1. Cấu hình Debug (`MARCO_DEBUG_FORENSIC`)
Khi build Debug, các macro sau được kích hoạt (định nghĩa trong `build_config.h`):
- `MARCO_ENABLE_ETW`: Kích hoạt theo dõi kernel bằng ETW.
- `MARCO_ENABLE_WATCHDOG`: Kích hoạt Watchdog để giám sát tình trạng deadlock của các luồng.
- `MARCO_ENABLE_HEARTBEATS`: Kích hoạt theo dõi timestamp (heartbeat) của các thread (Hook, Timing, Scanner).
- `MARCO_ENABLE_DIAGNOSTICS` / `MARCO_ENABLE_FORENSIC_UI`: Bật giao diện chẩn đoán overlay và bảng theo dõi (UI Diagnostics).
- `MARCO_ENABLE_TELEMETRY` / `MARCO_ENABLE_HEAVY_TELEMETRY`: Thu thập các chỉ số về độ trễ, oversleep, jitter, core migrations...
- `MARCO_ENABLE_LOGGING`: Kích hoạt bộ ghi log (ghi ra file).

### 2.2. Cấu hình Release (`MARCO_RELEASE` hoặc `NDEBUG`)
Khi build Release, phần lớn các tính năng debug bị tắt để tiết kiệm tài nguyên. Tuy nhiên, nó lại kích hoạt:
- `MARCO_ENABLE_HEARTBEATS`: Vẫn giữ nguyên việc cập nhật heartbeat.
- `MARCO_ENABLE_RELEASE_DASHBOARD`: Thay thế giao diện chẩn đoán chi tiết bằng một bảng điều khiển đơn giản.

## 3. CÁC LỖI VÀ LOGIC GAMEPLAY BỊ VÔ TÌNH VÔ HIỆU HÓA Ở RELEASE
Sau khi kiểm tra sự phụ thuộc của các `#if`, đã phát hiện 2 vấn đề lớn trong chế độ Release:

### Vấn đề 1: Tính năng Fallback / Fail-Safe (Watchdog) bị vô hiệu hóa hoàn toàn
- **Vị trí**: `src/core/main.cpp` (dòng 59, 265), `src/core/state_engine.cpp` (dòng 135-208, 217).
- **Phân tích**: Việc loại bỏ `MARCO_ENABLE_WATCHDOG` đã vô tình tắt hoàn toàn luồng Watchdog. Watchdog không chỉ dùng để debug mà còn chứa hàm `TriggerEmergencyFlush()` - tính năng fail-safe quan trọng để phát hiện và tự động giải phóng (flush) các phím bấm bị kẹt (stuck keys) do lỗi bất đồng bộ giữa logic và vật lý (`logicalVal && !physVal`).
- **Hậu quả**: Ở bản Release, nếu xảy ra tình trạng kẹt phím (do hook bị bypass bởi quyền Admin hoặc bị rớt event), engine sẽ không bao giờ tự động gỡ kẹt phím, ảnh hưởng nghiêm trọng đến trải nghiệm gameplay.

### Vấn đề 2: Background Target Polling không chạy
- **Vị trí**: `src/core/main.cpp` (dòng 64-70).
- **Phân tích**: Hàm `capture::PollTarget()` chỉ được gọi định kỳ bên trong `WatchdogTimerProc` (vốn được bọc bởi `#if MARCO_ENABLE_WATCHDOG`).
- **Hậu quả**: Ở bản Release, game process không được quét cập nhật (poll) ở chế độ nền. Engine chỉ nhận diện được sự thay đổi target thông qua các sự kiện đổi focus cửa sổ (`WM_TARGET_REFRESH_REQUEST` hoặc `IsTargetActive` thay đổi), dẫn đến việc nhận diện game có thể chậm hoặc không hoạt động nếu người dùng mở game mà không làm thay đổi focus ngay lập tức.

## 4. KẾT LUẬN VÀ KIẾN NGHỊ
- Không có lỗi biên dịch nào do thiếu biến/macro nhờ việc quản lý phạm vi khối (scope) khá kỹ lưỡng trong `timing.cpp` (biến `currentCore` dù chỉ được khai báo trong scope của macro `MARCO_ENABLE_TELEMETRY` nhưng vẫn an toàn).
- Tuy nhiên, việc gộp tính năng "chống kẹt phím" (Fail-Safe) vào chung với "Watchdog Debug" đã gây ra lỗi mất logic gameplay ở chế độ Release.
- **Khuyến nghị**: Tách logic phát hiện kẹt phím (`corruptionCounter` và `TriggerEmergencyFlush`) ra khỏi `MARCO_ENABLE_WATCHDOG` và đảm bảo nó luôn chạy kể cả trên bản Release. Gọi `capture::PollTarget()` thông qua luồng Timer chung thay vì phụ thuộc vào Watchdog.
