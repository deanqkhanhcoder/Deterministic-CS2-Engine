#include "ui_diagnostics.h"
#include "debug_logger.h"
#include "timing.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <cstdio>
#include <cassert>
#include <dbghelp.h>
#include "workspace.h"

namespace ui_diagnostics {

std::atomic<uint64_t> g_uiHeartbeatUs{0};
thread_local int g_paintDepth = 0;

static std::atomic<int> s_dashScrollY{0};
static std::atomic<int> s_dashViewportH{0};
static std::atomic<int> s_dashTotalH{0};

static std::atomic<bool> s_watchdogRunning{false};
static std::thread s_watchdogThread;

static std::atomic<uint32_t> s_invalidates{0};
static std::atomic<uint32_t> s_invalidatesPerSec{0};
static std::atomic<uint64_t> s_lastInvalidateSec{0};

static std::atomic<uint32_t> s_scrollCount{0};

static std::atomic<uint64_t> s_lastPaintStart{0};
static std::atomic<uint64_t> s_paintTimeUs{0};

static std::atomic<uint64_t> s_lastDispatchStart{0};
static std::atomic<uint64_t> s_dispatchTimeUs{0};

static std::atomic<uint64_t> s_layoutTimeUs{0};

static void SaveScreenshot(const char* filepath) {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(NULL);
    if (!hScreen) return;
    
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, w, h);
    HGDIOBJ old_obj = SelectObject(hDC, hBitmap);

    BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = w * h * 4;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    std::vector<BYTE> pixels(bi.biSizeImage);
    GetDIBits(hDC, hBitmap, 0, h, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    FILE* f = fopen(filepath, "wb");
    if (f) {
        BITMAPFILEHEADER bmfHeader;
        bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        bmfHeader.bfSize = bmfHeader.bfOffBits + bi.biSizeImage;
        bmfHeader.bfType = 0x4D42; // BM
        bmfHeader.bfReserved1 = 0;
        bmfHeader.bfReserved2 = 0;

        fwrite(&bmfHeader, sizeof(BITMAPFILEHEADER), 1, f);
        fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, f);
        fwrite(pixels.data(), 1, bi.biSizeImage, f);
        fflush(f);
        fclose(f);
    }

    SelectObject(hDC, old_obj);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);
}

static void WriteFreezeLog(uint64_t hbUs, uint64_t nowUs) {
    workspace::EnsureLogDirectoryExists();
    std::string root = workspace::GetLogRootA();

    char buf[2048];
    sprintf(buf, 
        "\n======================================================\n"
        "[WATCHDOG] UI THREAD STALL DETECTED\n"
        "Timestamp: %llu us\n"
        "Heartbeat Age: %llu ms\n"
        "GDI Object Count: %lu\n"
        "Paint Depth: %d\n"
        "InvalidateRect Rate: %u/sec\n"
        "Last Paint Duration: %llu us\n"
        "Last Layout Duration: %llu us\n"
        "Dashboard Scroll Y: %d\n"
        "Dashboard Viewport H: %d\n"
        "Dashboard Total H: %d\n"
        "======================================================\n",
        nowUs,
        (nowUs - hbUs) / 1000,
        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
        g_paintDepth,
        s_invalidatesPerSec.load(),
        s_paintTimeUs.load(),
        s_layoutTimeUs.load(),
        s_dashScrollY.load(),
        s_dashViewportH.load(),
        s_dashTotalH.load()
    );

    // 1. Log Tab Output
    DLOG_ERR(Watchdog, "%s", reinterpret_cast<int64_t>(buf));

    // 2. OutputDebugString
    OutputDebugStringA(buf);

    // 3. File Logging
    std::string logPath = root + "ui_watchdog.log";
    FILE* f = fopen(logPath.c_str(), "ab");
    if (f) {
        fwrite(buf, 1, strlen(buf), f);
        fflush(f);
        fclose(f);
    }

    // 4. Screenshot Capture
    std::string bmpPath = root + "freeze_capture.bmp";
    SaveScreenshot(bmpPath.c_str());

    // 5. Minidump Generation
    std::string dmpPath = root + "ui_freeze.dmp";
    HANDLE hFile = CreateFileA(dmpPath.c_str(), GENERIC_READ | GENERIC_WRITE, 
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType, nullptr, nullptr, nullptr);
        CloseHandle(hFile);
    }
}

