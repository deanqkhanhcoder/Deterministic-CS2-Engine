import { useEffect, useRef, useState } from 'react';
import type { TelemetryData } from './types';
import { packetRate, traceScale, TRACE_WINDOW_MS } from './telemetry';
import type { TraceSample } from './telemetry';
import { KPICard, ChartCard, StatusStrip } from './DashboardCards';

interface Props {
  telemetry: TelemetryData | null;
  history: TraceSample[];
  connected: boolean;
  threshold: number;
}

export default function TelemetryDashboard({ telemetry, history, connected, threshold }: Props) {
  const [mode, setMode] = useState<'velocity' | 'timing'>('velocity');
  const [now, setNow] = useState(() => performance.now());
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const latest = useRef({ history, mode, threshold });
  latest.current = { history, mode, threshold };
  useEffect(() => {
    const timer = window.setInterval(() => setNow(performance.now()), 250);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas?.getContext('2d');
    if (!canvas || !context) return;
    let frame = 0;
    let themeClass: string | null = null;
    let colors = { border: '', muted: '', accent: '', ok: '', warning: '' };
    const draw = () => {
      const { history: samples, mode: view, threshold: limit } = latest.current;
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;
      const dpr = window.devicePixelRatio || 1;
      if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
        canvas.width = Math.round(width * dpr);
        canvas.height = Math.round(height * dpr);
      }
      context.setTransform(dpr, 0, 0, dpr, 0, 0);
      context.clearRect(0, 0, width, height);
      // Cache palette until the root theme changes; no CSS parsing per data point.
      if (themeClass !== document.documentElement.className) {
        themeClass = document.documentElement.className;
        const css = getComputedStyle(document.documentElement);
        colors = Object.fromEntries(Object.keys(colors).map(key => [key, css.getPropertyValue(`--${key}`).trim()])) as typeof colors;
      }
      const clock = performance.now();
      const visible = samples.filter(sample => sample.time >= clock - TRACE_WINDOW_MS);
      const scale = traceScale(visible, view, limit);
      const left = 44, right = width - 12, top = 14, bottom = height - 30;
      const x = (time: number) => left + (time - clock + TRACE_WINDOW_MS) / TRACE_WINDOW_MS * (right - left);
      const y = (value: number) => bottom - value / scale * (bottom - top);
      context.font = '10px monospace';
      context.lineWidth = 1;
      for (let row = 0; row <= 4; row++) {
        const value = scale * row / 4;
        context.strokeStyle = colors.border;
        context.beginPath(); context.moveTo(left, y(value)); context.lineTo(right, y(value)); context.stroke();
        context.fillStyle = colors.muted; context.fillText(value.toFixed(0), 5, y(value) + 3);
      }
      for (let second = Math.ceil((clock - TRACE_WINDOW_MS) / 1000); second * 1000 <= clock; second++) {
        const position = x(second * 1000);
        context.strokeStyle = colors.border;
        context.beginPath(); context.moveTo(position, top); context.lineTo(position, bottom); context.stroke();
        context.fillStyle = colors.muted; context.fillText(`${((second * 1000 - clock) / 1000).toFixed(1)}s`, position - 10, height - 9);
      }
      if (view === 'velocity') {
        context.strokeStyle = colors.ok; context.setLineDash([5, 5]);
        context.beginPath(); context.moveTo(left, y(limit)); context.lineTo(right, y(limit)); context.stroke();
        context.setLineDash([]); context.fillStyle = colors.ok;
        context.fillText(`Accuracy ${limit} u/s`, left + 8, y(limit) - 6);
      }
      const series: [keyof Pick<TraceSample, 'velocity' | 'jitter' | 'oversleep'>, string][] =
        view === 'velocity' ? [['velocity', colors.accent]] : [['jitter', colors.accent], ['oversleep', colors.warning]];
      for (const [key, color] of series) {
        context.strokeStyle = color; context.lineWidth = 2; context.beginPath();
        visible.forEach((sample, index) => {
          // Never draw a continuous signal across a missing SSE interval.
          if (!index || sample.time - visible[index - 1].time > 250) context.moveTo(x(sample.time), y(sample[key]));
          else context.lineTo(x(sample.time), y(sample[key]));
        });
        context.stroke();
        const last = visible[visible.length - 1];
        if (last && clock - last.time < 250) {
          context.fillStyle = color; context.beginPath(); context.arc(x(last.time), y(last[key]), 3, 0, Math.PI * 2); context.fill();
        }
      }
      frame = requestAnimationFrame(draw);
    };
    frame = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(frame);
  }, []);

  const last = history[history.length - 1];
  const live = connected && !!last && now - last.time < 1500;
  const rate = live ? packetRate(history, now) : 0;
  const speed = telemetry?.hud?.currentSpeed ?? 0;
  const duration = telemetry?.hud?.lastBrakeMs ?? 0;
  const ticks = telemetry?.hud?.lastBrakeTicks ?? 0;
  const hasBrake = duration > 0;
  const verdict = hasBrake ? telemetry?.hud?.lastResult ?? 'IDLE' : 'IDLE';
  const settled = speed <= threshold;
  const core = (value: number | undefined) => value === undefined || value === 0xffffffff ? '—' : `#${value}`;
  const path = telemetry?.injection_path;
  const visibleSamples = history.filter(sample => sample.time >= now - TRACE_WINDOW_MS).length;
  const loading = !telemetry && connected;
  return <div className="space-y-6">
    <div className="grid grid-cols-2 gap-4 lg:grid-cols-4">
      <KPICard title="Realtime velocity" value={live ? speed.toFixed(1) : '—'} unit="u/s"
        tone={!live ? 'neutral' : settled ? 'ok' : 'warning'}
        detail={<><span className={!live ? '' : settled ? 'tone-ok' : 'tone-warning'}>{!live ? 'No live data' : settled ? 'Accurate' : 'Moving'}</span><span className="mt-1 block">{telemetry?.profile?.name ?? '—'} / ≤ {threshold} u/s</span></>} />
      <KPICard title="Brake verdict" value={hasBrake ? verdict : '—'}
        tone={verdict === 'FINE' ? 'ok' : verdict === 'EARLY' || verdict === 'OVER' ? 'danger' : 'neutral'}
        detail={hasBrake ? `From ${telemetry?.hud?.lastPreSpeed.toFixed(1)} u/s` : 'Awaiting first brake'} />
      <KPICard title="Last brake" value={hasBrake ? Number(duration.toFixed(3)) : '—'} unit="ms"
        detail={hasBrake ? `${Number(ticks.toFixed(3))} ticks at 64 Hz` : 'No brake recorded'} />
      <KPICard title="Timer jitter P99" value={telemetry?.metrics?.timerSampleCount ? telemetry.metrics.timerJitterP99Us : '—'} unit="µs"
        tone={telemetry?.affinity?.smtCollision ? 'warning' : undefined}
        detail={<>Timer {core(telemetry?.affinity?.timingCore)} / Hook {core(telemetry?.affinity?.hookCore)}{telemetry?.affinity?.smtCollision && <span className="block tone-warning">SMT collision</span>}</>} />
    </div>

    <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
      <ChartCard title={mode === 'velocity' ? 'Live velocity & deceleration' : 'QPC jitter & wake oversleep'}
        description={mode === 'velocity' ? `Dashed line: accuracy threshold ${threshold} u/s. Engine velocity estimate.` : 'Jitter P50 and wake oversleep P50 from the last 128 timings.'}
        loading={loading} samples={visibleSamples}
        actions={<div className="flex gap-1 rounded-lg border border-[var(--border)] p-1">
          {(['velocity', 'timing'] as const).map(view => <button key={view} aria-pressed={mode === view} onClick={() => setMode(view)}
            className={`rounded px-3 py-2 text-xs font-medium ${mode === view ? 'bg-[var(--accent)] text-[var(--accent-fg)]' : 'text-[var(--muted)] hover:text-[var(--fg)]'}`}>{view === 'velocity' ? 'Velocity' : 'QPC / wake'}</button>)}
        </div>}>
        <canvas ref={canvasRef} role="img" aria-label={mode === 'velocity' ? `Live velocity trace with accuracy threshold ${threshold} u/s` : 'Live QPC jitter and wake oversleep trace in microseconds'} className="h-64 w-full rounded-lg bg-[var(--bg)]" />
      </ChartCard>

      <section className="card px-6 py-4">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <h2 className="text-sm font-medium">Key state & sub-tick steps</h2>
          <span className={`flex items-center gap-2 text-xs ${live ? 'tone-ok' : 'tone-neutral'}`}>
            <span className={`h-2 w-2 rounded-full ${live ? 'bg-[var(--ok)]' : 'bg-[var(--muted)]'}`} />
            {live ? `Live ${rate} pkt/s` : 'Disconnected'}
          </span>
        </div>
        <div className="mx-auto my-4 grid max-w-[224px] grid-cols-3 gap-2">
          {[null, { key: 'W', index: 0 }, null, { key: 'A', index: 2 }, { key: 'S', index: 1 }, { key: 'D', index: 3 }].map((entry, position) => {
            if (!entry) return <div key={position} />;
            const physical = live && !!telemetry?.keys?.phys[entry.index];
            const logical = live && !!telemetry?.keys?.logical[entry.index];
            return <div key={entry.key} aria-label={`${entry.key}: physical ${physical ? 'down' : 'up'}, delivered ${logical ? 'down' : 'up'}`}
              className={`rounded-lg border p-4 text-center ${physical ? 'state-ok' : logical ? 'state-warning' : 'border-[var(--border)] bg-[var(--bg)] text-[var(--muted)]'}`}>
              <strong className="text-xl font-medium">{entry.key}</strong><div className="mt-1 text-xs"><span className="block">P {physical ? 'On' : 'Off'}</span><span className="block">L {logical ? 'On' : 'Off'}</span></div>
            </div>;
          })}
        </div>
        <p className="text-center text-xs text-[var(--muted)]">Green: physical / Yellow: delivered only</p>
        <div className="mt-4 border-t border-[var(--border)] pt-4">
          <div className="flex justify-between gap-4 text-xs"><span>Last brake at 64 Hz</span><span className="tabular-nums">{hasBrake ? `${Number(ticks.toFixed(3))} ticks` : 'Idle'}</span></div>
          <div className="mt-4 grid grid-cols-8 gap-1">
            {Array.from({ length: 8 }, (_, tick) => <div key={tick}
              className={`rounded border py-2 text-center text-xs ${hasBrake && tick === 0 ? 'state-danger' : hasBrake && tick === 7 && verdict === 'FINE' ? 'state-ok' : hasBrake && tick > 0 && tick < 7 && tick <= Math.ceil(ticks) ? 'border-[var(--accent)] bg-[var(--bg)]' : 'border-[var(--border)] text-[var(--muted)]'}`}>T{tick}</div>)}
          </div>
          <p className="mt-4 text-xs leading-5 text-[var(--muted)]">T0: Release → T1–T6: Counter-Accel → T7: Settle<br />Phase guide; duration above shows actual tick equivalents.</p>
        </div>
      </section>
    </div>

    <StatusStrip items={[
      { label: 'Hook P50 / P99', value: telemetry ? `${telemetry.metrics.hookLatencyP50Us} / ${telemetry.metrics.hookLatencyP99Us} µs` : '—' },
      { label: 'Wake oversleep', value: telemetry ? `${Number(telemetry.metrics.wakeOversleepUs.toFixed(1))} µs` : '—' },
      { label: 'Spin duration', value: telemetry ? `${Number(telemetry.metrics.spinDurationUs.toFixed(1))} µs` : '—' },
      { label: 'SSE rate', value: `${rate} pkt/s` },
      { label: 'Injection path —', value: path === 'ntuser' ? 'NtUserSendInput' : path === 'user32' ? 'SendInput (fallback)' : 'Waiting for telemetry', tone: path === 'ntuser' ? 'ok' : path === 'user32' ? 'warning' : 'neutral' },
    ]} />
    <p className="text-xs text-[var(--muted)]">3.3 s sliding window / {visibleSamples} samples / Auto scale{!telemetry?.metrics?.timerSampleCount && mode === 'timing' ? ' / Idle: no timer samples yet' : ''}</p>
  </div>;
}
