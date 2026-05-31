# MARCO V27.4 FINAL EVIDENCE REVIEW (BUG CERTIFICATION)

*Role: Independent Principal Engineer*
*Objective: Rigorous forensic certification of all recorded V27.4/V27.5 bugs against live source code to eliminate speculation.*

## 1. EVIDENCE REVIEW BY BUG ID

### BUG-001: Focus Desync / Inverse Ghost Key
*   **Source Path**: `src/core/input_capture.cpp:IsTargetActive()`, `src/core/state_reconciliation.cpp`
*   **Call Chain**: `KeyboardProc` -> `IsTargetActive()` -> async queue to `ResolverWorker`. On regain -> `RebuildState()`.
*   **Ownership/Sync**: Hook Thread (reader), Resolver Thread (writer) via `s_pubSeq` Seqlock.
*   **Verdict**: **PROVEN**. Lỗ hổng cache state vĩnh viễn đã được fix bằng `!isActive` fallback loop và Seqlock publication.

### BUG-002: Movement LUT Reload Race
*   **Source Path**: `src/core/movement_reconstruction.cpp:InitLUT()`
*   **Call Chain**: UI Thread (`rcfg::Apply`) -> `InitLUT()` (writes). Hook Thread (`HandleKeyUp` -> `LookupStopDur2D`) (reads).
*   **Ownership/Sync**: `s_stopLUT` array is completely unprotected.
*   **Mechanism**: Concurrent write/read causes undefined behavior (Tearing).
*   **Counter-Evidence**: Kiến trúc x86_64 đảm bảo ghi aligned 32-bit `int` là atomic ở mức phần cứng. Tearing thực tế cực kỳ khó xảy ra, nhưng về mặt chuẩn C++ Memory Model thì đây vẫn là Data Race.
*   **Verdict**: **HIGH CONFIDENCE** (Formally proven, practically low impact).

### BUG-003: Space Swallow Leak
*   **Source Path**: `src/core/input_capture.cpp` (Line 142)
*   **Call Chain**: Focus Regain -> `WinEventProc` -> `IsTargetActive` -> `if (s_physSpaceDown)` -> `bhop::OnSpaceDown()`.
*   **Ownership/Sync**: Focus Thread updating local static variable.
*   **Mechanism**: Code gọi hàm khởi động BHOP nhưng bỏ sót việc gán `s_spaceSwallowed = true`. Phím auto-repeat tiếp theo từ hệ điều hành lọt qua filter `if (s_spaceSwallowed) return 1;` và gửi thẳng xuống game, gây một nhịp giật cục (stutter) khi vừa vào game.
*   **Verdict**: **PROVEN**.

### BUG-004: Resolver Starvation
*   **Source Path**: `src/core/target_platform.cpp`
*   **Call Chain**: `ResolveTargetAsync` -> `s_resolverCv.notify_one()`.
*   **Ownership/Sync**: `s_resolverCv` được `wait` bởi CẢ HAI thread `ProcessScannerWorker` và `ResolverWorker`.
*   **Mechanism**: Hàm `notify_one()` báo tín hiệu thức dậy. OS Scheduler ngẫu nhiên đánh thức `ScannerWorker`. `ScannerWorker` tỉnh dậy, kiểm tra điều kiện tắt app (`!s_resolverRunning`), thấy sai, và đi ngủ tiếp. `ResolverWorker` mất tín hiệu vĩnh viễn, làm kẹt queue Target Platform.
*   **Verdict**: **PROVEN** (Lỗi dùng chung Condition Variable kinh điển).

### BUG-005: Emergency Unhook Blackhole
*   **Source Path**: `src/core/state_engine.cpp`, `src/ui/ui_main.cpp`
*   **Call Chain**: Watchdog Thread -> `TriggerEmergencyFlush` -> `PostMessage(s_hwnd, WM_EMERGENCY_UNHOOK, 0, 0)`.
*   **Mechanism**: File `ui_main.cpp` nhận `WM_EMERGENCY_UNHOOK` trong `WndProc` nhưng không hề định nghĩa `case WM_EMERGENCY_UNHOOK:`. Thông điệp rơi vào `DefWindowProc` và bị OS vứt bỏ. Watchdog kêu cứu nhưng không ai nghe.
*   **Verdict**: **PROVEN**.

