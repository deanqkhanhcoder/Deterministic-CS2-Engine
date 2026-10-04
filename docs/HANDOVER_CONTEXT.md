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

### Chạy Unit & Integration Test Suite (18 tests)
```powershell
cmake -S . -B build/make-tests -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/make-tests --target check --parallel
```
*Tất cả 18 test cases đều phải đạt 100% Passed.*
