# THREAD OWNERSHIP MAP (MARCO V27.4)

## 1. Thread Definitions & Lifecycles

| Thread Name | Origin / Spawner | Priority | Pinned / Topology | Primary Role |
|---|---|---|---|---|
| **UI Thread (Main)** | OS `WinMain` | Normal | `PinUIThread()` | Xử lý Window Message (`WndProc`), Settings UI, Dashboard, Trigger Apply Config. |
| **Hook Thread** | OS (Global Hooks) | High (OS level) | (Dynamic, but tracked) | Xử lý Callback `KeyboardProc`, `MouseProc`. Ghi đè vào Game State. |
| **Focus Thread** | `input_capture.cpp: Install()` | Normal | `PinBackgroundThread()` | Bắt sự kiện `EVENT_SYSTEM_FOREGROUND` qua `WinEventProc`. Theo dõi Active Window. |
| **Resolver Thread** | `target_platform.cpp: Init()` | Normal | `PinBackgroundThread()` | Xử lý hàng đợi `s_resolverQueue` (Target Identity). Phân tích PID/HWND và xuất bản kết quả. |
| **Timer Thread** | `timing.cpp: StartTimerThread()` | Critical | `PinCriticalThread(L"Pro Audio")` | Đếm giờ microsecond bằng QPC, inject các sự kiện KeyUp (Counter-Strafe release). |
| **BHOP Thread** | `bhop.cpp: Init()` | Critical | `PinCriticalThread(L"Pro Audio")` | Loop chờ Space Bar để inject `VK_SPACE` xuống game (Bhop simulation). |
| **Watchdog Thread** | `state_engine.cpp: StartWatchdog()` | Normal | `PinBackgroundThread()` | Check timeouts, heartbeats, trigger FailSafe/Emergency Flush nếu các thread khác bị block. |
| **Scanner Thread** | `target_platform.cpp: Init()` | Normal | `PinBackgroundThread()` | Duyệt Process List 2s/lần tìm CS2/Roblox đang chạy. |

## 2. Lock & Mutex Hierarchy

*   **`s_focusMutex`** (`input_capture.cpp`)
    *   **Bảo vệ**: Biến tracking Focus cục bộ `s_cachedIdentity`, `s_lastEvaluatedFg`.
    *   **Owner**: Focus Thread, UI Thread (khi lấy UI Active State), Hook Thread.
*   **`s_stateMutex`** (`state_engine.cpp`)
    *   **Bảo vệ**: Khối dữ liệu VẬT LÝ & LOGIC chung (`s_state`).
    *   **Owner**: Hook Thread (`HandleKeyDown`/`Up`), Timer Thread (`OnTimerExpired`), Focus Thread (`RebuildState`, `ClearHeldKeys`), UI Thread (Watchdog/Clear).
*   **`s_resolverMutex`** (`target_platform.cpp`)
    *   **Bảo vệ**: Hàng đợi phân giải `s_resolverQueue`, `s_queueTail`, `s_queueCount`.
    *   **Owner**: Hook/Focus Thread (khi `ResolveTargetAsync`), Resolver Thread (khi Pop).
*   **`s_spinlock`** (`timing.cpp`)
    *   **Bảo vệ**: Mảng timer `s_slots`, `s_nextTimerId`, `s_activeMask`.
    *   **Owner**: Timer Thread, Hook Thread (khi `ScheduleTimerUs` hoặc `CancelTimer`).
*   **`bhop::s_mutex`** (`bhop.cpp`)
    *   **Bảo vệ**: State `s_spaceHeld`, Condition Variable predicate.
    *   **Owner**: BHOP Thread, Hook Thread (`OnSpaceDown`/`Up`), Focus Thread (`ForceSpaceSync`).

> **Safety Verdict**: KHÔNG TÌM THẤY CROSS-LOCK DEADLOCK. Code luôn lấy lock đơn (Single-lock leaf nodes) và trả ngay lập tức. Không có hiện tượng lồng lock (Nested Locks) giữa các subsystem.

## 3. Atomic & Lock-Free Publication Paths

*   **Runtime Config (`rcfg::s_seq`)**
    *   **Mô hình**: Seqlock Write-Release / Read-Acquire.
    *   **Ghi**: UI Thread (`Apply()`).
    *   **Đọc**: BẤT KỲ Thread nào (`rcfg::Get()`).
*   **Engine State (`engine::s_pubState.seq`)**
    *   **Mô hình**: Seqlock Write-Release / Read-Acquire.
    *   **Ghi**: Bất cứ Thread nào cầm `s_stateMutex` và gọi `PublishEngineState()`.
    *   **Đọc**: UI Thread (`TakeSnapshot()`).
*   **Target Identity (`target_platform::s_pubSeq`)**
    *   **Mô hình**: Seqlock Write-Release / Read-Acquire.
    *   **Ghi**: Resolver Thread (`ResolverWorker()`).
    *   **Đọc**: Hook Thread, Focus Thread, UI Thread.
*   **Timer Dirty Flag (`timing::s_dirty`)**
    *   **Mô hình**: Atomic Bool `memory_order_relaxed`.
    *   **Sử dụng**: Ngắt vòng lặp spin `_mm_pause()` của Timer Thread để kiểm tra lại slot.