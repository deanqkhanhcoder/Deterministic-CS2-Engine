# MARCO ENGINE — SYSTEM HANDOVER & CONTEXT GUIDE

Tài liệu bàn giao context kỹ thuật dành cho Kỹ sư và AI Agent tiếp quản dự án.

---

## 1. Mục Tiêu Dự Án (Project Objective)
* **Tên dự án**: `Deterministic-CS2-Engine` (Marco).
* **Mục tiêu**: Xây dựng bộ động cơ Counter-Strafe và Bunnyhop sub-tick hoàn toàn tất định (deterministic), không can thiệp bộ nhớ game (external/hardware injection-level), mô phỏng chuẩn xác 100% toán học Source SDK của Valve.
* **Mô hình triển khai**: Headless Background Daemon (C++20) giao tiếp 2 chiều với Web Dashboard (React + Tailwind CSS) qua Local Loopback IPC (REST & SSE trên cổng `127.0.0.1:47650`).

---

## 2. Kiến Trúc Hệ Thống (Architecture Blueprint)

```text
[Raw Hardware Input / Low-Level Hook]
                   │
                   ▼
       [Input Capture & Focus Gate] (Target: cs2.exe)
                   │
                   ▼
      [State Engine & Semantic Router]
                   │
         ┌─────────┴─────────┐
         ▼                   ▼
[2D Continuous Velocity] [BHOP State Machine]
(PmFriction -> Accelerate)   │
         │                   ▼
         ▼           [Cadence Generator]
[Counter-Strafe Controller]  │
         │                   │
         └─────────┬─────────┘
                   │
                   ▼
     [High-Precision Hybrid Timer]
(QPC + timeBeginPeriod + spin-wait _mm_pause)
                   │
                   ▼
    [SendInput Injection Boundary]
                   ▲
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
[Telemetry Buffer]    [Local IPC Server]
                      (127.0.0.1:47650, REST/SSE)
                              │
                              ▼
                     [React Web Dashboard]
                     (ui/dist served via HTTP)
```

### 2.1 Backend Core Daemon (`marco.exe`)
* **Ngôn ngữ**: C++20, MinGW-w64 (`g++`).
* **Tiến trình**: Daemon không cửa sổ (`-mwindows`), độ ưu tiên `HIGH_PRIORITY_CLASS`, luồng timing ghim cứng CPU affinity.
* **Bộ định thời (Hybrid Timer)**: Kết hợp `timeBeginPeriod(1)` + `QueryPerformanceCounter` (QPC/QPF). Ngủ ngắn (`std::this_thread::sleep_for`) trong giai đoạn đầu, và chuyển sang **busy-wait / spin-wait** (`_mm_pause`) trong 1.5 ms cuối để đạt độ chính xác nhả phím từng microsecond.
* **Local IPC Server**: C++ Winsock socket thuần (`ws2_32`), lắng nghe duy nhất `127.0.0.1:47650`.
  * Hỗ trợ REST API (`GET /api/config`, `POST /api/config`, `POST /api/profile`, `POST /api/safemode`, `POST /api/revert`, `GET /api/token`, `POST /api/quit`).
  * Server-Sent Events (`GET /api/events`) stream telemetry ~30 FPS.
  * Tự sinh token phiên ngẫu nhiên trong `./marco.token`, hỗ trợ header `X-Marco-Token` hoặc `?token=`.

### 2.2 Frontend Web Dashboard (`ui/`)
* **Công nghệ**: React 19, TypeScript, Tailwind CSS, Vite.
* **Chế độ chạy**:
  * Production: Bundle tĩnh xuất ra `ui/dist/`, được `marco.exe` phục vụ trực tiếp tại `http://127.0.0.1:47650`.
  * Development: Chạy Vite dev server tại `http://localhost:5173`.
* **UX/UI**: Phong cách Esports Utility / Wooting Utility tối giản, tương phản cao, số liệu kỹ thuật rõ ràng.

