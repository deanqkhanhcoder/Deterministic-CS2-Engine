# BÁO CÁO KHOA HỌC: KIẾN TRÚC VÀ QUÁ TRÌNH PHÁT TRIỂN MARCO COUNTER-STRAFE ENGINE

## 1. GIỚI THIỆU DỰ ÁN
Marco là một hệ thống hỗ trợ counter-strafe (dừng chuyển động nhanh) thời gian thực, độ trễ siêu thấp (ultra-low latency), được thiết kế chuyên biệt cho môi trường competitive. Bài toán lõi của dự án là đọc input từ bàn phím, tính toán vận tốc vật lý giả lập của nhân vật dựa trên thời gian di chuyển, và ngay lập tức tiêm (inject) phím ngược lại trong một khoảng thời gian chính xác xác định theo hàm decay vật lý nhằm hãm đà (brake) tức thời. 
Dự án đòi hỏi một hệ thống deterministic input processing (xử lý input mang tính quyết định), khả năng nhận diện scheduler của hệ điều hành, phòng tránh SMT (Simultaneous Multithreading) collision bằng topology awareness, và duy trì timing ở mức microsecond.

## 2. KIẾN TRÚC TỔNG THỂ
Marco được chia làm 3 layer chính phân tách hoàn toàn nhằm triệt tiêu lock contention:
1. **Core Runtime**: Bao gồm Timing Engine, Input Pipeline, và Physics Engine. Chạy độc lập trên các luồng có độ ưu tiên cao (Realtime/Highest).
2. **UI Layer**: Giao diện Win32 GDI Allocation-Free, được chạy trên luồng phụ, tách biệt hoàn toàn khỏi đường ống (hotpath) xử lý input.
3. **Telemetry & Configuration**: Hệ thống thu thập số liệu (Ring buffers) không cần lock (lock-free) và bộ nạp cấu hình thời gian thực.

```text
[Input Hardware] -> [OS Message Queue] -> [Input Pipeline (Hook Thread)]
                                                   |
    +----------------------------------------------+
    | (Logical State & Capability Routing)
    v
[State Engine] <--> [Physics Model]
    |
    | (Schedule Brake & Overlap Quantization)
    v
[Timing Engine (Dedicated Core)] -> [SendInput Injection] -> [Game Client]
    |
    v (Lock-free Ring Buffers)
[Telemetry / Watchdog] <--- [UI Rendering / Dashboard]
```

## 3. INPUT PIPELINE
Pipeline input được xây dựng dựa trên Low-Level Keyboard Hook (`SetWindowsHookExW` với `WH_KEYBOARD_LL`).
* **Semantic Routing**: Khi một phím (W/A/S/D) được nhấn, `KeyboardProc` đánh giá liệu sự kiện này đến từ người chơi hay từ chính engine tiêm vào (Injected Event Filtering) thông qua cờ `dwExtraInfo == 0x1337BEEF`.
* **Physical vs Logical State**: Trạng thái thực lý (nhấn phím vật lý) và trạng thái logic (những gì engine gửi cho game) được tách biệt.
* **Focus Tracking**: Chặn không cho input định tuyến (routing) nếu Game Target không ở trạng thái Active Window, chống nhiễu hệ điều hành.

## 4. COUNTER-STRAFE ENGINE
Engine hoạt động dựa trên biểu đồ trạng thái (state machine) 4 trục độc lập:
* Khi trục đang di chuyển (ví dụ: đang giữ A) được thả ra, `AutoCounterStrafe` sẽ được kích hoạt.
* **Overlap Logic**: Engine quản lý việc người dùng bấm phím ngược lại trước khi phím cũ thả ra (overlap) và thực hiện quantization (đóng băng timing) để giữ nguyên sức mạnh của vector hãm ban đầu mà không bị cắt vụn do nhiễu vật lý.
* **Release Ordering & Reconciliation**: Đảm bảo các sự kiện `KeyUp` vật lý không triệt tiêu các sự kiện `KeyDown` được tiêm (injected), engine sẽ giành quyền sở hữu (state ownership) và giữ trạng thái cho đến khi hoàn thành `finalDurMs`.

