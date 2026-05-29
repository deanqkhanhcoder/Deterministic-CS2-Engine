# V27 REGRESSION REPORT

## Overview
A comprehensive regression suite was executed following the rollback and stripping of the Fire Delay architecture. The engine was tested to ensure the core mechanics of Counter-Strafe V26 are completely preserved and that no critical logic was compromised during the source code audit.

## Test Results
1. **Automated Baseline Verification**
   - **Command:** `python scripts/test/run_regression.py`
   - **Result:** `[PASS] REGRESSION PASSED: Current physics matches V26 baseline exactly.`
   - **Details:** The deterministic simulator confirmed that movement physics and axis processing produce byte-for-byte identical outputs to the established V26 physics baseline.

2. **Core Systems Audit**
   - **BHOP Engine:** Passed. Bypass logic tested against race conditions; `OnSpaceDown`/`OnSpaceUp` handles native events seamlessly.
   - **Axis Resolution:** Passed. Overlap and release logic functions synchronously.
   - **Counter-Strafe Fire Brake:** Passed. `OnLButtonDown` logic applies the physical brake immediately and injects counter keys without swallowing the mouse click or delaying the shot.
   - **Focus Gating (Alt-Tab):** Passed. The `IsTargetActive()` mutex resolves asynchronous focus updates cleanly.
   - **Hotkey System:** Passed. Self-healing timestamp mechanism ensures F-keys do not get stuck when UI layers interfere.

## Verdict
**STABLE.** The engine is functionally sound and free from the complexity of the V26.3 FireState experiments. 1-tick delays and quantization artifacts are permanently eliminated.
