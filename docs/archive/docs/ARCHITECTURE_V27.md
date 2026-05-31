# V27 Architecture Overview

## Core Philosophy
The Counter-Strafe V27 engine is built on **absolute stability and deterministic input ordering**. 
We have intentionally removed asynchronous fire delays, subtick quantization, and Generation IDs to guarantee zero-latency responsiveness and maintainable code.

## Primary Components
1. **Physical Tracking Layer:**
   - Always tracks raw hardware edges regardless of focus state.
2. **Semantic Routing Layer:**
   - Synthesizes logical inputs (Counter-Strafes, BHOPs) strictly when the target window is focused.
3. **State Engine & Reconciliation:**
   - Synchronizes internal virtual states with physical reality upon focus regain.

## Event Pipeline
```
[User Input] -> [LL Hook] -> [Semantic Routing] -> [State Engine] -> [Injection] -> [Target Game]
```
