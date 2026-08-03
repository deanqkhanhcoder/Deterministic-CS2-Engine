# MARCO V27.4 FINAL NEGATIVE AUDIT

## 1. PROVEN ISSUES (CRITICAL & HIGH)

Các lỗi dưới đây đã được chứng minh 100% bằng source code (không liên quan đến Focus Desync).

### 1.1. DATA RACE TRÊN MOVEMENT LUT (HIGH)
*   **File**: `src/core/movement_reconstruction.cpp` (`InitLUT`) và `src/core/runtime_config.cpp` (`rcfg::Apply`).
*   **Execution Path**:
    *   *Writer*: UI Thread nhận thay đổi Settings -> Gọi `rcfg::Apply()` -> Gọi `movement::InitLUT()`. Hàm này ghi đè lên mảng tĩnh `s_velocityLUT` và `s_stopLUT` (không dùng Mutex hay Atomic).
    *   *Reader*: Cùng lúc đó, người dùng nhả phím WASD. Hook Thread kích hoạt `engine::HandleKeyUp` -> `AutoCounterStrafe` -> `movement::LookupStopDur2D`. Hàm này truy xuất trực tiếp `s_stopLUT`.
*   **Impact**: Mảng LUT là kiểu `double`/`int`. Đọc và Ghi đồng thời mảng này từ 2 thread khác nhau gây ra undefined behavior (Tearing). Hậu quả thực tế: Counter-strafe đọc sai số mili-giây phanh khẩn cấp trong khoảnh khắc đang đổi config/profile, dẫn đến phanh trượt (Under-brake / Over-brake).
*   **Confidence**: 100% (PROVEN).

### 1.2. LOGIC LEAK: SPACE SWALLOW STATE TRÊN FOCUS REGAIN (MEDIUM)
*   **File**: `src/core/input_capture.cpp`, hàm `IsTargetActive()` (dòng 142).
*   **Execution Path**:
    *   Mất Focus -> Nhấn Space (Vật lý thật) ở ngoài game.
    *   Lấy lại Focus vào game -> `WinEventProc` kích hoạt `IsTargetActive()` -> Phát hiện `s_physSpaceDown == true`.
    *   Code kiểm tra `if (shouldRouteBhop && rcfg::Get().bhopEnabled ...)`, thoả mãn điều kiện -> Nó gọi `bhop::OnSpaceDown()`.
    *   **NHƯNG**, nó KHÔNG set `s_spaceSwallowed = true`.
    *   Lần `VK_SPACE` Auto-Repeat kế tiếp từ hệ điều hành chạy vào `KeyboardProc`, do `s_spaceSwallowed == false`, Hook cho phép event lọt thẳng xuống hệ điều hành (return CallNextHookEx).
*   **Impact**: Game nhận thừa các event Space rác ngay khoảnh khắc Alt-Tab vào game nếu đang đè Space ở ngoài. Gây ra hiện tượng nhân vật nhảy múa lộn xộn một nhịp trước khi BHOP engine kiểm soát lại.
*   **Confidence**: 100% (PROVEN).

## 2. DEAD CODE CANDIDATES (LOW RISK)

Dọn dẹp nợ kỹ thuật (Technical Debt) dư thừa. Không gây crash nhưng làm rối codebase.

*   **`src/core/autofire_controller.cpp`**
    *   *Verdict*: File này không được include vào luồng chính ở bất kỳ đâu trong V27.4 (không xuất hiện trong `engine_internal.h` macro hay routing của `input_router.cpp`). Hậu quả của việc "scrub experimental generational timers" nhưng quên xóa file vật lý.
*   **Legacy Build Flags (`MARCO_ENABLE_LOGGING`, vv.)**
    *   *File*: `include/ui/build_config.h`
    *   *Verdict*: Các macro như `MARCO_ENABLE_LOGGING`, `MARCO_ENABLE_ASSERTS`, `MARCO_ENABLE_UI_OVERLAY` hoàn toàn không có hàm `#ifdef` tương ứng tiêu thụ trong `src/`. `debug_logger` đang phụ thuộc vào `MARCO_ENABLE_FORENSIC` hoặc luôn chạy.
