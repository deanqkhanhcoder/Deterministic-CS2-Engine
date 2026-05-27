// ╔══════════════════════════════════════════════════════════════════════╗
// ║  CS2 Macro Suite — Config I/O Implementation                        ║
// ║  Uses Windows INI API (GetPrivateProfileInt/WritePrivateProfileString) ║
// ║  Zero external dependencies                                         ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "config_io.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include "debug_logger.h"

namespace config_io {

static wchar_t s_iniPath[MAX_PATH] = {};
static std::atomic<bool> s_shutdownBarrier{false};
static std::atomic<uint64_t> s_persistenceGeneration{0};

void SetShutdownBarrier() {
    s_shutdownBarrier.store(true, std::memory_order_seq_cst);
    DLOG_INFO(Config, "[SHUTDOWN] [PERSIST_BARRIER] Persistence barrier activated.");
}

static void EnsurePath() {
    if (s_iniPath[0]) return;
    GetModuleFileNameW(nullptr, s_iniPath, MAX_PATH);
    // Replace .exe with .ini
    wchar_t* dot = wcsrchr(s_iniPath, L'.');
    if (dot) wcscpy(dot, L".ini");
    else wcscat(s_iniPath, L".ini");
}

const wchar_t* GetConfigPath() {
    EnsurePath();
    return s_iniPath;
}

// ── Helpers ──
static int ReadInt(const wchar_t* section, const wchar_t* key, int def) {
    return GetPrivateProfileIntW(section, key, def, s_iniPath);
}

static double ReadDbl(const wchar_t* section, const wchar_t* key, double def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(section, key, L"", buf, 64, s_iniPath);
    if (buf[0] == 0) return def;
    return _wtof(buf);
}

static bool ReadBool(const wchar_t* section, const wchar_t* key, bool def) {
    return GetPrivateProfileIntW(section, key, def ? 1 : 0, s_iniPath) != 0;
}

static void WriteInt(const wchar_t* section, const wchar_t* key, int val) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", val);
    WritePrivateProfileStringW(section, key, buf, s_iniPath);
}

static void WriteDbl(const wchar_t* section, const wchar_t* key, double val) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.6g", val);
    WritePrivateProfileStringW(section, key, buf, s_iniPath);
}

static void WriteBool(const wchar_t* section, const wchar_t* key, bool val) {
    WritePrivateProfileStringW(section, key, val ? L"1" : L"0", s_iniPath);
}

