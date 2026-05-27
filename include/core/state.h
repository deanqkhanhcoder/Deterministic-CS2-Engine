#pragma once
// ╔══════════════════════════════════════════════════════════════════════╗
// ║  Counter-Strafe v25.3 C++ — Unified State Object                    ║
// ║  Cache-friendly layout, all per-key arrays indexed by Key enum      ║
// ╚══════════════════════════════════════════════════════════════════════╝

#include "types.h"
#include "config.h"
#include <cstdint>
#include <cstring>

struct WalkState {
    bool    shiftDown           = false;
    int64_t shiftReleaseTimeMs  = 0;
    int64_t startTimeUs[5]      = {};   // per key [W,S,A,D,M1]
    int64_t accumUs[5]          = {};   // per key
};

struct MemoryState {
    AxisDir lastDir[2]                   = { AxisDir::None, AxisDir::None };
    int64_t lastDirChangeTimeMs[2]       = {};
    int64_t lastReleaseTimeMs[2]         = {};
    int64_t lastHoldUs[2]                = {};

    int64_t tapSpamLastTimeMs[5]         = {};
    double  tapSpamPenalty[5]            = {};
    int64_t tapSpamLastDecayTimeMs[5]    = {};

    double  conflictPenalty[2]           = {};
    int64_t lastConflictExitTimeMs[2]    = {};
};

struct alignas(64) State {
    // ── Per-key state (indexed by Key enum: W=0, S=1, A=2, D=3, M1=4) ──
    bool    phys[5]             = {};
    bool    logical[5]          = {};
    int64_t downTimeUs[5]       = {};
    int64_t heldDurUs[5]        = {};

    // ── Per-axis state (indexed by Axis enum: X=0, Y=1) ──
    AxisState axisState[2]              = { AxisState::None, AxisState::None };
    uint32_t  generation[2]             = {};
    int64_t   conflictEnteredTimeMs[2]  = {};

    // ── Timer tracking ──
    uint64_t expectedTimerId[5] = {};
    
    // ── System keys ──
    bool    sysLCtrl            = false;
    bool    sysC                = false;
    bool    spacePhys           = false;  // [FIX Bug #1] Always-track physical space
    int64_t lastSpaceTimeMs     = 0;
    int64_t lastCounterMs       = 0;

    // ── Sub-states ──
    WalkState   walk;
    MemoryState mem;

    // ── Script suspended state ──
    bool suspended = false;

    struct {
        bool active = false;
        uint8_t suspendedMovementMask = 0;
        uint8_t injectedCounterMask = 0;
        uint64_t expectedShotId = 0;
    } autoFire;

    // ── Click history (circular buffer) ──
    int64_t clickHistory[cfg::CLICK_HISTORY_MAX] = {};
    int     clickHistoryHead    = 0;
    int     clickHistoryCount   = 0;

    // ── Reset all state ──
    void Reset() {
        memset(phys, 0, sizeof(phys));
        memset(logical, 0, sizeof(logical));
        memset(downTimeUs, 0, sizeof(downTimeUs));
        memset(heldDurUs, 0, sizeof(heldDurUs));

        axisState[0] = axisState[1] = AxisState::None;
        generation[0] = generation[1] = 0;
        conflictEnteredTimeMs[0] = conflictEnteredTimeMs[1] = 0;

        sysLCtrl = sysC = false;
        lastSpaceTimeMs = lastCounterMs = 0;
        clickHistoryHead = clickHistoryCount = 0;

        walk = WalkState{};
        mem  = MemoryState{};
    }

    // ── Click history helpers ──
    void PushClick(int64_t timeMs) {
        int idx = (clickHistoryHead + clickHistoryCount) % cfg::CLICK_HISTORY_MAX;
        if (clickHistoryCount < cfg::CLICK_HISTORY_MAX) {
            clickHistory[idx] = timeMs;
            clickHistoryCount++;
        } else {
            // Overwrite oldest
            clickHistory[clickHistoryHead] = timeMs;
            clickHistoryHead = (clickHistoryHead + 1) % cfg::CLICK_HISTORY_MAX;
        }
    }

    void PruneClicks(int64_t nowMs) {
        while (clickHistoryCount > 0) {
            int64_t oldest = clickHistory[clickHistoryHead];
            if (nowMs - oldest > cfg::CLICK_HISTORY_WINDOW_MS) {
                clickHistoryHead = (clickHistoryHead + 1) % cfg::CLICK_HISTORY_MAX;
                clickHistoryCount--;
            } else {
                break;
            }
        }
    }

    // Keep at most N clicks
    void TrimClicks(int maxCount) {
        while (clickHistoryCount > maxCount) {
            clickHistoryHead = (clickHistoryHead + 1) % cfg::CLICK_HISTORY_MAX;
            clickHistoryCount--;
        }
    }

    bool IsCrouching() const {
        return sysLCtrl || sysC;
    }
};