static void WatchdogLoop() {
    DLOG_INFO(Watchdog, "Watchdog thread started");
    while (s_watchdogRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        uint64_t nowUs = timing::NowUs();
        uint64_t hbUs = g_uiHeartbeatUs.load(std::memory_order_relaxed);
        
        if (hbUs > 0 && (nowUs - hbUs) > 2000000) { // 2000ms
            WriteFreezeLog(hbUs, nowUs);
            
            // We only dump once per stall to avoid log spam, so reset heartbeat until UI recovers
            g_uiHeartbeatUs.store(nowUs); 
        }
        
        // Reset invalidate per sec counter
        uint64_t sec = nowUs / 1000000;
        if (sec > s_lastInvalidateSec.load()) {
            s_lastInvalidateSec.store(sec);
            s_invalidatesPerSec.store(s_invalidates.exchange(0));
            if (s_invalidatesPerSec.load() > 100) {
                DLOG_WARN(Watchdog, "INVALIDATE RECT STORM: %u/sec", s_invalidatesPerSec.load());
            }
        }
    }
    DLOG_INFO(Watchdog, "Watchdog thread stopped");
}

void StartWatchdog() {
    if (s_watchdogRunning) return;
    s_watchdogRunning = true;
    s_watchdogThread = std::thread(WatchdogLoop);
}

void StopWatchdog() {
    if (!s_watchdogRunning) return;
    s_watchdogRunning = false;
    if (s_watchdogThread.joinable()) {
        s_watchdogThread.join();
    }
}

void StartPaint() {
    g_paintDepth++;
    if (g_paintDepth > 1) {
        DLOG_ERR(Watchdog, "PAINT RECURSION DETECTED! Depth: %d", g_paintDepth);
    }
    assert(g_paintDepth == 1 && "Nested paints are not allowed!");
    s_lastPaintStart = timing::NowUs();
}

void EndPaint() {
    g_paintDepth--;
    uint64_t dur = timing::NowUs() - s_lastPaintStart.load();
    s_paintTimeUs.store(dur);
}

void StartDispatch() {
    s_lastDispatchStart = timing::NowUs();
}

void EndDispatch() {
    uint64_t dur = timing::NowUs() - s_lastDispatchStart.load();
    s_dispatchTimeUs.store(dur);
    if (dur > 16000) { // 16ms
        DLOG_WARN(Watchdog, "Message dispatch latency spike: %llu ms", dur / 1000);
    }
}

void TrackMeasureLayout(uint64_t durationUs) {
    s_layoutTimeUs.store(durationUs);
}

void TrackScroll() {
    s_scrollCount++;
}

void TrackInvalidate(HWND hwnd, const RECT* lpRect, BOOL bErase) {
    s_invalidates++;
    ::InvalidateRect(hwnd, lpRect, bErase);
}

void PaintOverlay(HDC hdc, RECT rc) {
    SetBkMode(hdc, OPAQUE);
    
    uint64_t nowUs = timing::NowUs();
    uint64_t hbUs = g_uiHeartbeatUs.load(std::memory_order_relaxed);
    uint64_t ageMs = (nowUs - hbUs) / 1000;
    
    COLORREF statusColor = RGB(0, 255, 0); // Green
    const char* statusText = "HEALTHY";
    if (ageMs > 1500) {
        statusColor = RGB(255, 0, 0); // Red
        statusText = "STALLED";
    } else if (ageMs > 500) {
        statusColor = RGB(255, 255, 0); // Yellow
        statusText = "SLOW";
    }

    SetBkColor(hdc, RGB(0, 0, 0));
    SetTextColor(hdc, statusColor);
    
    // Quick font for overlay
    HFONT hFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    DWORD gdiCount = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);

    char buf[512];
    sprintf(buf, 
        "=== DIAGNOSTICS ===\n"
        "Watchdog: %s\n"
        "Heartbeat: %llu ms\n"
        "Paint Time: %llu us\n"
        "Layout Time: %llu us\n"
        "Dispatch Time: %llu us\n"
        "Invals/sec: %u\n"
        "GDI Objects: %lu\n"
        "Scroll Evts: %u", 
        statusText,
        ageMs,
        s_paintTimeUs.load(),
        s_layoutTimeUs.load(),
        s_dispatchTimeUs.load(),
        s_invalidatesPerSec.load(),
        gdiCount,
        s_scrollCount.load());

    RECT rText = { rc.right - 200, rc.bottom - 150, rc.right - 10, rc.bottom - 10 };
    DrawTextA(hdc, buf, -1, &rText, DT_LEFT | DT_TOP);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

void SetDashboardScrollState(int scrollY, int viewportH, int totalH) {
    s_dashScrollY.store(scrollY);
    s_dashViewportH.store(viewportH);
    s_dashTotalH.store(totalH);
}

} // namespace ui_diagnostics
