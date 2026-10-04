import type { TelemetryData } from './types';

export interface TraceSample {
  time: number;
  uptime: number;
  velocity: number;
  jitter: number;
  oversleep: number;
}

export const TRACE_LIMIT = 100;
export const TRACE_WINDOW_MS = 3300;
const nonnegative = (value: number | undefined) =>
  Number.isFinite(value) ? Math.max(0, value!) : 0;

export function appendTelemetry(history: TraceSample[], data: TelemetryData, time: number): TraceSample[] {
  const previous = history[history.length - 1];
  // A new daemon session starts a new trace; reconnect gaps remain visible.
  const retained = previous && data.uptimeMs < previous.uptime ? [] : history;
  return [...retained.slice(-(TRACE_LIMIT - 1)), {
    time, uptime: data.uptimeMs,
    velocity: nonnegative(data.hud?.currentSpeed),
    jitter: nonnegative(data.metrics?.timerJitterUs),
    oversleep: nonnegative(data.metrics?.wakeOversleepUs)
  }];
}

export function traceScale(history: TraceSample[], mode: 'velocity' | 'timing', threshold: number): number {
  const floor = mode === 'velocity' ? Math.max(250, threshold) : 100;
  const peak = history.reduce((max, sample) => Math.max(max,
    mode === 'velocity' ? sample.velocity : Math.max(sample.jitter, sample.oversleep)), 0);
  return Math.ceil(Math.max(floor, peak * 1.15) / 25) * 25;
}

export function packetRate(history: TraceSample[], now: number): number {
  return history.filter(sample => sample.time > now - 1000 && sample.time <= now).length;
}