## 5. PHYSICS MODEL
Thay vì dùng một giá trị cố định, hệ thống mô phỏng vận tốc và động lượng.
* Cập nhật `CalcIntentVelocity` liên tục: khi giữ phím, vận tốc nội tại tăng dần lên `physMaxSpeed`. Khi đảo hướng nhanh, vận tốc được kế thừa theo hệ số.
* **Dynamic Scaling & Brake Authority**: Cường độ (strength) phanh được nội suy từ vận tốc hiện tại.

**Công thức lõi:**
```cpp
double strength = physics::CalcStopStrength(intentV) * profile.tap_strength_multiplier;
strength = std::pow(strength, profile.release_curve_exponent);

// [FIX] Tăng 15% authority hãm phanh toàn cầu
int finalDurMs = std::max(rc.minStopMs, 
                 (int)std::round(baseDurMs * strength * 1.15) 
                 - rc.latencyMarginMs);
```
* `intentV`: Vận tốc giả lập trước khi phanh.
* `baseDurMs`: Thời gian phanh tối đa định nghĩa trong profile.
* `strength`: Hệ số từ 0.0 -> 1.0 (sau khi áp dụng release curve).
* `1.15`: Brake authority boost, đảm bảo game server ghi nhận đủ xung lực hãm.
* `latencyMarginMs`: Khấu trừ độ trễ hệ thống/hook.

## 6. TIMING ENGINE
Vì `Sleep()` của Windows có độ sai số lên tới 15.6ms, Marco sử dụng một Custom Microsecond Scheduler:
* **Timer Slots & Dispatch**: Chạy trên một luồng vòng lặp liên tục, theo dõi các yêu cầu `timerPending`.
* **Adaptive Spin Wait & Wake Strategy**: Kết hợp MMCSS (Multimedia Class Scheduler Service) để xin độ ưu tiên. Khi thời gian chờ lớn hơn 2ms, sử dụng `Sleep(1)`. Khi còn dưới 2ms, sử dụng *Spin Wait* với `_mm_pause()` (yield SMT) để tiết kiệm chu kỳ CPU, duy trì oversleep ở mức <50us.

## 7. THREAD TOPOLOGY & CPU AFFINITY
Để đảm bảo Hook Thread và Timing Thread không bị tranh chấp bộ đệm L1/L2 hoặc bị OS gạt đi, hệ thống phát hiện cấu trúc CPU (Topology Discovery):
* **SMT Collision Avoidance**: Ngăn không cho Timing Core và Hook Core chạy trên 2 logical processors thuộc cùng 1 physical core (tránh xung đột ALU/cache).
* **Thread Affinity Assignment**: Ép cứng các luồng quan trọng vào các lõi P-Core riêng biệt.

## 8. UI SYSTEM
Giao diện không sử dụng Framework nặng (như ImGui/Qt) mà code thủ công bằng Win32 GDI:
* **Immediate-mode Rendering**: Vòng lặp vẽ trực tiếp từ `RuntimeSnapshot`.
* **Allocation-free Paint Loop**: Không khởi tạo vùng nhớ mới (`new/malloc`) trong hàm vẽ (`WM_PAINT`), triệt tiêu Garbage Collection và heap fragmentation.
* Bao gồm hệ thống Clipping, Cài đặt Profile, Analysis Tab vẽ đồ thị Jitter / Oversleep theo thời gian thực (rolling timeline).

## 9. TELEMETRY & ANALYSIS
* Tích hợp các bộ đệm vòng `MetricBuffer` (lock-free) đếm: Hook Latency, Timer Jitter, Oversleep, Spin Duration, State Mutation Latency.
* Dashboard hiển thị histogram và timeline array với 120 mẫu. 
* ETW Analysis (dành riêng cho Profile mode) cung cấp dump chuyên sâu.

