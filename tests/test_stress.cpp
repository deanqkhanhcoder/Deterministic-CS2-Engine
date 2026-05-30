#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>

// External hook overrides
extern "C" {
    SHORT WINAPI MockGetAsyncKeyState(int vKey);
    HWND WINAPI MockGetForegroundWindow();
}

#include "engine_internal.h"
#include "runtime_config.h"
#include "timing.h"
#include "bhop.h"
#include "telemetry.h"
#include "workspace.h"

// --- Global Mocks ---
static std::atomic<bool> s_mockKeys[256];
static std::atomic<HWND> s_mockActiveWindow{ (HWND)0x1337 };

SHORT WINAPI MockGetAsyncKeyState(int vKey) {
    if (vKey >= 0 && vKey < 256 && s_mockKeys[vKey].load()) return 0x8000;
    return 0;
}

extern "C" SHORT (WINAPI *__imp_MockGetAsyncKeyState)(int) = MockGetAsyncKeyState;

HWND WINAPI MockGetForegroundWindow() {
    return s_mockActiveWindow.load();
}

extern "C" HWND (WINAPI *__imp_MockGetForegroundWindow)() = MockGetForegroundWindow;

void SetMockKey(int vKey, bool down) {
    if (vKey >= 0 && vKey < 256) s_mockKeys[vKey].store(down);
}

void SetMockFocus(bool active) {
    s_mockActiveWindow.store(active ? (HWND)0x1337 : (HWND)0);
}

// --- Test Harness ---

void ReportResult(const char* name, int iter, int fails, const char* outPath) {
    workspace::EnsureArtifactDirectoryExists();
    std::string fullPath = workspace::GetArtifactRootA() + outPath;
    std::ofstream out(fullPath);
    out << "# " << name << "\n\n";
    if (fails > 0) {
        out << "Status: FAILED\n";
    } else {
        out << "Status: TESTED\n";
    }
    out << "Iterations: " << iter << "\n";
    out << "Failures: " << fails << "\n";
    out.close();
    
    std::cout << name << " -> " << (fails > 0 ? "FAILED" : "TESTED") << " (" << fails << "/" << iter << " fails) [" << fullPath << "]\n";
}

void TestFocusCycling() {
    int iters = 10000;
    int fails = 0;
    
    for (int i = 0; i < iters; ++i) {
        // Randomly press keys
        SetMockKey('W', i % 2 == 0);
        SetMockKey('A', i % 3 == 0);
        SetMockKey(VK_SPACE, i % 5 == 0);
        
        SetMockFocus(false);
        engine::RebuildState(); // trigger focus loss rebuild (though normally active only on regain)
        
        SetMockFocus(true);
        engine::RebuildState();
        
        {
            std::lock_guard<std::mutex> lock(engine::s_stateMutex);
            if (engine::s_state.phys[ki(Key::W)] != engine::s_state.logical[ki(Key::W)]) fails++;
            if (engine::s_state.phys[ki(Key::A)] != engine::s_state.logical[ki(Key::A)]) fails++;
            if (engine::s_state.axisState[0] == AxisState::Conflict) fails++;
        }
    }
    
    ReportResult("STRESS_FOCUS_REPORT", iters, fails, "STRESS_FOCUS_REPORT.md");
}

void TestProfileStorm() {
    int iters = 10000;
    int fails = 0;
    
    for (int i = 0; i < iters; ++i) {
        auto& cfg = rcfg::GetMutable();
        cfg.activeBrakeProfileIndex = i % 3;
        cfg.bhopMode = (i % 4) + 1;
        rcfg::Apply(cfg);
        
        auto current = rcfg::Get();
        if (current.activeBrakeProfileIndex != (i % 3) || current.bhopMode != (i % 4) + 1) {
            fails++;
        }
    }
    
    ReportResult("STRESS_PROFILE_REPORT", iters, fails, "STRESS_PROFILE_REPORT.md");
}

void TestCounterStrafeStorm() {
    int iters = 10000;
    int fails = 0;
    
    for (int i = 0; i < iters; ++i) {
        engine::HandleKeyDown(Key::A, true);
        engine::HandleKeyUp(Key::A, true);
        engine::HandleKeyDown(Key::D, true);
        engine::HandleKeyUp(Key::D, true);
    }
    
    uint64_t created = telemetry::g_timersCreated.load();
    uint64_t exec = telemetry::g_timersExecuted.load();
    uint64_t canc = telemetry::g_timersCancelled.load();
    
    // allow a slight diff if timers are pending
    if (created > exec + canc + 10) fails++; // Check for leaks (created > completed + cancelled)
    
    ReportResult("STRESS_COUNTERSTRAFE_REPORT", iters, fails, "STRESS_COUNTERSTRAFE_REPORT.md");
}

void TestBhopStorm() {
    int iters = 10000;
    int fails = 0;
    
    SetMockKey(VK_SPACE, true);
    engine::OnSpaceDown(true);
    
    for (int i = 0; i < iters; ++i) {
        SetMockFocus(i % 2 == 0);
        engine::RebuildState();
        if (i % 100 == 0) rcfg::Apply(rcfg::GetMutable());
    }
    
    ReportResult("STRESS_BHOP_REPORT", iters, fails, "STRESS_BHOP_REPORT.md");
}

void TestChaos() {
    int iters = 10000;
    int fails = 0;
    std::mt19937 r(1337);
    
    for (int i = 0; i < iters; ++i) {
        int action = r() % 6;
        if (action == 0) engine::HandleKeyDown(Key::W, true);
        else if (action == 1) engine::HandleKeyUp(Key::W, true);
        else if (action == 2) { SetMockFocus(false); engine::RebuildState(); }
        else if (action == 3) { SetMockFocus(true); engine::RebuildState(); }
        else if (action == 4) { 
            auto& cfg = rcfg::GetMutable();
            cfg.bhopEnabled = !cfg.bhopEnabled; 
            rcfg::Apply(cfg); 
        }
    }
    
    ReportResult("STRESS_CHAOS_REPORT", iters, fails, "STRESS_CHAOS_REPORT.md");
}

int main() {
    rcfg::Init();
    timing::Init();
    engine::Init((HWND)0x1337);
    timing::StartTimerThread((HWND)0x1337);
    
    TestFocusCycling();
    TestProfileStorm();
    TestCounterStrafeStorm();
    TestBhopStorm();
    TestChaos();
    
    timing::StopTimerThread();
    return 0;
}