---

## 3. Mô Hình Toán Học Vật Lý (Mathematical Specification)

### 3.1 Thứ tự thực thi vật lý trong từng sub-tick
Tuân thủ tuyệt đối quy chuẩn Source SDK của Valve (`CGameMovement::FullWalkMove`):
1. **Friction trước**:
   $$\text{control} = \max(\text{speed}, sv\_stopspeed)$$
   $$\text{new\_speed} = \max(0, \text{speed} - \text{control} \times sv\_friction \times dt)$$
   Triệt tiêu một phần vận tốc trước khi sinh lực mới.
2. **Accelerate sau**:
   $$\text{currentspeed} = \vec{v} \cdot \vec{w}_{dir}$$
   $$\text{addspeed} = \text{wishspeed} - \text{currentspeed}$$
   Khi bấm phím đối nghịch để phanh, $\text{currentspeed} < 0 \implies \text{addspeed} > \text{wishspeed}$. Toàn bộ gia tốc hãm đạt tối đa $sv\_accelerate \times dt \times \text{wishspeed}$.

### 3.2 Chuẩn hóa thông số súng và ngưỡng Accuracy Threshold
* **Triệt tiêu Overlap**: Đặt `overlap_duration_us = 0 µs` trên tất cả profile. Phím di chuyển nhả ra và phím phanh được nhấn trong cùng một gói `SendInput` để không lãng phí tick gia tốc hãm.
* **Ngưỡng dừng (Accuracy Threshold)**:
  * **Rifle (AK-47 / M4) / Pistol / SMG**: $34.0\text{ u/s}$ (vận tốc đạt độ chính xác bắn đầu tiên).
  * **Sniper (AWP / SSG 08)**: $17.0\text{ u/s}$ (ngưỡng dừng hoàn toàn của súng ngắm).
  * Mô phỏng dừng (`SimulateStopDurationMs`) dừng lặp khi vận tốc giảm dưới ngưỡng này.

---

## 4. Tổng Kết Các Bug Đã Fix & Điểm Lưu Ý Kỹ Thuật

| Lỗi | Nguyên nhân | Cách khắc phục đã triển khai |
|---|---|---|
| **Early Stopping** (Dừng sớm khi còn 88–123 u/s) | Threshold ngắt mô phỏng bị gán $80\text{ u/s}$ (quá cao) khiến phanh chỉ kéo dài 3–4 tick (~47–63 ms). | Chuẩn hóa threshold về $\le 34.0\text{ u/s}$ (Rifle) và $17.0\text{ u/s}$ (Sniper). Đảm bảo thời gian phanh đạt đủ 6–7 tick (~93–109 ms). |
| **Overlapped 16 ms** (Đè 2 phím đối nghịch) | Phím cũ chưa nhả thì phím phanh đã nhấn, làm `wishdir = 0` và game chỉ trượt theo friction tự nhiên. | Triệt tiêu overlap (`0 µs`), nhả phím di chuyển và kích hoạt phím phanh trong cùng một sub-tick batch. |
| **Safe Mode Snapshot Bug** (Mất cấu hình khi tắt Safe Mode) | `rcfg::Sanitize` ghi đè trực tiếp thông số Safe Mode vào `s_baseConfig`, làm clobber vĩnh viễn cấu hình người dùng. | Tách rời `s_userConfig` (cấu hình thật) và `s_snapshotConfig`. `ApplySafeModeOverrides` chỉ overlay lúc publish vào seqlock `s_active`. Bổ sung `RevertToSnapshot()`. |
| **Diagonal Over-Brake** (Giật ngược chiều khi phanh chéo W+A, W+D, S+A, S+D) | Vận tốc chạy chéo bị giới hạn vector bởi $sv\_maxspeed = 250\text{ u/s}$, nên từng trục chỉ đạt $250/\sqrt{2} \approx 176.7\text{ u/s}$. Giả định 1D khiến engine tính dư 1 tick phanh. | Nhận diện trạng thái chạy chéo: Scale vận tốc trục theo hệ số $1/\sqrt{2} \approx 0.707$ và giảm 1 tick (~15.6 ms) brake hold, triệt tiêu hoàn toàn giật ngược. |
| **GDI UI Message Loop Stalls** | Cửa sổ Windows GDI cũ xử lý đồ họa trên cùng thread message pump làm giật hook. | Khai tử hoàn toàn `src/ui/*`, chuyển core sang headless daemon + Local IPC REST/SSE + Web UI độc lập. |
| **Profiles Tab Unresponsive** | UI không khởi tạo default config khi daemon chưa kết nối hoặc token chưa nhập, làm ẩn component profile. | Khởi tạo `DEFAULT_CONFIG` đầy đủ và hỗ trợ auto-fetch `/api/token` từ loopback `127.0.0.1`. |

