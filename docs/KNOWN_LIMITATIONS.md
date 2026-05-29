# Known Limitations

While V27 establishes a robust and maintainable architecture by stripping out the experimental asynchronous fire delays, it inherently comes with certain design tradeoffs:

## 1. 0-Delay M1 Braking
Because the engine no longer swallows and delays the `Mouse1` event, the physical counter-strafe brake (which takes effect virtually) occurs at the EXACT same millisecond as the mouse click is registered by the OS. 
This means that if the game processes input events sequentially, the brake and the shot are processed in the same tick. If the game evaluates the shot *before* applying the brake velocity change, the shot will register with moving inaccuracy. 
This is an acceptable tradeoff for absolute stability, as it exactly mimics native human gameplay (where pressing M1 and counter-strafe simultaneously usually results in moving inaccuracy).

## 2. No Generational Timers
The system no longer tracks generation IDs or ABA state validation for counter-strafes. Extremely rapid button mashing that overlaps within the <20ms brake window could theoretically cancel a brake prematurely, although the physical logic minimizes this.

## 3. Strict Target Binding
The `IsTargetActive()` focus resolution relies on `GetForegroundWindow()`. Rapidly minimizing and maximizing the window may cause a 1-tick delay in the focus recognition, which could temporarily leak inputs.