## 10. BUILD SYSTEM
Sử dụng Makefile với 3 hệ con chuyên biệt:
* **DEBUG_FORENSIC**: Chứa Macro phân tích sâu, logs bật, có console window (`-O0`).
* **PROFILE**: Bật tối ưu hóa (`-O2`), tắt log thừa, giữ Watchdog, có ETW diagnostic. Console được ẩn (`-mwindows`).
* **RELEASE**: Khóa toàn bộ Telemetry nặng, Watchdog tắt, tắt Console (`-mwindows`), tối đa hóa inline và tối ưu hóa (`-O3`). Được thiết kế cho môi trường thi đấu.

## 11. CÁC BUG & REGRESSION QUAN TRỌNG ĐÃ GIẢI QUYẾT
1. **Recursion Crash & Synthetic Event Poisoning**:
   * *Nguyên nhân*: Hàm Injected của Marco lại kích hoạt Hook của Marco, vòng lặp vô hạn gây tràn Stack (Stack Overflow).
   * *Fix*: Sử dụng `dwExtraInfo = 0x1337BEEF` do cờ `LLKHF_INJECTED` có thể bị Byfron/Anti-cheat lột bỏ. Fix này an toàn vì OS tôn trọng cấu trúc ExtraInfo.
2. **Timing Quantization Collapse**:
   * *Nguyên nhân*: Người dùng spam A/D quá nhanh, timer đè lên nhau khiến phanh bị "ăn bớt" (còn 2-3ms).
   * *Fix*: Áp dụng quy tắc "Người sống sót" (Overlap survival) và `MIN_BRAKE_PHASE_US` đóng băng bộ đếm nếu chưa hoàn tất phanh.
3. **Core 0 Publication Bug**:
   * *Nguyên nhân*: Biến Tracking Affinity nằm trong khối `#if MARCO_ENABLE_TELEMETRY`. Ở bản Release (telemetry=0), biến thành 0 và Dashboard báo "Core 0".
   * *Fix*: Gỡ biến Tracking Affinity ra khỏi Macro, định tuyến độc lập.
4. **Weak Brake Vector (Continuity Penalty Collapse)**:
   * *Nguyên nhân*: Hàm suy hao vận tốc vật lý (`continuityFactor`) làm giảm hãm phanh khi đổi hướng nhanh.
   * *Fix*: Xóa phạt continuity, đảm bảo xung lực max 100% khi intent vector đủ mạnh.

## 12. TỐI ƯU HIỆU NĂNG
* Tách `g_activeTimingCore` và snapshot khỏi hotpath.
* Tránh sử dụng `std::mutex` trong Hook. Thay vào đó dùng các phép toán `std::atomic` và Spinlock trọng lượng nhẹ.
* Low latency design: Đảm bảo Hook P99 luôn duy trì ở mức < 10us.

## 13. HƯỚNG PHÁT TRIỂN TƯƠNG LAI
* **Latency Prediction**: Dự đoán khung hình của game để đồng bộ hóa Injected Input.
* **Kernel-mode Injection**: Cân nhắc sử dụng driver cấp thấp thay vì SendInput để tránh sự can thiệp của raw input hooks.
* **Adaptive Runtime Tuning**: AI tự động điều chỉnh hệ số `spin duration` dựa trên thermal throttling của hệ thống.

## 14. HƯỚNG DẪN SỬ DỤNG
* **Build Project**: Cài đặt MSYS2 (MinGW-w64). Chạy `make release` để lấy bản production-ready, hoặc `make debug` để mở cửa sổ log forensic.
* **Chạy Project**: Chạy `bin/release/marco.exe`. Giao diện xuất hiện ở góc màn hình.
* **Config Profiles**: Vào tab Settings, chỉnh Min Brake, Base Brake, Curve, Mode. Thông số lý tưởng cho Rifle là 25ms base.
* **Hotkeys**: `F1` (Disable), `F2` (Rifle), `F3` (Pistol), `F6` (Sniper), `F8` (SMG).
* **Analysis**: Chuyển sang Tab "Analysis" để xem sức khỏe lõi (Core Health), SMT Collision, Jitter.

