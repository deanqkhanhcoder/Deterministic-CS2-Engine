# Config Layer Audit Report

## Objectives
1. Remove "Fire Delay" and "Subtick" architecture configuration fields.
2. Remove configuration variables related to the Fire Delay architecture that are no longer needed.
3. Remove the serialization/deserialization logic for these deleted variables.
4. Audit the files for any other stale config fields.

## Changes Made

### `include/core/runtime_config.h`
- Removed `tapDelayMs`, `sprayDelayMs`, `burstThreshold`, and `spaceDelayMs` fields (part of the Fire Delay architecture).
- Removed `subtickPaddingTicks` field (part of the Subtick architecture).
- Audited for stale config fields and removed `clickHistoryMax` and `clickHistoryWindowMs` which were part of the Fire Delay / click history logic and are now unused.

### `src/core/config_io.cpp`
- Removed loading logic (`ReadInt`, `ReadDbl`) for `TapDelayMs`, `SprayDelayMs`, `BurstThreshold`, `SpaceDelayMs`, and `SubtickPaddingTicks`.
- Removed saving logic (`WriteInt`, `WriteDbl`) for `TapDelayMs`, `SprayDelayMs`, `BurstThreshold`, `SpaceDelayMs`, and `SubtickPaddingTicks`.

### `src/core/runtime_config.cpp`
- Removed validation bounds checks for `tapDelayMs`, `sprayDelayMs`, `burstThreshold`, and `spaceDelayMs`.
- Removed the Safe Mode override logic that set `subtickPaddingTicks = 0.0`.

### `include/core/config_io.h`
- Audited for stale references. No changes were necessary as it only exposes the public interface (`Load`, `Save`, `GetConfigPath`).

## Status
- **Completed**: All Fire Delay and Subtick configuration variables, validation rules, and serialization logic have been successfully removed from the config layer.