### BUG-006: Cross-Thread UI Blocking
*   **Source Path**: `src/ui/ui_analysis.cpp` (IDB_ETW_START)
*   **Mechanism**: Sự kiện click tạo một `std::thread` chạy ngầm. Thread này gọi Win32 API `MessageBoxW(hwnd, ...)`. Giao một HWND của luồng chính cho Dialog Box sinh ra ở luồng phụ sẽ ép Windows phải ngầm thực hiện Thread Input Attachment, nguy cơ cao gây Deadlock toàn bộ UI queue nếu main thread đang xử lý message.
*   **Verdict**: **HIGH CONFIDENCE**.

### BUG-007: Test Infrastructure Rot
*   **Source Path**: `tests/test_physics.cpp`, `tests/test_stress.cpp`
*   **Mechanism**: Các file test vẫn đang gọi `physics::SimulateTrueStopDuration()`, trong khi Core Engine đã đổi toàn bộ thành `movement::LookupStopDur2D()`. Lỗi compile 100%. Mọi kết quả pass từ các file này đều là đồ giả mạo / file log cũ.
*   **Verdict**: **PROVEN**.

### BUG-008: Duplicated Watchdog
*   **Mechanism**: Audit trước đó cho rằng `ui_diagnostics` và `state_engine` có Watchdog trùng lặp. Tuy nhiên, đọc kỹ source code: `ui_diagnostics` kiểm tra `g_uiHeartbeatUs` để phát hiện UI đóng băng và dump GDI. `state_engine` kiểm tra `g_blockedTiming` để gỡ Low-level Hook. Hai hệ thống này độc lập, bảo vệ 2 vùng sinh tử khác nhau (Presentation vs Logic). Không hề có sự lãng phí hay trùng lặp.
*   **Verdict**: **REJECTED** (Audit cũ suy luận sai về thiết kế kiến trúc).

### BUG-009: Hook I/O Spinlock Starvation
*   **Source Path**: `src/core/telemetry.cpp`
*   **Call Chain**: `ForensicRingBuffer::FlushToFile` (Background thread) vs `Push` (Hook thread).
*   **Ownership/Sync**: Dùng chung `std::atomic_flag lock`.
*   **Mechanism**: `FlushToFile` chiếm lock, sau đó gọi `fopen`, `fprintf`, `fclose` (đồng bộ I/O ổ đĩa ~ 10-200ms). Cùng lúc, Hook Thread bắt được sự kiện, gọi `Push` và rơi vào vòng lặp `while(lock.test_and_set) _mm_pause();`. Hook Thread bị đứng hình 200ms chờ ổ đĩa. Windows LowLevelHooksTimeout hết hạn, vứt bỏ toàn bộ Hook.
*   **Verdict**: **PROVEN** (Lỗi thiết kế đa luồng chí mạng).

### BUG-010: Watchdog Self Deadlock
*   **Source Path**: `src/core/state_engine.cpp:StartWatchdog`
*   **Mechanism**: Watchdog sinh ra để gỡ rối khi Engine bị kẹt. Khổ nỗi, vòng lặp kiểm tra sức khỏe của nó lại mở đầu bằng `std::lock_guard<std::mutex> lock(s_stateMutex);`. Nếu Engine bị kẹt, 99% nó đang hold `s_stateMutex`. Watchdog lao vào xin lock, tự treo cổ chính mình, không bao giờ chạy được tới dòng báo động.
*   **Verdict**: **PROVEN**.

### BUG-011: Synchronous I/O In Hook
*   **Source Path**: `src/core/input_capture.cpp:IsTargetActive`
*   **Mechanism**: Khi mất Focus, hàm gọi trực tiếp `telemetry::FlushForensicLog()`, tức là thực thi I/O ổ cứng ngay bên trong con trỏ ngắt phần cứng (`KeyboardProc`). 
*   **Verdict**: **PROVEN**.

---

## 2. BUG CERTIFICATION MATRIX