// ════════════════════════════════════════════════════════════════
//  LOAD
// ════════════════════════════════════════════════════════════════
bool Load(RuntimeConfig& c) {
    EnsurePath();

    // Check if file exists
    DWORD attr = GetFileAttributesW(s_iniPath);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;

    const wchar_t* S = L"Strafe";
    c.quickTapMs       = ReadInt(S, L"QuickTapMs", c.quickTapMs);
    c.maxScaleMs       = ReadInt(S, L"MaxScaleMs", c.maxScaleMs);
    c.crouchMult       = ReadDbl(S, L"CrouchMult", c.crouchMult);
    c.tapDelayMs       = ReadInt(S, L"TapDelayMs", c.tapDelayMs);
    c.sprayDelayMs     = ReadInt(S, L"SprayDelayMs", c.sprayDelayMs);
    c.burstThreshold   = ReadInt(S, L"BurstThreshold", c.burstThreshold);
    c.spaceDelayMs     = ReadInt(S, L"SpaceDelayMs", c.spaceDelayMs);
    c.latencyMarginMs  = std::clamp(ReadInt(S, L"LatencyMarginMs", c.latencyMarginMs), 0, 100);
    c.minStopMs        = ReadInt(S, L"MinStopMs", c.minStopMs);
    c.lutMaxMs         = ReadInt(S, L"LutMaxMs", c.lutMaxMs);
    c.subtickPaddingTicks = ReadDbl(S, L"SubtickPaddingTicks", c.subtickPaddingTicks);
    c.walkMemoryMs     = ReadInt(S, L"WalkMemoryMs", c.walkMemoryMs);
    c.walkRatioSkip    = ReadDbl(S, L"WalkRatioSkip", c.walkRatioSkip);
    c.walkRatioLight   = ReadDbl(S, L"WalkRatioLight", c.walkRatioLight);
    c.minWalkStopMs    = ReadInt(S, L"MinWalkStopMs", c.minWalkStopMs);
    c.walkMaxStopMs    = ReadInt(S, L"WalkMaxStopMs", c.walkMaxStopMs);
    c.decayK           = ReadDbl(S, L"DecayK", c.decayK);
    c.dirChangePenaltyMs= ReadInt(S, L"DirChangePenaltyMs", c.dirChangePenaltyMs);
    c.tapSpamWindowMs  = ReadInt(S, L"TapSpamWindowMs", c.tapSpamWindowMs);
    c.stopStrengthMin  = ReadDbl(S, L"StopStrengthMin", c.stopStrengthMin);
    c.tapSpamAlpha     = ReadDbl(S, L"TapSpamAlpha", c.tapSpamAlpha);
    c.tapSpamHalfLifeMs= ReadInt(S, L"TapSpamHalfLifeMs", c.tapSpamHalfLifeMs);
    c.minTapUs         = std::clamp((int64_t)ReadInt(S, L"MinTapUs", (int)c.minTapUs), (int64_t)0, (int64_t)100000);
    c.conflictIncrement= ReadDbl(S, L"ConflictIncrement", c.conflictIncrement);
    c.conflictDecrement= ReadDbl(S, L"ConflictDecrement", c.conflictDecrement);
    c.conflictHalfLifeMs= ReadInt(S, L"ConflictHalfLifeMs", c.conflictHalfLifeMs);

    c.hardwareDebounceUs = ReadInt(S, L"HardwareDebounceUs", c.hardwareDebounceUs);
    c.humanizeMinUs      = ReadInt(S, L"HumanizeMinUs", c.humanizeMinUs);
    c.humanizeMaxUs      = ReadInt(S, L"HumanizeMaxUs", c.humanizeMaxUs);
    c.activeBrakeProfileIndex = ReadInt(S, L"ActiveBrakeProfileIndex", c.activeBrakeProfileIndex);
    if (c.activeBrakeProfileIndex < 0 || c.activeBrakeProfileIndex > 4) {
        c.activeBrakeProfileIndex = 1;
    }

    // Read Brake Profiles
    for (int i = 1; i <= 4; i++) {
        wchar_t section[32];
        swprintf(section, 32, L"Profile_%d", i);
        c.brakeProfiles[i].overlap_duration_us = ReadInt(section, L"OverlapDurationUs", (int)c.brakeProfiles[i].overlap_duration_us);
        c.brakeProfiles[i].brake_bias_multiplier = ReadDbl(section, L"BrakeBiasMultiplier", ReadDbl(section, L"TapStrengthMultiplier", c.brakeProfiles[i].brake_bias_multiplier));
        c.brakeProfiles[i].authority_bias_ms = ReadDbl(section, L"AuthorityBiasMs", c.brakeProfiles[i].authority_bias_ms);
        c.brakeProfiles[i].aggressiveness_curve = ReadDbl(section, L"AggressivenessCurve", ReadDbl(section, L"ReleaseCurveExponent", c.brakeProfiles[i].aggressiveness_curve));
        c.brakeProfiles[i].accuracyThreshold = ReadDbl(section, L"AccuracyThreshold", c.brakeProfiles[i].accuracyThreshold);
    }

    const wchar_t* P = L"Physics";
    c.physMaxSpeed     = std::clamp(ReadDbl(P, L"MaxSpeed", c.physMaxSpeed), 10.0, 1000.0);
    c.physFriction     = ReadDbl(P, L"Friction", c.physFriction);
    c.physStopSpeed    = ReadDbl(P, L"StopSpeed", c.physStopSpeed);
    c.physAccelerate   = ReadDbl(P, L"Accelerate", c.physAccelerate);

    const wchar_t* B = L"Bhop";
    c.bhopMode         = ReadInt(B, L"Mode", c.bhopMode);
    c.airborneDelayMs  = ReadInt(B, L"AirborneDelayMs", c.airborneDelayMs);
    c.scrollBurstGapMs = ReadInt(B, L"ScrollBurstGapMs", c.scrollBurstGapMs);

    c.landingScanMs  = ReadInt(B, L"LandingScanMs", ReadInt(B, L"CS2LandingScanMs", c.landingScanMs));
    c.airborneLockMs = ReadInt(B, L"AirborneLockMs", ReadInt(B, L"CS2AirborneLockMs", c.airborneLockMs));
    c.spamIntervalMs = std::clamp(ReadInt(B, L"SpamIntervalMs", ReadInt(B, L"CS2SpamIntervalMs", c.spamIntervalMs)), 1, 100);

    for (int i = 1; i <= 4; i++) {
        wchar_t key[32];
        swprintf(key, 32, L"Mode%dHoldMin", i);  c.modeCfg[i].hMin = ReadInt(B, key, c.modeCfg[i].hMin);
        swprintf(key, 32, L"Mode%dHoldMax", i);  c.modeCfg[i].hMax = ReadInt(B, key, c.modeCfg[i].hMax);
        swprintf(key, 32, L"Mode%dDelayMin", i); c.modeCfg[i].dMin = ReadInt(B, key, c.modeCfg[i].dMin);
        swprintf(key, 32, L"Mode%dDelayMax", i); c.modeCfg[i].dMax = ReadInt(B, key, c.modeCfg[i].dMax);
    }

    const wchar_t* A = L"App";
    c.minimizeToTray    = ReadInt(A, L"MinimizeToTray", c.minimizeToTray) != 0;
    c.dashboardRefreshMs= ReadInt(A, L"DashboardRefreshMs", c.dashboardRefreshMs);
    c.debugMode         = ReadBool(A, L"DebugMode", c.debugMode);
    c.watchdogIntervalMs= std::clamp(ReadInt(A, L"WatchdogIntervalMs", c.watchdogIntervalMs), 10, 10000);

    const wchar_t* SM = L"SafeMode";
    c.safeModeEnabled    = ReadBool(SM, L"Enabled", c.safeModeEnabled);

    return true;
}