---

## 5. Hướng Dẫn Build & Chạy Kiểm Thử

### Build Core Daemon & Web UI
```powershell
# 1. Build C++ Release binary (ra ./marco.exe ở thư mục gốc)
mingw32-make release

# 2. Build C++ Debug binary (ra ./marco_debug.exe ở thư mục gốc)
mingw32-make debug

# 3. Build bundle Web UI production (ra ./ui/dist/)
cd ui
npm install
npm run build
cd ..
```

### Chạy Unit & Integration Test Suite (19 tests)
```powershell
cmake -S . -B build/make-tests -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/make-tests --target check --parallel
```
*Tất cả 19 test cases đều phải đạt 100% Passed.*


---

## 6. System Tray & Đóng Gói Windows

1. Chạy `marco.exe`: icon Marco xuất hiện trong System Tray (có thể nằm trong nhóm icon ẩn). Click trái hoặc chọn **Open Dashboard** để mở `http://127.0.0.1:47650` bằng trình duyệt mặc định.
2. Click phải: **Open Dashboard**, **Toggle Engine (Active/Pause)**, **Exit Marco**. Dấu check thể hiện engine đang Active; tooltip chuyển giữa **ACTIVE** và **PAUSED** theo cả tray, Dashboard và hotkey.
3. Đóng tab Dashboard không dừng daemon. **Exit Marco** dùng `WM_CLOSE` → `WM_DESTROY` → cleanup chung; gọi `NIM_DELETE`, chờ IPC/SSE/client workers, đóng Winsock, tháo hooks, dừng bhop/timer/telemetry, nhả synthetic keys còn giữ, khôi phục timer resolution, gỡ exception handler và giải phóng mutex.
4. Khi Explorer restart, `TaskbarCreated` thêm lại icon. Cửa sổ `CS2MsgClass` phải là top-level ẩn, không dùng `HWND_MESSAGE` vì cửa sổ message-only không nhận broadcast này.
5. Đóng gói portable gồm `marco.exe` và toàn bộ `ui/dist/`. `marco.ini` là cấu hình cá nhân; `marco.token` sinh lại mỗi phiên. `resources/resource.rc` và `resources/icon.ico` được compile bởi `windres` trong Makefile và RC language trong CMake; cả Release/Debug link `shell32` và có icon nhúng.

### Các file và kiểm thử liên quan

- `src/core/system_tray.cpp`, `include/core/system_tray.h`: quản lý `NOTIFYICONDATAW`, menu, callback và vòng đời icon; `include/core/resource.h` chứa ID resource/menu.
- `src/core/main.cpp`: nối tray với message pump và cleanup; `WM_TOGGLE_SUSPEND` dùng chung cho tray, hotkey và IPC.
- `src/core/ipc_server.cpp`: worker client có ownership và được chờ trước `WSACleanup`; listener chỉ đóng một lần; socket timeout và `SendMessageTimeoutW` tránh shutdown kẹt ở client.
- `tests/unit/test_system_tray.cpp`: Win32 fakes kiểm tra click, menu, Active/Pause, Explorer restart, lỗi thêm icon/menu và cleanup idempotent; không mở trình duyệt hay phát input thật. Test nằm trong target `check` (19 tests).
- `python tests/integration/test_ipc_dashboard.py`: kiểm thử daemon thật trong thư mục tạm khi port `47650` trống; xác nhận icon nhúng, tray Toggle/Exit (gồm client HTTP chưa gửi đủ body), SSE, Power, profile, SAFE MODE, snapshot, reset và lưu/đọc `MomentumMemoryMs`. Không thuộc suite desktop-safe vì khởi động daemon có hooks thật.