## 15. KẾT LUẬN
Dự án Marco đã hoàn thiện mục tiêu xây dựng một deterministic runtime cho bài toán Counter-Strafe. Từ chỗ phụ thuộc vào sleep thô sơ, engine đã tiến hóa thành một hệ thống nhận biết topology, tính toán vật lý giả lập chính xác, và tiêm sự kiện với độ trễ dưới 50us. Workspace hiện đang ở trạng thái chuẩn công nghiệp, sẵn sàng cho các vòng đời production lâu dài.

---

# BÁO CÁO MỞ RỘNG: TÀI LIỆU VẬN HÀNH VÀ THAM CHIẾU MODULE

## 16. GIẢI THÍCH TOÀN BỘ MODULE

Kiến trúc nguồn được chia thành các phân hệ (module) độc lập, mỗi module đảm nhận một vi nhiệm vụ cực kỳ cụ thể nhằm phân tách trách nhiệm (Separation of Concerns).

### 16.1. `input_capture.cpp`
* **Nhiệm vụ**: Đăng ký và lắng nghe Low-Level Keyboard Hook (`WH_KEYBOARD_LL`). Phân loại tín hiệu thật từ phần cứng và tín hiệu giả lập do chính Marco sinh ra.
* **Hotpath**: `KeyboardProc` callback.
* **Thread Ownership**: OS Hook Thread (luồng do Windows cấp phát ngầm hoặc luồng gọi `SetWindowsHookExW`).
* **Realtime Constraints**: Hàm callback phải trả về cực nhanh (thường dưới 5-10ms), nếu không Windows sẽ tự động tháo hook (Silent Unhook) để bảo vệ hệ điều hành. Do đó, KHÔNG có bất kỳ hàm chặn (blocking call) nào trong file này.

### 16.2. `state_engine.cpp`
* **Nhiệm vụ**: Trái tim logic của Counter-Strafe. Quản lý vòng đời của 4 trục di chuyển (W, A, S, D). Đánh giá khi nào một phím được thả ra, tính toán overlap, và quyết định gửi lệnh hãm phanh.
* **Dữ liệu quản lý**: Trạng thái `AxisState`, `timerPending`, trạng thái vật lý (`s_state.phys`).
* **Hotpath**: `AutoCounterStrafe()`.
* **Critical Sections**: Được bảo vệ bằng các spinlock siêu nhẹ để giao tiếp giữa Hook Thread và Timing Thread.

### 16.3. `timing.cpp`
* **Nhiệm vụ**: Lập lịch microsecond. Chờ (sleep/spin) đến chính xác thời điểm cần gửi lệnh KeyUp hãm phanh.
* **Hotpath**: `TimingWorker` loop.
* **Thread Ownership**: Timing Core (được ép vào một P-Core độc lập thông qua `SetThreadAffinityMask`).
* **Dependency**: Giao tiếp một chiều từ `state_engine.cpp` thông qua mảng cờ nguyên tử (atomic flags) và deadline `timerStartTimeMs`. Không gọi ngược lại Hook Thread.

### 16.4. `physics.cpp`
* **Nhiệm vụ**: Giả lập hệ thống vật lý (động lượng, ma sát) của Source Engine.
* **Dữ liệu**: `intentV` (vận tốc giả định), các hệ số suy hao (decay/attenuation).
* **Nhiệm vụ lõi**: Cung cấp hàm `CalcIntentVelocity()` và `CalcStopStrength()` để trả về cường độ phanh chính xác dựa trên thời gian di chuyển.

### 16.5. `injection.cpp`
* **Nhiệm vụ**: Cầu nối giao tiếp với hệ điều hành để tiêm tín hiệu (Injected Inputs).
* **Hotpath**: `SendInput` API hoặc các phương thức tiêm cấp thấp.
* **Cơ chế**: Nhãn các sự kiện bằng `dwExtraInfo = 0x1337BEEF` để module `input_capture` có thể nhận diện và bỏ qua.

