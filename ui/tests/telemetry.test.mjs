import test from 'node:test';
import assert from 'node:assert/strict';
import { appendTelemetry, traceScale, packetRate } from '../src/telemetry.ts';
const packet = (uptime, speed = 0, jitter = 0) => ({
  uptimeMs: uptime, hud: { currentSpeed: speed },
  metrics: { timerJitterUs: jitter, wakeOversleepUs: jitter / 2 }
});
test('SSE history appends, retains zero idle samples and bounds the sliding window', () => {
  let history = [];
  for (let i = 0; i < 150; i++) history = appendTelemetry(history, packet(i, i), i * 33);
  assert.equal(history.length, 100);
  assert.equal(history[0].velocity, 50);
  assert.equal(history[99].velocity, 149);
  const idle = appendTelemetry(history, packet(151), 5000);
  assert.equal(idle.length, 100);
  assert.equal(idle[99].velocity, 0);
  assert.equal(history[99].velocity, 149); // immutable React state
  assert.equal(appendTelemetry(idle, packet(0), 5100).length, 1); // restart
});
test('axis floors, peaks and invalid values cannot flatten or poison the trace', () => {
  assert.equal(traceScale([], 'velocity', 34), 250);
  assert.equal(traceScale([], 'timing', 34), 100);
  const spike = appendTelemetry([], packet(1, 310, 600), 10);
  assert.ok(traceScale(spike, 'velocity', 34) > 310);
  assert.ok(traceScale(spike, 'timing', 34) > 600);
  const bad = appendTelemetry([], packet(1, NaN, Infinity), 10);
  assert.equal(bad[0].velocity, 0);
  assert.equal(bad[0].jitter, 0);
});
test('packet rate counts actual SSE arrivals and expires on disconnect', () => {
  const history = [0, 33, 66, 99].map(time => ({ time }));
  assert.equal(packetRate(history, 99), 4);
  assert.equal(packetRate(history, 1099), 0);
});
