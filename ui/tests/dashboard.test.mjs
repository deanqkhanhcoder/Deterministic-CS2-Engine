import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { buildSync } from 'esbuild';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';

const require = createRequire(import.meta.url);
function component(file) {
  const { outputFiles } = buildSync({ entryPoints: [fileURLToPath(new URL(file, import.meta.url))], bundle: true,
    format: 'cjs', platform: 'node', packages: 'external', write: false, jsx: 'automatic' });
  const module = { exports: {} };
  new Function('require', 'module', 'exports', outputFiles[0].text)(require, module, module.exports);
  return module.exports;
}
const { default: Dashboard } = component('../src/TelemetryDashboard.tsx');
const { ChartCard } = component('../src/DashboardCards.tsx');
const render = (path) => renderToStaticMarkup(createElement(Dashboard, {
  connected: true, history: [], threshold: 34,
  telemetry: path ? { injection_path: path, metrics: { wakeOversleepUs: 0, spinDurationUs: 0 }, keys: { phys: [], logical: [] } } : null,
}));

test('injection indicator distinguishes native, fallback and pending telemetry', () => {
  assert.match(render('ntuser'), /tone-ok[^>]*>NtUserSendInput/);
  assert.match(render('user32'), /tone-warning[^>]*>SendInput \(fallback\)/);
  assert.match(render(null), /Waiting for telemetry/);
  assert.doesNotMatch(render(null), /SendInput \(fallback\)/);
});

test('chart shows loading, empty and populated states while preserving the canvas', () => {
  const chart = (loading, samples) => renderToStaticMarkup(createElement(ChartCard,
    { title: 'Trace', description: '', loading, samples }, createElement('canvas')));
  assert.match(chart(true, 0), /Loading chart/);
  assert.match(chart(false, 0), /No samples yet/);
  assert.doesNotMatch(chart(false, 1), /Loading chart|No samples yet|invisible/);
  assert.match(chart(false, 0), /canvas/);
});