### 16.6. `topology.cpp`
* **Nhiệm vụ**: Quét cấu trúc CPU (topology discovery). Phát hiện các lõi thực (P-Cores), lõi hiệu năng thấp (E-Cores) và ánh xạ SMT (Hyper-Threading).
* **Dependency**: Sử dụng `GetLogicalProcessorInformationEx` của Win32.
* **Vai trò**: Tránh SMT Collision, ngăn chặn việc Hook Core và Timing Core chạy trên 2 luồng của cùng 1 lõi vật lý gây chia sẻ L1/L2 cache.

### 16.7. `telemetry.cpp`
* **Nhiệm vụ**: Thu thập metrics (Jitter, Oversleep, Hook Latency) theo dạng lock-free ring buffer.
* **Thiết kế**: Không sử dụng `std::mutex`. Chuyên sử dụng `std::memory_order_relaxed` cho các điểm đo đạc để không làm chậm luồng thực thi (zero-cost telemetry).

### 16.8. `runtime_config.cpp`
* **Nhiệm vụ**: Đọc/ghi cấu hình INI, quản lý các Brake Profiles (Rifle, Sniper, v.v.). Tự động reload khi có thay đổi trên UI.

## 17. HƯỚNG DẪN SETTING CHUYÊN SÂU

Cấu hình engine quyết định trực tiếp cảm giác bắn (gamefeel). Dưới đây là phân tích cặn kẽ từng tham số:

### 17.1. Overlap Duration
* **Chức năng**: Cửa sổ thời gian (ms) mà hai phím ngược chiều (VD: A và D) được phép "đè" lên nhau trước khi phím cũ thực sự nhả ra.
* **Ý nghĩa Gameplay**: Giúp mô phỏng kỹ năng "Counter-Strafe mượt" của con người. Con người không bao giờ nhả phím A rồi mới bấm phím D trong 0 mili-giây.
* **Quá thấp**: Gây khựng nhân vật (dead-zone). Cảm giác di chuyển bị cắt vụn.
* **Quá cao**: Nhân vật bị trượt (ice-skating) do không cắt động lượng kịp thời.
* **Range an toàn**: `10ms` - `45ms`.

### 17.2. Brake Duration
* **Chức năng**: Thời gian (ms) mà engine giữ phím đối nghịch để hãm phanh.
* **Tương tác**: Kết hợp với Physics Model. Vận tốc càng cao, thời gian tiêm phím càng tiến gần tới `baseDurMs`.
* **Ảnh hưởng**: Thời gian quá ngắn (<15ms) game sẽ bỏ qua (do subtick/tickrate), quá dài (>40ms) nhân vật sẽ bước ngược lại một bước (backstep).
* **Giá trị mặc định**: `20ms` - `25ms` cho súng trường (Rifle).

### 17.3. Tap Strength Multiplier
* **Chức năng**: Hệ số nhân tổng lực phanh.
* **Ý nghĩa Kỹ thuật**: `strength = CalcStopStrength() * tap_strength_multiplier;`
* **Tác động**: `1.0` là chuẩn vật lý. `< 1.0` dùng cho các pha Micro-adjust (chỉnh tâm li ti). `> 1.0` dùng để ép dừng khẩn cấp (Sniper flick).
* **Danger Zone**: Vượt quá `1.3` sẽ gây giật cục cứng nhắc.

### 17.4. Release Curve Exponent
* **Chức năng**: Tạo đường cong phi tuyến tính cho lực phanh dựa trên thời gian phím được giữ.
* **Linear (`1.0`)**: Giữ phím 50% thời gian tối đa -> Lực phanh 50%.
* **Aggressive (`< 1.0`)**: Giữ phím 20% thời gian -> Lực phanh đã lên 80%. Giúp dừng cực nhạy ở cự ly ngắn (Ví dụ: `0.5`).
* **Smooth (`> 1.0`)**: Yêu cầu giữ phím đủ lâu mới có lực phanh mạnh. Thích hợp súng SMG (chạy bắn).

