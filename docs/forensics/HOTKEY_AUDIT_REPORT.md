# HOTKEY AUDIT REPORT

## 1. Tracing Hotkeys

### Phím tắt F1
- **Nơi nhận (Hook):** `src\core\input_capture.cpp:224` (trong `KeyboardProc`, `if (vk == VK_F1)`)
- **Nơi debounce:** `src\core\input_capture.cpp:226-227` (`isAutoRepeat = s_hkDownF1 && (now - s_hkLastDownF1 < 500000)`)
- **Nơi publish:** `src\core\input_capture.cpp:232` (`SendNotifyMessageW(s_hwnd, WM_BHOP_TOGGLE, 0, 0)`)
- **Nơi toggle (Logic/Worker/UI):** `src\core\main.cpp:97-101` (`MsgWndProc`, `case WM_BHOP_TOGGLE: bhop::ToggleEnabled(); ui::OnStateChanged();`)

### Phím tắt F2
- **Nơi nhận (Hook):** `src\core\input_capture.cpp:239` (`if (vk == VK_F2)`)
- **Nơi debounce:** `src\core\input_capture.cpp:241-242`
- **Nơi publish:** `src\core\input_capture.cpp:247` (`SendNotifyMessageW(s_hwnd, WM_BHOP_CYCLE_MODE, 0, 0)`)
- **Nơi toggle (Logic/Worker/UI):** `src\core\main.cpp:103-107` (`case WM_BHOP_CYCLE_MODE: bhop::CycleMode(); ui::OnStateChanged();`)

### Phím tắt F3
- **Nơi nhận (Hook):** `src\core\input_capture.cpp:254` (`if (vk == VK_F3)`)
- **Nơi debounce:** `src\core\input_capture.cpp:256-257`
- **Nơi publish:** `src\core\input_capture.cpp:262` (`SendNotifyMessageW(s_hwnd, WM_CYCLE_PROFILE, 0, 0)`)
- **Nơi toggle (Logic/Worker/UI):** `src\core\main.cpp:109-116` (`case WM_CYCLE_PROFILE: rcfg::Apply(...);`)

### Phím tắt F6
- **Nơi nhận (Hook):** `src\core\input_capture.cpp:269` (`if (vk == VK_F6)`)
- **Nơi debounce:** `src\core\input_capture.cpp:271-272`
- **Nơi publish:** `src\core\input_capture.cpp:277` (`SendNotifyMessageW(s_hwnd, WM_TOGGLE_SUSPEND, 0, 0)`)
- **Nơi toggle (Logic/Worker/UI):** `src\core\main.cpp:88-95` (`case WM_TOGGLE_SUSPEND: engine::ToggleSuspend();`)

### Phím tắt F8
- **Nơi nhận (Hook):** `src\core\input_capture.cpp:284` (`if (vk == VK_F8)`)
- **Nơi debounce:** `src\core\input_capture.cpp:286-287`
- **Nơi publish:** `src\core\input_capture.cpp:292` (`SendNotifyMessageW(s_hwnd, WM_CLOSE, 0, 0)`)
- **Nơi toggle (Logic/Worker/UI):** `src\core\main.cpp:138-140` (`case WM_CLOSE: DestroyWindow(hwnd);`)

---

## 2. Các Bug Đã Phát Hiện (Không Sửa)

### Bug 1: Queue Bypass & Blocking LL Hook
- **File:** `src\core\input_capture.cpp` và `src\core\main.cpp`
- **Function:** `KeyboardProc` / `MsgWndProc`
- **Root cause:** Sử dụng hàm `SendNotifyMessageW` để gửi message tới cửa sổ (`s_hwnd`) được tạo bởi chính thread đang chạy hook (main thread). Do gọi trên cùng một thread, Windows sẽ gọi `MsgWndProc` trực tiếp và đồng bộ (synchronous), hoàn toàn bypass message queue. Điều này ép logic Worker/UI chạy trực tiếp bên trong callback của LL Keyboard Hook, gây block hệ thống input của OS và dễ dẫn đến timeout hook.
- **Mức độ nghiêm trọng:** High

### Bug 2: Duplicate Handling (Auto-repeat flaw)
- **File:** `src\core\input_capture.cpp`
- **Function:** `KeyboardProc`
- **Root cause:** Logic debounce hardcode timeout `now - s_hkLastDown < 500000`. Nếu người dùng giữ phím (hold), Windows sẽ tự động gửi các event auto-repeat (WM_KEYDOWN). Nếu setting "Keyboard Delay" của OS được cấu hình chậm hơn 500ms (ví dụ 750ms hoặc 1s), auto-repeat đầu tiên sẽ trễ hơn 500ms. Kết quả là `isAutoRepeat` bị evaluate thành `false`, hệ thống nhận diện nhầm thành một lần bấm phím mới và kích hoạt (duplicate handling).
- **Mức độ nghiêm trọng:** Medium

### Bug 3: Lost Keyup -> Stuck Key State -> Swallowed Events
- **File:** `src\core\input_capture.cpp`
- **Function:** `KeyboardProc`
- **Root cause:** 
  - **Lost keyup & Stuck key state:** Nếu hệ thống bị lag hoặc bị hook khác drop mất event `WM_KEYUP` (lost keyup), biến state (ví dụ `s_hkDownF1`) sẽ bị kẹt ở trạng thái `true` (stuck key state). 
  - **Swallowed events:** Do biến trạng thái bị kẹt ở `true`, nếu người dùng bấm phím thật nhanh một lần nữa (bấm đúp trong vòng 500ms kể từ lần bấm trước), logic sẽ đánh giá `isAutoRepeat = true && (time < 500ms) = true`. Lần bấm hợp lệ thứ 2 này sẽ bị tính nhầm là auto-repeat và bị hàm `return CallNextHookEx` nuốt mất (swallowed event) mà không kích hoạt tính năng.
- **Mức độ nghiêm trọng:** Medium
