// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Input Injection Implementation          ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "injection.h"
#include "debug_logger.h"
#include "timing.h"
#include <windows.h>

namespace injection {

// Pre-built INPUT structs for each key (avoid construction overhead)
static INPUT s_keyDown[4];
static INPUT s_keyUp[4];
static bool  s_initialized = false;

static void EnsureInit() {
    if (s_initialized) return;
    s_initialized = true;

    for (int i = 0; i < 4; ++i) {
        // Key down
        s_keyDown[i] = {};
        s_keyDown[i].type           = INPUT_KEYBOARD;
        s_keyDown[i].ki.wVk         = keymap::VkCode[i];
        s_keyDown[i].ki.wScan       = keymap::ScanCode[i];
        s_keyDown[i].ki.dwFlags     = KEYEVENTF_SCANCODE;
        s_keyDown[i].ki.time        = 0;
        s_keyDown[i].ki.dwExtraInfo = 0x1337BEEF; // Marco magic value

        // Key up
        s_keyUp[i] = {};
        s_keyUp[i].type           = INPUT_KEYBOARD;
        s_keyUp[i].ki.wVk         = keymap::VkCode[i];
        s_keyUp[i].ki.wScan       = keymap::ScanCode[i];
        s_keyUp[i].ki.dwFlags     = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        s_keyUp[i].ki.time        = 0;
        s_keyUp[i].ki.dwExtraInfo = 0x1337BEEF; // Marco magic value
    }
}

void KeyDown(Key k) {
    EnsureInit();
    int idx = ki(k);
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_BEGIN key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    int64_t preSyscall = timing::NowUs();
    UINT sent = SendInput(1, &s_keyDown[idx], sizeof(INPUT));
    int64_t postSyscall = timing::NowUs();
    DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_SYSCALL_US duration=%lld", (postSyscall - preSyscall));
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_END key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    if (sent == 0) {
        DLOG_ERR(Injection, "SendInput FAILED for %s DOWN (err=%lu)", reinterpret_cast<int64_t>(keymap::KeyName[idx]), GetLastError());
    }
    DLOG_INFO(Injection, "Inject %s DOWN", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
}

void KeyUp(Key k) {
    EnsureInit();
    int idx = ki(k);
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_BEGIN key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    int64_t preSyscall = timing::NowUs();
    UINT sent = SendInput(1, &s_keyUp[idx], sizeof(INPUT));
    int64_t postSyscall = timing::NowUs();
    DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_SYSCALL_US duration=%lld", (postSyscall - preSyscall));
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_END key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    if (sent == 0) {
        DLOG_ERR(Injection, "SendInput FAILED for %s UP (err=%lu)", reinterpret_cast<int64_t>(keymap::KeyName[idx]), GetLastError());
    }
    DLOG_INFO(Injection, "Inject %s UP", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
}

void KeyDownUp(Key k) {
    EnsureInit();
    int idx = ki(k);
    INPUT batch[2] = { s_keyDown[idx], s_keyUp[idx] };
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_BEGIN key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    int64_t preSyscall = timing::NowUs();
    UINT sent = SendInput(2, batch, sizeof(INPUT));
    int64_t postSyscall = timing::NowUs();
    DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_SYSCALL_US duration=%lld", (postSyscall - preSyscall));
    DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_END key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    if (sent < 2) {
        DLOG_ERR(Injection, "SendInput FAILED for %s DOWN+UP (sent=%u, err=%lu)", reinterpret_cast<int64_t>(keymap::KeyName[idx]), sent, GetLastError());
    }
    DLOG_INFO(Injection, "Inject %s DOWN+UP", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
}

void SendBatch(INPUT* inputs, int count) {
    if (count > 0) {
        DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_BEGIN count=%d", count);
        int64_t preSyscall = timing::NowUs();
        UINT sent = SendInput(count, inputs, sizeof(INPUT));
        int64_t postSyscall = timing::NowUs();
        DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_SYSCALL_US duration=%lld", (postSyscall - preSyscall));
        DLOG_INFO(Injection, "[FIRE_TRACE] phase=SENDINPUT_DISPATCH_END count=%d", count);
        if (sent < (UINT)count) {
            DLOG_ERR(Injection, "SendInput batch FAILED (requested=%d sent=%u err=%lu)", count, sent, GetLastError());
        }
    }
}

} // namespace injection