### 17.5. Humanize Range
* **Chức năng**: Gây nhiễu ngẫu nhiên (Jitter injection) vào thời gian hãm phanh.
* **Ý nghĩa**: Biến `20ms` thành `18ms - 22ms`. 
* **Tác dụng**: Chống lại các hệ thống anti-cheat dựa trên phân tích pattern (Anti-pattern avoidance), tăng độ thực tế (realism).

### 17.6. Hardware Debounce
* **Chức năng**: Bỏ qua các tín hiệu siêu ngắn từ nhiễu switch bàn phím cơ.
* **Tương tác**: Nếu bàn phím bị "chatter" (nhảy phím đúp), tính năng này giúp engine không hiểu nhầm đó là một lệnh Strafe mới.

## 18. PROFILE TUNING GUIDE

Hệ thống cho phép cấu hình theo từng kịch bản vũ khí:

1. **LEGIT / OFF**: Trả lại quyền điều khiển hoàn toàn cho hệ điều hành. Dùng khi gõ chat hoặc jump-throw.
2. **RIFLE (Súng trường - AK/M4)**: 
   * *Triết lý*: Đòi hỏi độ dừng chính xác tuyệt đối ngay lập tức để tap headshot.
   * *Thông số*: `BaseBrake: ~25ms`, `Multiplier: 1.15` (Aggressive), `Curve: 0.8` (Snappy).
3. **SNIPER (AWP/Scout)**:
   * *Triết lý*: Flick, dừng, bắn. Cần cắt triệt để trượt băng.
   * *Thông số*: `BaseBrake: ~35ms`, `Multiplier: 1.3`, `Overlap: Thấp`. Độ nhạy trễ (Latency sensitivity) cao nhất.
4. **SMG (Súng tiểu liên - Mac10/MP9)**:
   * *Triết lý*: Run and gun. Chạy bắn là chính, ít khi cần dừng hẳn.
   * *Thông số*: `BaseBrake: ~18ms`, `Curve: 1.2` (Smooth). Cần overlap cao để giữ đà.
5. **PISTOL (Súng lục)**:
   * *Triết lý*: AD-AD liên tục (spam).
   * *Thông số*: Cần loại bỏ Continuity Penalty để phanh dồn dập không bị mất lực. Phanh ngắn (`15-18ms`).

## 19. GAMEPLAY THEORY

Kiến trúc Marco được xây dựng dựa trên đặc thù vật lý của Source Engine (CS2/CS:GO):
* **Friction & Acceleration**: Nhân vật không dừng lại ngay khi thả phím do có ma sát (`sv_friction`) và gia tốc. Để dừng ngay, người chơi phải tạo một vector lực đối nghịch (bấm phím ngược lại).
* **Velocity Cancellation**: Marco thay thế con người bằng cách gửi phím ngược lại với thời gian (Duration) chính xác bằng lượng thời gian cần thiết để vector đối nghịch triệt tiêu vector gia tốc hiện tại.
* **Tại sao Overlap quan trọng?**: Con người thực hiện Jiggle-peek (thò thụt) bằng cách giữ cả hai phím W/D hoặc A/D trong một mili-giây. Nếu engine nhả phím cũ ngay lập tức (0ms overlap), nhân vật sẽ khựng lại ở giữa không trung, làm nát chuyển động.
* **Timing Quantization Issue**: Nếu spam A/D liên tục, hàm `AutoCounterStrafe` bị gọi đè lên nhau, cắt vụn các khoảng 25ms thành 2-3ms, gây mất lực phanh. Đó là lý do Marco phải có luật `Overlap Survival` (băng bó phanh cho đến khi hoàn tất).

## 20. TELEMETRY INTERPRETATION GUIDE

Phân tích Telemetry trên Dashboard là kỹ năng bắt buộc để debug hệ thống:

* **Hook Latency (P50/P99)**: 
  * *Tốt*: `P99 < 15us`. 
  * *Nguy hiểm*: `P99 > 1000us` (1ms). Nếu cao, OS có thể tháo hook. Chạy game ở chế độ Fullscreen Exclusive có thể gây tăng hook latency.
