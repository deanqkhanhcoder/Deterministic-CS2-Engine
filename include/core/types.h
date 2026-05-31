#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Type Definitions & Key Mappings         ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include <cstdint>
#include <windows.h>

// ── Key enum: W=0, S=1, A=2, D=3, Mouse1=4 ──
enum class Key : uint8_t { W = 0, S = 1, A = 2, D = 3, Mouse1 = 4, COUNT = 5 };

// ── Axis enum ──
enum class Axis : uint8_t { X = 0, Y = 1, COUNT = 2 };

// ── Axis state (mirrors AHK §5) ──
enum class AxisState : uint8_t { None, Positive, Negative, Conflict };

// ── Release reason (mirrors AHK §6) ──
enum class ReleaseReason : uint8_t { Normal, Conflict, Redundant, Stale };

// ── Axis direction ──
enum class AxisDir : uint8_t { None, Positive, Negative };

// ── Compile-time mapping tables ──
namespace keymap {

// Opposite key: W↔S, A↔D
constexpr Key Opposite[] = {
    Key::S,  // W → S
    Key::W,  // S → W
    Key::D,  // A → D
    Key::A,  // D → A
    Key::Mouse1, // Mouse1 -> Mouse1 (no opposite)
};

// Which axis each key belongs to
constexpr Axis KeyAxis[] = {
    Axis::Y,  // W
    Axis::Y,  // S
    Axis::X,  // A
    Axis::X,  // D
    Axis::X,  // Mouse1 (dummy)
};

// Note: AxisDir per key is derived inline in state_engine.cpp via
// (newState == AxisState::Positive) ? AxisDir::Positive : AxisDir::Negative
// No lookup table needed.

// Positive key per axis
constexpr Key AxisPosKey[] = {
    Key::D,  // X axis positive = D
    Key::W,  // Y axis positive = W
};

// Negative key per axis
constexpr Key AxisNegKey[] = {
    Key::A,  // X axis negative = A
    Key::S,  // Y axis negative = S
};

// Virtual key codes for WASD
constexpr WORD VkCode[] = {
    0x57,  // W
    0x53,  // S
    0x41,  // A
    0x44,  // D
    0x01,  // Mouse1 (VK_LBUTTON)
};

// Scan codes for WASD (used for KEYEVENTF_SCANCODE — better game compat)
constexpr WORD ScanCode[] = {
    0x11,  // W
    0x1F,  // S
    0x1E,  // A
    0x20,  // D
    0x00,  // Mouse1 (no scan code)
};

// Key names for debug logging
constexpr const char* KeyName[] = { "W", "S", "A", "D", "M1" };
constexpr const char* AxisName[] = { "X", "Y" };
constexpr const char* AxisStateName[] = { "NONE", "POSITIVE", "NEGATIVE", "CONFLICT" };
constexpr const char* DirName[] = { "NONE", "POSITIVE", "NEGATIVE" };

} // namespace keymap

// ── Helper: integer index from enum ──
constexpr int ki(Key k) { return static_cast<int>(k); }
constexpr int ai(Axis a) { return static_cast<int>(a); }

// ── Custom message IDs ──
constexpr UINT WM_TIMER_EXPIRED   = WM_APP + 1;
constexpr UINT WM_TOGGLE_SUSPEND  = WM_APP + 2;
constexpr UINT WM_BHOP_TOGGLE     = WM_APP + 3;
constexpr UINT WM_BHOP_CYCLE_MODE = WM_APP + 4;
constexpr UINT WM_CYCLE_PROFILE   = WM_APP + 5;
constexpr UINT WM_TRAY_CALLBACK   = WM_APP + 6;
constexpr UINT WM_UI_REFRESH      = WM_APP + 7;
constexpr UINT WM_STATE_DIRTY     = WM_APP + 8;
constexpr UINT WM_TARGET_REFRESH_REQUEST = WM_APP + 9;
constexpr UINT WM_EMERGENCY_UNHOOK = WM_APP + 10;
constexpr UINT WM_ANALYSIS_ETW_START_FAILED = WM_APP + 11;