`POST /api/state` nhận `{ "suspended": true/false }` và trả telemetry. `POST /api/config` nhận `activeProfileIndex` (alias của `activeBrakeProfileIndex`), mảng `brakeProfiles` 5 phần tử hoặc patch `profile_1`…`profile_4`. JSON telemetry SSE được xuất một dòng để mỗi sự kiện luôn parse được.


## Release v28 — Telemetry Dashboard

- `ui/src/TelemetryDashboard.tsx`: F-pattern với hero velocity, verdict/duration, P99/core affinity; trace bên trái (~63%), key matrix/timeline bên phải; micro-diagnostics phía dưới.
- `ui/src/telemetry.ts`: lịch sử 100 packet SSE, cửa sổ 3.3s, reset khi uptime daemon giảm; trục Y tối thiểu 250 u/s hoặc 100 µs. Canvas dùng `requestAnimationFrame` và kích thước theo device pixel ratio; ngắt đường khi mất packet >250 ms. Idle không tạo mẫu giả.
- `state_engine.cpp`: publish `VelocityTracker` cùng state, extrapolate bản sao bằng `AdvanceVelocity` tại snapshot; không đọc `s_state.vel` ngoài khóa hoặc sửa state engine từ telemetry. Timing stats được cập nhật cả sau khi timer ngắn đã kết thúc; `timerJitterP99Us` và `timerSampleCount` được xuất qua IPC.
- `lastBrakeMs` / `lastBrakeTicks` giữ phần thập phân (tick equivalents @64 Hz). UI không còn fallback 93 ms / 6 ticks / 215 u/s; timeline chỉ là phase guide.
- Kiểm thử: `cd ui; npm test; npm run build`; `cmake --build build/make-tests --target check`; `mingw32-make release`.


## SOCD Control — FULL / HUMANIZED / OFF và Auto Counter-Strafe

- `SocdMode`: FULL=0, HUMANIZED=1, OFF=2; `RuntimeConfig.socdMode`, REST `socdMode`, INI `[Strafe] socd_mode`. Giá trị `1` cũ của LITE được đọc thành HUMANIZED. Không còn PRNG, xác suất 30% hay settle 30 ms. Enum REST vẫn chỉ nhận integer 0–2; dữ liệu INI không hợp lệ về OFF.
- `ResolveAxis` chỉ xét conflict vật lý và quyết định SOCD ở Key_Down. FULL nhả loser ngay. HUMANIZED dùng timer một lần 8000 µs trong `State.socdReleaseTimerId`, sau đó nhả loser và giữ winner. Callback chỉ thực thi khi timer ID, mode và physical pair còn khớp. Nhả sớm/rebuild/pause hủy timer; admission failure về immediate priority. `axisState` luôn biểu diễn physical state, kể cả callback brake; không suy từ logical output để tránh re-trigger trong sustained hold.
- Auto Counter-Strafe ở Key_Up chỉ xét eligibility routing/focus, physical opposite và velocity/profile; không gate theo SOCD mode hay conflict cache. Trong OFF, hook vẫn queue eligibility cho Auto Counter-Strafe nhưng passthrough WASD. `State.nativeLogical` lưu native output riêng; commit logical bằng native output OR accepted synthetic ownership. Physical key-up của counter key được swallow khi burst cùng target đang giữ; đọc atomic mask và target publication, không lock hook. Burst giữ đúng deadline dù người dùng bấm/nhả counter key giữa chừng.
- Mode change vẫn serialize trên `WM_CONFIG_REQUEST`, drain owner queue, yêu cầu WASD đã nhả, cancel/release owned input, rồi apply config. Revert/Safe Mode dùng cùng guard; HTTP 409 khi không thể chuyển an toàn, 503 khi timeout. Numeric INI/REST ABI giữ nguyên.
- Regression fake backend dùng routing/controller/timer/reconciliation production: A+D và W+S giữ 2000 ms, cả thứ tự nhấn và cả 3 mode; FULL/HUMANIZED giữ last-key wish cố định, OFF wishdir=0 và stationary sau friction; không injection/timer lặp. A→D Key_Up arm burst 94–109 ms ở mọi mode, đưa speed dưới 34 u/s rồi friction về 0; physical counter tap không cắt timer và target khác không swallow. Bao phủ callback cũ, nhả sớm, timer admission failure, cleanup, inactive OFF và 20000 diagonal/focus cycles. Suite CMake `check` gồm 19 tests; INI/IPC bao phủ mode roundtrip, enum validation, snapshot và restart.