*   **WM_TIMER_EXPIRED (Message ID)**
    *   *File*: `include/core/types.h` (và tàn dư trong comments).
    *   *Verdict*: Sau khi kiến trúc chuyển sang "Direct Callback" (gọi thẳng `engine::OnTimerExpired` từ Timer Thread), hệ thống message queue của Windows không còn dùng ID này nữa.

## 3. THINGS THAT LOOK SCARY BUT ARE ACTUALLY SAFE

Quá trình Red Team có tìm ra một số điểm nghi vấn nguy hiểm, nhưng sau khi đào sâu đã chứng minh an toàn:

### 3.1. Timer Spinloop Break
*   *Mối lo*: `TimerThreadFunc` (`timing.cpp`) spin (`_mm_pause`) bên ngoài Mutex. Nếu `CancelTimer` được gọi từ thread khác, làm sao vòng lặp biết để thoát?
*   *Thực tế*: Hàm `CancelTimer` và `ScheduleTimerUs` luôn set cờ `s_dirty.store(true, memory_order_relaxed)`. Spinloop có dòng `if (s_dirty.load(...)) break;`. Thoát spinloop -> acquire lại Mutex -> tái đánh giá lại mảng `s_slots`. Rất an toàn và độ trễ cực thấp.

### 3.2. Bounded Queue Overwrite (Target Resolver)
*   *Mối lo*: `s_resolverQueue` chỉ có size = 16. Nếu spam focus nhanh, queue bị tràn?
*   *Thực tế*: Logic ghi bằng `s_queueTail = (s_queueTail + 1) % RESOLVER_QUEUE_SIZE;` trước khi ghi đè mục cũ nhất. Vì bản chất `ResolveTargetAsync` là để tìm target *hiện tại*, việc vứt bỏ các target cũ trong quá khứ bị spam là hoàn toàn đúng logic.

### 3.3. Watchdog Emergency Flush Deadlock
*   *Mối lo*: `TriggerEmergencyFlush` hold `s_stateMutex` và gửi message tới UI thread, UI thread lại gọi `Uninstall` đợi Focus Thread (đang hold `s_focusMutex`).
*   *Thực tế*: Nó dùng `PostMessage(s_hwnd, WM_EMERGENCY_UNHOOK, 0, 0)`. `PostMessage` trả về ngay lập tức (không chờ Window Proc xử lý như `SendMessage`). Lock được thả ra ngay. An toàn khỏi Deadlock vòng tròn (Circular Dependency).

## 4. TOP 5 RISKS FOR V27.5 (ROADMAP ADVICE)

1.  **LUT Data Race**: Phải bọc `s_velocityLUT` và `s_stopLUT` vào Seqlock chung với `RuntimeConfig` hoặc thiết kế Double-Buffering cho bảng LUT. Hiện tại là mối họa tiềm ẩn khi đổi profile.
2.  **Focus Regain Swallow Leak**: Sửa 1 dòng code trong `IsTargetActive()`: thêm `s_spaceSwallowed = true;` vào khối `bhop::OnSpaceDown()`.
3.  **Spam Anomaly Ring Buffer**: Lệnh `Push()` liên tục trong `LOGICAL_PHYSICAL_DIVERGENCE` có thể ghi đè các sự kiện sinh tử như `FOCUS_LOST`. Cần cơ chế *Rate-Limiting* (chống spam) cho từng loại Anomaly ID.
4.  **Autofire Dead Code**: Xóa triệt để `src/core/autofire_controller.cpp`.
5.  **Dangling Hooks Protection**: Bổ sung `assert(!s_spaceHeld)` khi gọi `Uninstall()` để đảm bảo không còn event giả nào kẹt lại OS khi đóng app.

## 5. RELEASE IMPACT

*   **Có bug nghiêm trọng chặn Release không?** KHÔNG. Data Race ở LUT rất khó trúng vì người dùng ít khi vừa bắn/di chuyển (cần độ chính xác timer) lại vừa đè nút F3 (đổi profile). Bug Swallow Space không gây kẹt mà chỉ rác input nhẹ ở Frame đầu tiên sau khi Alt-tab.
*   **Lời khuyên:** Tiếp tục giữ nhãn **RELEASE WITH CAUTION** cho V27.4. Đưa các Issue (1.1, 1.2, và dọn dẹp) vào backlog của V27.5 để xử lý. V27.4 hoàn toàn có khả năng phục vụ Test diện rộng.