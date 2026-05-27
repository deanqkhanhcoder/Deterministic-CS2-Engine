# Phase 1: Fire Timeline Instrumentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Instrument the Counter-Strafe timeline to precisely trace the OS-level injection delays, `batch.flush()` boundaries, and the timer wake jitter, giving us deterministic proof of the root cause.

**Architecture:** We will extend the existing `[FIRE_TRACE]` mechanism to track `SendInput` dispatch events and timer wake jitter. Since the timer thread does not have access to the `fireGenerationId`, we will log the jitter with the timer `id` and `key`, which can be cross-referenced with `SHOT_SCHEDULED`.

**Tech Stack:** C++, Marco Debug Logger.

---

### Task 1: Trace `SendInput` Dispatch

**Files:**
- Modify: `c:/Users/toanpq/Desktop/marco/src/core/injection.cpp:44-80`

- [ ] **Step 1: Add `DLOG_TRACE` to `SendInput` calls**

```cpp
// Inside KeyDown
    DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_DISPATCH_DOWN key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    UINT sent = SendInput(1, &s_keyDown[idx], sizeof(INPUT));

// Inside KeyUp
    DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_DISPATCH_UP key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    UINT sent = SendInput(1, &s_keyUp[idx], sizeof(INPUT));

// Inside KeyDownUp
    DLOG_TRACE(Injection, "[FIRE_TRACE] SENDINPUT_DISPATCH_DOWNUP key=%s", reinterpret_cast<int64_t>(keymap::KeyName[idx]));
    UINT sent = SendInput(2, batch, sizeof(INPUT));
```

- [ ] **Step 2: Commit**

```bash
git add src/core/injection.cpp
git commit -m "chore: instrument SendInput dispatch for fire trace"
```

---

### Task 2: Trace Timer Wake Jitter

**Files:**
- Modify: `c:/Users/toanpq/Desktop/marco/src/core/timing.cpp:250-300`

- [ ] **Step 1: Add jitter logging to `TimerThreadFunc`**

```cpp
            // Record metrics
            int64_t jitter = std::abs(actualWakeUs - slot.expireUs);
            (void)jitter;
            int64_t oversleep = std::max(0LL, actualWakeUs - slot.expireUs);

            // ADD JITTER LOG FOR FIRE TRACE
            DLOG_TRACE(Timing, "[FIRE_TRACE] TIMER_WAKE_JITTER id=%llu key=%s jitter=%lld", slot.id, reinterpret_cast<int64_t>(keymap::KeyName[ki(slot.key)]), jitter);
```

- [ ] **Step 2: Commit**

```bash
git add src/core/timing.cpp
git commit -m "chore: instrument timer wake jitter for fire trace"
```
