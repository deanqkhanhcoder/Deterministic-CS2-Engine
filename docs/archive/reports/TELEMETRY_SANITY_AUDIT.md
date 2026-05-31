# Telemetry Sanity Audit

Date: 2026-05-30
Goal: minimal signal with maximum debugging value. This audit does not change telemetry behavior.

## Forensic Anomaly Events

Defined in `include/core/telemetry.h` as `telemetry::ForensicTrapType`.

| Event | Classification | Reason |
| --- | --- | --- |
| `FOCUS_LOST` | Critical | Focus transitions are a known source of logical/physical state divergence. |
| `FOCUS_GAINED` | Critical | Required to correlate reconciliation and post-focus failures. |
| `PROFILE_CHANGED` | Critical | User reports intermittent failure after profile changes. |
| `COUNTERSTRAFE_CANCELLED` | Critical | Directly identifies cancellation path without logging every input. |
| `COUNTERSTRAFE_CONFLICT` | Critical | Captures axis conflict and divergence symptoms. |
| `BHOP_ABORTED` | Critical | Captures jump path abort without logging all key events. |
| `BHOP_STALL` | Critical | Captures intermittent BHOP stop conditions. |
| `TIMER_REJECTED` | Critical | Captures rejected timer work without changing timer behavior. |
| `LOGICAL_PHYSICAL_DIVERGENCE` | Critical | Core observability event for inverse ghost key class bugs. |

## Metric And Trace Events

| Producer | Classification | Reason |
| --- | --- | --- |
| `Latency_Hook_Keyboard` | Useful | Supports latency investigation, but can be noisy if promoted to text logs. |
| `Latency_Hook_Mouse` | Useful | Same as keyboard latency. |
| `Latency_TimerWake` | Useful | Supports timer jitter diagnosis. |
| `Latency_StateMutation` | Useful | Helps identify state mutation cost and spikes. |
| Event ring type `EVENT_HOOK_KEYBOARD` | Useful/noisy | Good for in-memory telemetry; should not be dumped as ordinary text unless diagnosing hook latency. |
| Event ring type `EVENT_HOOK_LATENCY` | Useful | Perf-only signal. |
| Event ring type `EVENT_TIMER_WAKE` | Useful | Perf-only signal. |
| Event ring type `EVENT_STATE_MUTATION` | Useful | Perf-only signal. |
| Event ring type `EVENT_CORE_MIGRATION` | Useful | Necessary for scheduler/affinity diagnosis. |
| Event ring type `EVENT_TIMER_JITTER` | Useful | Necessary for timer diagnosis. |
| Event ring type `EVENT_TIMER_OVERSLEEP` | Useful | Necessary for timer diagnosis. |
| Event ring type `EVENT_SCHEDULER_SPIKE` | Useful | Necessary for scheduler diagnosis. |

## Debug Logger Signal

`DLOG_TRACE`, `DLOG_DEBUG`, and related debug logs remain valuable during development, but normal key down/up traces are noise for release forensic capture.

Recommended policy:

- Keep anomaly events always available in forensic logs.
- Keep performance metrics in ring buffers and ETW.
- Avoid dumping normal input events to text logs by default.
- Preserve debug logger for debug/profile builds, but do not treat it as the primary forensic source.

## Obsolete Or Misleading Telemetry

| Item | Evidence | Recommendation |
| --- | --- | --- |
| Historical `VK_F8` emergency capture docs | Docs still mention F8, but active source no longer contains an F8 flush handler. | Update docs after hotkey ownership review. |
| `MARCO_ENABLE_LOGGING` flag | Defined but not consumed by logger code. | Resolve build flag drift. |

## Result

Current forensic anomaly enum is appropriately focused. The remaining telemetry debt is governance: keep text logs anomaly-first, keep high-frequency signals in buffers/ETW, and remove or document unused build flags.

