# Focus Gating

Focus gating is critical to prevent the engine from swallowing keys or injecting events while the user is tabbed out (e.g., typing in a browser or discord).

## Mechanism
- The `IsTargetActive()` function verifies if the currently focused foreground window (`GetForegroundWindow`) matches the configured target game process.
- To prevent data races between the asynchronous window focus thread and the `KeyboardProc` hook, a `std::mutex s_focusMutex` is used.
- When focus is LOST: The engine synthesizes `KEYUP` events for any currently swallowed or injected keys to prevent "stuck" keys in the OS.
- When focus is REGAINED: The engine queries the physical state of WASD via `GetAsyncKeyState` and resynchronizes the virtual state.