// ════════════════════════════════════════════════════════════════
//  SAVE
// ════════════════════════════════════════════════════════════════
bool Save(const RuntimeConfig& c) {
    uint64_t gen = s_persistenceGeneration.fetch_add(1, std::memory_order_relaxed);
    DWORD tid = GetCurrentThreadId();
    (void)gen; // Suppress unused variable warning in Release/Profile builds
    (void)tid; // Suppress unused variable warning in Release/Profile builds

    if (s_shutdownBarrier.load(std::memory_order_seq_cst)) {
        DLOG_WARN(Config, "[PERSIST] [SAVE_REJECTED] Rejected by ShutdownPersistenceBarrier. Gen: %llu, Thread: %lu", gen, tid);
        return false;
    }

    if (!rcfg::CanPersistRuntimeState()) {
        DLOG_WARN(Config, "[PERSIST] [SAVE_REJECTED] Rejected by Global Persistence Freeze Layer. Gen: %llu, Thread: %lu", gen, tid);
        return false;
    }

    DLOG_INFO(Config, "[PERSIST] [SAVE_ALLOWED] Persistence allowed. Gen: %llu, Thread: %lu", gen, tid);
    
    EnsurePath();

    const wchar_t* S = L"Strafe";
    WriteInt(S, L"QuickTapMs", c.quickTapMs);
    WriteInt(S, L"MaxScaleMs", c.maxScaleMs);
    WriteDbl(S, L"CrouchMult", c.crouchMult);
    WriteInt(S, L"TapDelayMs", c.tapDelayMs);
    WriteInt(S, L"SprayDelayMs", c.sprayDelayMs);
    WriteInt(S, L"BurstThreshold", c.burstThreshold);
    WriteInt(S, L"SpaceDelayMs", c.spaceDelayMs);
    WriteInt(S, L"LatencyMarginMs", c.latencyMarginMs);
    WriteInt(S, L"MinStopMs", c.minStopMs);
    WriteInt(S, L"LutMaxMs", c.lutMaxMs);
    WriteDbl(S, L"SubtickPaddingTicks", c.subtickPaddingTicks);
    WriteInt(S, L"WalkMemoryMs", c.walkMemoryMs);
    WriteDbl(S, L"WalkRatioSkip", c.walkRatioSkip);
    WriteDbl(S, L"WalkRatioLight", c.walkRatioLight);
    WriteInt(S, L"MinWalkStopMs", c.minWalkStopMs);
    WriteInt(S, L"WalkMaxStopMs", c.walkMaxStopMs);
    WriteDbl(S, L"DecayK", c.decayK);
    WriteInt(S, L"DirChangePenaltyMs", c.dirChangePenaltyMs);
    WriteInt(S, L"TapSpamWindowMs", c.tapSpamWindowMs);
    WriteDbl(S, L"StopStrengthMin", c.stopStrengthMin);
    WriteDbl(S, L"TapSpamAlpha", c.tapSpamAlpha);
    WriteInt(S, L"TapSpamHalfLifeMs", c.tapSpamHalfLifeMs);
    WriteInt(S, L"MinTapUs", (int)c.minTapUs);
    WriteDbl(S, L"ConflictIncrement", c.conflictIncrement);
    WriteDbl(S, L"ConflictDecrement", c.conflictDecrement);
    WriteInt(S, L"ConflictHalfLifeMs", c.conflictHalfLifeMs);

    WriteInt(S, L"HardwareDebounceUs", c.hardwareDebounceUs);
    WriteInt(S, L"HumanizeMinUs", c.humanizeMinUs);
    WriteInt(S, L"HumanizeMaxUs", c.humanizeMaxUs);
    WriteInt(S, L"ActiveBrakeProfileIndex", c.activeBrakeProfileIndex);

    // Write Brake Profiles
    for (int i = 1; i <= 4; i++) {
        wchar_t section[32];
        swprintf(section, 32, L"Profile_%d", i);
        WriteInt(section, L"OverlapDurationUs", (int)c.brakeProfiles[i].overlap_duration_us);
        WriteDbl(section, L"BrakeBiasMultiplier", c.brakeProfiles[i].brake_bias_multiplier);
        WriteDbl(section, L"AuthorityBiasMs", c.brakeProfiles[i].authority_bias_ms);
        WriteDbl(section, L"AggressivenessCurve", c.brakeProfiles[i].aggressiveness_curve);
        WriteDbl(section, L"AccuracyThreshold", c.brakeProfiles[i].accuracyThreshold);
    }

    const wchar_t* P = L"Physics";
    WriteDbl(P, L"MaxSpeed", c.physMaxSpeed);
    WriteDbl(P, L"Friction", c.physFriction);
    WriteDbl(P, L"StopSpeed", c.physStopSpeed);
    WriteDbl(P, L"Accelerate", c.physAccelerate);

    const wchar_t* B = L"Bhop";
    WriteInt(B, L"Mode", c.bhopMode);
    WriteInt(B, L"AirborneDelayMs", c.airborneDelayMs);
    WriteInt(B, L"ScrollBurstGapMs", c.scrollBurstGapMs);

    WriteInt(B, L"LandingScanMs", c.landingScanMs);
    WriteInt(B, L"AirborneLockMs", c.airborneLockMs);
    WriteInt(B, L"SpamIntervalMs", c.spamIntervalMs);

    for (int i = 1; i <= 4; i++) {
        wchar_t key[32];
        swprintf(key, 32, L"Mode%dHoldMin", i);  WriteInt(B, key, c.modeCfg[i].hMin);
        swprintf(key, 32, L"Mode%dHoldMax", i);  WriteInt(B, key, c.modeCfg[i].hMax);
        swprintf(key, 32, L"Mode%dDelayMin", i); WriteInt(B, key, c.modeCfg[i].dMin);
        swprintf(key, 32, L"Mode%dDelayMax", i); WriteInt(B, key, c.modeCfg[i].dMax);
    }

    const wchar_t* A = L"App";
    WriteInt(A, L"ConfigVersion", 2);
    WriteBool(A, L"MinimizeToTray", c.minimizeToTray);
    WriteInt(A, L"DashboardRefreshMs", c.dashboardRefreshMs);
    WriteBool(A, L"DebugMode", c.debugMode);
    WriteInt(A, L"WatchdogIntervalMs", c.watchdogIntervalMs);

    const wchar_t* SM = L"SafeMode";
    WriteBool(SM, L"Enabled", c.safeModeEnabled);

    return true;
}

} // namespace config_io