* **Timer Jitter**: 
  * Độ chênh lệch giữa thời gian yêu cầu và thực tế kích hoạt.
  * *Tốt*: `< 50us`.
* **Oversleep**: 
  * *Tốt*: `< 20us`. 
  * *Nguy hiểm*: `> 500us`. Nguyên nhân là luồng Timing bị hệ điều hành tước quyền (Preemption) do có phần mềm khác chiếm CPU.
* **Spin Duration**: Thời gian đốt CPU (`_mm_pause`). Càng cao CPU càng nóng, nhưng độ trễ càng thấp. Thường ở mức `1000us` (1ms).

## 21. TROUBLESHOOTING GUIDE

* **Tình trạng: Counter-strafe có cảm giác yếu, bị trượt xa.**
  * *Nguyên nhân*: Lực phanh không đủ thắng gia tốc server, hoặc game tick rate không nhận diện kịp.
  * *Cách Fix*: Tăng `Base Brake Duration` (lên 25-30ms). Tăng `Tap Strength Multiplier` lên `1.15`.
* **Tình trạng: Oversleep vọt lên >1000us (Màu đỏ trên Dashboard).**
  * *Nguyên nhân*: CPU bị quá tải, Thread priority bị rớt.
  * *Cách Fix*: Đảm bảo Marco chạy dưới quyền Administrator (để cấp quyền MMCSS/Realtime). Đóng bớt trình duyệt/phần mềm nền. Kiểm tra xem SMT Collision có báo `YES` không.
* **Tình trạng: Hook Latency tăng, kẹt phím.**
  * *Nguyên nhân*: Xung đột phần mềm (Discord Overlay, NVIDIA Shadowplay, Anti-virus).
  * *Cách Fix*: Tắt overlay. Thử chuyển `Affinity Mode` để ép lõi.
* **Tình trạng: Engine bị "Conflict State" (nhân vật tự đi bộ).**
  * *Cách Fix*: Bấm `F1` để kích hoạt Emergency Flush (Xả bộ đệm), sau đó nhấp phím di chuyển một lần để đồng bộ lại trạng thái.

## 22. BEST PRACTICES (TỐI ƯU HỆ THỐNG PC)

Để engine chạy mượt nhất ở mức <50us, máy tính cần được tinh chỉnh:
1. **Windows Power Plan**: Chuyển sang `High Performance` hoặc `Ultimate Performance` để tắt CPU C-States (ngăn CPU hạ xung).
2. **Keyboard Polling Rate**: Khuyến nghị dùng bàn phím có Polling Rate `1000Hz` (1ms) trở lên. Tránh các bàn phím Bluetooth.
3. **Raw Input**: Luôn bật Raw Input trong game để bỏ qua gia tốc chuột/bàn phím của Windows.
4. **SMT Recommendations**: Nếu CPU có P-Core và E-Core (Intel Gen 12+), nên cấu hình để Game chạy trên các lõi đầu, Marco chạy trên các lõi P-Cores cuối cùng, hoàn toàn cách ly khỏi E-Cores.

## 23. FULL WORKFLOW

Chu trình chuẩn của một người dùng pro:
1. **Build**: Mở MSYS2, gõ `make release` để tạo ra bản build cực nhẹ và ẩn console.
2. **Deploy**: Chạy `bin/release/marco.exe` dưới quyền Administrator.
3. **Tune**: Vào UI, chọn tab Profile, chuyển qua `RIFLE`. Vào DM/Bot để test cảm giác di chuyển bắn.
4. **Monitor**: Alt-Tab ra màn hình thứ hai, bật sang tab `Analysis`. Bắn spam A/D liên tục và nhìn đồ thị Jitter / Oversleep. Nếu không có cột đỏ nào vọt lên (Spikes), hệ thống đã hoàn toàn ổn định.
5. **Play**: Thu nhỏ cửa sổ UI (chạy ngầm). Engine sẽ tự nhận diện Focus của game và kích hoạt ngầm.
