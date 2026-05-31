# Input Pipeline

The V27 input pipeline is extremely strict about ordering and latency.

## Mouse1 Dispatch
- Mouse1 clicks **never** yield to asynchronous timers.
- When `WM_LBUTTONDOWN` occurs, the engine synchronously injects the logical counter-strafe brake (if applicable) in the same thread execution block.
- The physical `Mouse1` event is deliberately passed through to the OS by returning `false` from the handler, guaranteeing identical input processing time as vanilla gameplay.

## WASD Dispatch
- WASD physical events are swallowed (`return 1`) when the target game is focused.
- Logical WASD events are synchronously injected via `SendInput`.
- Counter-strafing calculates a temporal brake window, schedules a high-resolution waitable timer, and logically releases the brake once the timer expires.
