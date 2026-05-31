# Hotkey System

The V27 Engine supports global hotkeys for rapid configuration toggling:

- **F1**: Toggle BHOP Bypass
- **F2**: Cycle BHOP Modes
- **F3**: Cycle Profiles
- **F6**: Toggle Suspend
- **F8**: Exit Application

## Self-Healing Mechanism
Historically, if another application or a high-priority UI layer swallowed a `WM_KEYUP` event for an F-key, the engine would consider the key permanently held down. This blocked subsequent hotkey activations.

In V27, we implemented a timestamp-based auto-repeat detector. 
When an F-key is pressed, its timestamp is recorded. If a subsequent `WM_KEYDOWN` event for the same key arrives within 500ms and the key is already marked as held, it is safely ignored as OS auto-repeat. If it arrives *after* 500ms, the engine assumes the `KEYUP` was swallowed, resets the state, and correctly processes the new press.
