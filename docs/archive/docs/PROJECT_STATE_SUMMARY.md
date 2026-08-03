# PROJECT STATE SUMMARY

## Architecture

* **Counter-Strafe**: Trực tiếp chặn và vô hiệu hóa trục di chuyển (axis neutralization), sau đó tự động tiêm phím nhả (injected key-up) thông qua scheduler timing độ trễ thấp để phanh khẩn cấp.
* **BHOP**: Máy trạng thái cho phím Space. Theo dõi state của không gian vật lý so với không gian ảo được inject, sử dụng worker riêng và cơ chế `ForceSpaceSync()` để ngăn chặn kẹt phím khi bị suspend.
* **Input Path**: Đi từ Windows Low-Level Hook (`input_capture`) -> theo dõi physical state -> định tuyến semantic (`input_router`) -> cập nhật logic trong `state_engine`.
* **State Reconciliation**: Đảm nhận đồng bộ lại logical state và physical state sau khi mất và lấy lại focus. Giải quyết các trường hợp Focus Desync hoặc Inverse Ghost Key bằng cách clear các phím đang kẹt và rebuild state từ hardware.
* **Timing**: Hệ thống đếm giờ thread riêng biệt. Đã chuyển từ `WM_TIMER_EXPIRED` (thông qua Message Queue) sang direct callback thẳng tới `engine::OnTimerExpired` nhằm loại bỏ jitter (độ trễ bất định).
* **Telemetry**: Thu thập logs và forensic event tốc độ cao. Gồm `ForensicRingBuffer`, Auto Flush và Crash Flush. Ghi nhận các sự kiện: `FOCUS_LOST/GAINED`, `BHOP_STALL`, `COUNTERSTRAFE_CONFLICT`, `LOGICAL_PHYSICAL_DIVERGENCE`.

## Runtime Layout

* `runtime/bin/`: File thực thi của engine (debug, profile, release).
* `runtime/logs/`: Nơi lưu trữ text log (`marco_debug.log`), forensic session logs (`marco_YYYY-MM-DD_HH-MM-SS.log`) và các log giám sát (watchdog logs).
* `runtime/crash/`: Lưu trữ dump và thông tin khi xảy ra crash (startup crash, fatal crash).
* `runtime/captures/`: Screenshot, chẩn đoán không phải crash.
* `runtime/artifacts/`: ETW traces, CSV exports, biểu đồ fuzzer từ quá trình test.

## Documentation Layout

* `docs/reports/`: Các báo cáo kiến trúc chuẩn, rà soát nợ kỹ thuật (Technical Debt), kiểm định path governance và trạng thái baseline.
* `docs/forensics/`: Hồ sơ điều tra chuyên sâu các lỗi intermittent, kết quả mock stress, đánh giá bug (Bug Verdict) và mô tả các đợt càn quét anomaly.

## Current Version

* **Version**: V27.4.0-stable (Stabilization Candidate)
* **Tag**: Chưa có tag chính thức cho bản release (Đang chờ chứng nhận)
* **Branch**: `v27-stabilization`