## Injection Path Latency

- `MarcoSendInput` chọn export `win32u!NtUserSendInput` resolve một lần qua `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` / `GetProcAddress`; giữ DLL reference tới shutdown, warm resolve trước hot path. Fallback `user32!SendInput` chỉ khi không resolve được. Không hardcode syscall number, không asm, không retry zero/partial-send sang path khác để tránh duplicate input.
- Signature dùng `UINT (NTAPI *)(UINT, LPINPUT, int)`: trả inserted-event count, không phải NTSTATUS. Tham khảo [implementation ABI của Wine](https://github.com/wine-mirror/wine/blob/master/dlls/win32u/input.c) và [SendInput return semantics](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput). Scan-code templates đặt wVk=0, không UNICODE, dwExtraInfo=0; Set 1 W/A/S/D/Space được static_assert.
- Bỏ watermark; giữ một bit test LLKHF_INJECTED / LLMHF_INJECTED do Windows set. Không thay bằng g_injecting: cửa sổ gọi không nhận diện được callback trễ và có thể bỏ phím vật lý đồng thời. Hai path đều là synthetic input và giữ cùng hành vi flags; dwExtraInfo=0 không biến input thành HID. [Microsoft KBDLLHOOKSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct).
- Đo ngày 2026-10-05, Windows 10 Home 22H2 x64 build 19045, `marco_debug.exe --benchmark-injection`. QPC: warmup 1000 cặp call; 9 batch x 20000 call/path, luân phiên thứ tự; avg_ns là median của các batch mean. Zero-input không phát phím vào desktop và chỉ đo dispatch/validation, không đo latency của burst 2–4 event.

```text
[INJ] module=C:\WINDOWS\System32\win32u.dll offset=0x2010 workload=zero-input
[INJ] path=syscall avg_ns=1282.4
[INJ] path=user32 avg_ns=1281.0
[INJ] zero-input dispatch only; does not measure delivered-key latency
```

Trước (user32): 1281.0 ns; sau (native export): 1282.4 ns, chênh +1.4 ns (~0.11%), chưa xác nhận cải thiện. Trên DLL đang cài, `user32!SendInput` là jump thunk và target khớp `win32u!NtUserSendInput`; không có bằng chứng về marshal overhead 15–40% trên build này. Đây là dynamic export dispatch, không phải bypass kernel validation.

Windows 11 23H2 và valid-input/end-to-end benchmark chưa được kiểm tra trên máy này. Build MinGW Release/Debug với -Wall/-Wextra/-pedantic không warning; MSVC /W4 chưa kiểm tra vì không có cl. CMake check: 19/19 tests, gồm timing_lifecycle và physics_fixture_regression. Test fake kiểm tra preferred path, fallback khi export thiếu, zero/partial returns không retry, bảo toàn GetLastError và scan-code/zero-extra-info. PE import audit xác nhận không import tĩnh win32u.dll/NtUserSendInput.

## Release v29 — Dashboard và Injection Path Indicator

### SOCD sustained hold — sửa reconcile ngày 2026-10-06

- FULL/HUMANIZED giữ last-key priority; A → A+D duy trì D, nhả D phục hồi A trong cùng batch. Không có timeout 30 ms tự neutralize; Auto Counter-Strafe chỉ được arm khi cả hai phím vật lý trên trục đã nhả.
- Reconcile cũ press lại phím SOCD bị suppress rồi release ngay, tạo injection thừa mỗi lần rebuild. Reconcile mới tính desired output theo priority trước và chỉ gửi phần khác biệt; giữ deadline HUMANIZED 8 ms và deadline auto-brake đã arm.
- `capture::PhysicalMovementMask()` drain physical edges trên hook-owner thread. Rebuild/focus regain không ghi đè WASD từ `GetAsyncKeyState`, vì API này phản ánh cả key-up synthetic. Snapshot OS chỉ bootstrap WASD một lần trước injection đầu tiên.
- Regression kiểm tra A/D và W/S ở cả hai thứ tự: giữ phím đầu 500 ms, giữ cả hai 2000 ms với reconcile/repeat mỗi 1 ms, nhả winner phục hồi survivor, rồi nhả survivor ngay hoặc sau 500 ms. Assert từng key-down/up, timestamp 8 ms, không schedule brake khi còn held. Code reconcile cũ fail assert timeline; bản mới pass cùng 20000 diagonal/focus cycles.

- `ui/src/index.css` định nghĩa palette sáng/tối qua `--bg`, `--fg`, `--card`, `--border`, `--muted`, `--accent` và màu semantic. Class `dark` trên `<html>`; không glow, neon cyan/amber hay animate-ping. `theme.ts` ưu tiên localStorage `marco_theme`, fallback `prefers-color-scheme`; nút header lưu lựa chọn và tự theo hệ thống khi chưa chọn.
- `DashboardCards.tsx` tách `KPICard`, `ChartCard`, `StatusStrip` với props TypeScript. Telemetry dùng 4 KPI bằng nhau, hai cột chart/matrix ở desktop, một status strip; mobile giữ 2 KPI/cột. Chart giữ canvas mounted qua loading/empty state, palette được cập nhật theo theme trong vòng rAF, không thay đổi sliding buffer hoặc physics.
- `syscall_dispatch.cpp` publish `std::atomic<const char*> g_injection_path` sau resolve đầu tiên bằng release store; IPC acquire load và xuất root field `injection_path: "ntuser" | "user32"` qua REST và mọi event SSE. Không resolve lại từ IPC, không thêm endpoint. Frontend hiển thị native xanh, fallback vàng; chưa có packet thì chờ telemetry.
- Xác minh ngày 2026-10-05: `npm run build` sạch TypeScript; `npm test` 6/6 (history, scale/rate, theme fallback, native/fallback/pending indicator, loading/empty/populated chart); CMake `check` 19/19; `mingw32-make -B release -j4` không compiler warning. Browser QA dùng fixture SSE 30 Hz: light/dark + reload persistence, grid 1280px và 390px không tràn ngang, profile Sniper bind 17 u/s, native/fallback cập nhật qua SSE, console không warning/error. Fixture là dữ liệu kiểm thử, không phải vận tốc đo từ game.
- `python tests/integration/test_ipc_dashboard.py` pass với daemon Release trong thư mục tạm: `injection_path` nhất quán giữa initial SSE, state_changed, POST state và GET telemetry; auth/config/SOCD/INI roundtrip, snapshot, tray toggle/exit và cleanup đều pass. Test yêu cầu không giữ W/A/S/D trong lúc đổi mode; HTTP 409 khi còn held là guard dự kiến, không được vô hiệu hóa.