| Bug ID | Severity | Confidence | Evidence Mechanism | Fix In V27.5? |
| --- | -------- | ---------- | -------- | ------------- |
| BUG-001 | CRITICAL | PROVEN | Source/Logs verified | *Fixed V27.4* |
| BUG-002 | LOW | HIGH CONF. | C++ Data Race on Array | FIXED V27.5 - build tested, runtime not verified |
| BUG-003 | MEDIUM | PROVEN | Missing bool assignment | FIXED V27.5 - build tested, runtime not verified |
| BUG-004 | CRITICAL | PROVEN | CV Signal Theft | FIXED V27.5 - build tested, runtime not verified |
| BUG-005 | HIGH | PROVEN | Missing `switch` case | FIXED V27.5 - build tested, runtime not verified |
| BUG-006 | MEDIUM | HIGH CONF. | Cross-thread `MessageBoxW` | FIXED V27.5 - build tested, runtime not verified |
| BUG-007 | HIGH | PROVEN | Uncompilable tests | FIXED V27.5 - build tested, runtime not verified |
| BUG-008 | N/A | **REJECTED** | Distinct sub-system domains | **NO** |
| BUG-009 | CRITICAL | PROVEN | Spinlock I/O Blocking Hook | FIXED V27.5 - build tested, runtime not verified |
| BUG-010 | CRITICAL | PROVEN | Watchdog Mutex Deadlock | FIXED V27.5 - build tested, runtime not verified |
| BUG-011 | HIGH | PROVEN | I/O inside OS Hook | FIXED V27.5 - build tested, runtime not verified |

---

## 2A. V27.5 Execution Update

The certified V27.5 runtime risks were implemented in source. Status for BUG-002/003/004/005/006/007/009/010/011 is:

`IMPLEMENTED`, `TESTED (build)`, `RUNTIME NOT YET VERIFIED`.

Fix mapping:

| Bug ID | Fixed call chain |
| --- | --- |
| BUG-002 | `rcfg::Apply()` -> `movement::InitLUT()` now publishes an immutable LUT snapshot; `LookupStopDur2D()` reads through atomic `shared_ptr` snapshot load. |
| BUG-003 | `WinEventProc()` -> `IsTargetActive()` -> focus regain Space branch now sets `s_spaceSwallowed = true` when BHOP owns held Space. |
| BUG-004 | `ResolveTargetAsync()` now uses `notify_all()` for the shared scanner/resolver condition variable. |
| BUG-005 | `WM_EMERGENCY_UNHOOK` is handled by the hidden message window and now also forwarded by the UI window if received there. |
| BUG-006 | ETW worker thread posts `WM_ANALYSIS_ETW_START_FAILED`; UI thread displays the `MessageBoxW`. |
| BUG-007 | Unit tests use `movement::InitLUT()` and `movement::LookupStopDur2D()` instead of removed `physics::` APIs. |
| BUG-009 | `ForensicRingBuffer::Push()` is non-blocking; `FlushToFile()` snapshots then writes after unlocking. |
| BUG-010 | Watchdog health checks read published seqlock state and emergency recovery uses `try_to_lock`. |
| BUG-011 | Hook/focus paths request asynchronous forensic flush instead of calling `FlushForensicLog()` directly. |

Gameplay/runtime validation is still required before any stability or production claim.

---

## 3. V27.5 ROADMAP RECOMMENDATION

### V27.5 MUST FIX (Kiến trúc chết người)
*   **BUG-004**: Thay `notify_one()` thành `notify_all()` trong `ResolveTargetAsync()`.
*   **BUG-005**: Thêm `case WM_EMERGENCY_UNHOOK:` vào `ui_main.cpp`.
*   **BUG-007**: Xóa/Viết lại Unit Tests với namespace `movement`.
*   **BUG-009**: Dời `FlushToFile` ra khỏi Lock của RingBuffer (thiết kế Shadow Buffer) hoặc chuyển I/O sang thread khác.
*   **BUG-010**: Gỡ bỏ `lock(s_stateMutex)` ra khỏi Watchdog loop. Watchdog chỉ được đọc biến lock-free atomic.
*   **BUG-011**: Xóa lời gọi `FlushForensicLog()` đồng bộ khỏi `input_capture.cpp`, chuyển qua PostMessage hoặc Flag.

### V27.5 SHOULD FIX (Trải nghiệm người dùng)
*   **BUG-003**: Thêm `s_spaceSwallowed = true;` khi Rebuild State có bật BHOP.
*   **BUG-006**: Đưa `MessageBoxW` vào `PostMessage` gọi về main window.

### V27.5 CAN DEFER (Thấp)
*   **BUG-002**: Data Race trên x86 cho biến int/double hiếm khi gây lỗi thực tế. Có thể sửa bằng Read-Write Lock ở V27.6.

### V27.5 REJECTED
*   **BUG-008**: Báo cáo sai. Hệ thống Watchdog hiện tại phân tách UI/Core rất hợp lý.
