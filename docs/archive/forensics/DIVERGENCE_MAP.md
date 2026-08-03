# Divergence Map

Date: 2026-05-30
Runtime evidence: `runtime/runtime/logs/marco_2026-05-30_13-13-15.log`
Debug evidence: `runtime/runtime/logs/marco_debug.log`

## Event Definition

`LOGICAL_PHYSICAL_DIVERGENCE` is `telemetry::ForensicTrapType::LOGICAL_PHYSICAL_DIVERGENCE = 9` in `include/core/telemetry.h`.

Flush format is produced by `ForensicRingBuffer::FlushToFile()` in `src/core/telemetry.cpp`:

```text
event=<name> type=<id> tid=<thread> time_us=<timestamp> reason=<reason> data1=<extraData1> data2=<extraData2> focus=<focus>
```

## Producers

| Producer | File/function | Trigger | reason | data1 | data2 | focus |
| --- | --- | --- | --- | --- | --- | --- |
| Primary runtime producer | `src/core/state_engine.cpp`, `engine::PublishEngineState()` | any `s_state.phys[i] != s_state.logical[i]` during state publication | key index | `s_state.phys[i]` | `s_state.logical[i]` | currently `s_state.spacePhys`, not target focus |
| Watchdog producer | `src/core/state_engine.cpp`, `engine::RunWatchdog()` | `logical == true && phys == false` for 10 watchdog cycles | key index | `logicalVal` | `physVal` | `false` |

The runtime log is Release build. `MARCO_ENABLE_WATCHDOG` is off in Release, so the observed divergence events match the primary `PublishEngineState()` producer.

## Reason Mapping

Reason is the `Key` enum index from `include/core/types.h`:

| reason | key | meaning |
| --- | --- | --- |
| `0` | `W` | W physical/logical mismatch |
| `1` | `S` | S physical/logical mismatch |
| `2` | `A` | A physical/logical mismatch |
| `3` | `D` | D physical/logical mismatch |

## Runtime Counts

Observed divergence summaries from the forensic log:

| Event shape | Count | Interpretation |
| --- | ---: | --- |
| `reason=3 data1=0 data2=1` | 53 | D physical up, D logical down |
| `reason=2 data1=0 data2=1` | 36 | A physical up, A logical down |
| `reason=1 data1=0 data2=1` | 20 | S physical up, S logical down |
| `reason=0 data1=0 data2=1` | 8 | W physical up, W logical down |
| `reason=3 data1=1 data2=0` | 3 | D physical down, D logical up |
| `reason=2 data1=1 data2=0` | 3 | A physical down, A logical up |

## Physical State And Logical State

For the primary producer:

- `data1=0 data2=1` means physical key is not held but logical/injected key is down.
- `data1=1 data2=0` means physical key is held but logical/injected key is not down.

Important correction:

- `phys != logical` is not automatically corruption.
- Counter-Strafe intentionally creates `logical=true, phys=false` while an injected brake key is held until its timer expires.
- Focus-disabled routing can intentionally create `phys=true, logical=false` because the engine tracks physical state but does not route semantic logic while inactive.

## Recovery Path

Actual recovery seen in debug evidence is:

```text
Target focus REGAINED
-> src/core/state_reconciliation.cpp: Rebuilding semantic state from physical truth...
-> subsequent key events route=1
```

The repairing function is `engine::RebuildState()`, which calls `ReconcileLogicalStateFromPhysical()` and `bhop::ForceSpaceSync()`.

## Conclusion

The divergence event is currently too broad. It maps physical/logical mismatch but does not distinguish intended synthetic Counter-Strafe state from persistent corruption. The stronger runtime failure signal is debug routing switching to `route=0` after focus loss, then returning to `route=1` after focus regain/rebuild.

