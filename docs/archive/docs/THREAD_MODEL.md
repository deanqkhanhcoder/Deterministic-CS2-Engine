# Threading Model

The V27 Engine uses a strict multi-threaded architecture to ensure zero latency input interception while preventing system deadlocks.

## 1. Input Thread (LL Hook)
- Driven by standard Windows message pump (`GetMessage`).
- Executes `KeyboardProc` and `MouseProc`.
- MUST NEVER block on I/O, UI, or complex logic to avoid Windows silently dropping the hook.
- Enqueues events and returns immediately.

## 2. Timer Thread
- A high-priority multimedia timer thread (`timeSetEvent` or waitable timers).
- Handles counter-strafe brake expirations.
- Dispatches key-up injections strictly on time.

## 3. UI/Main Thread
- Handles ImGui rendering, telemetry processing, and configuration reloading.
- Isolated from the Input and Timer threads to prevent framerate drops from affecting input latency.
