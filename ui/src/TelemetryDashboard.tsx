import { useEffect, useRef, useState } from 'react';
import type { TelemetryData } from './types';
import { packetRate, traceScale, TRACE_WINDOW_MS } from './telemetry';
import type { TraceSample } from './telemetry';

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
        context.strokeStyle = '#243041';
        context.beginPath(); context.moveTo(left, y(value)); context.lineTo(right, y(value)); context.stroke();
        context.fillStyle = '#94a3b8'; context.fillText(value.toFixed(0), 5, y(value) + 3);
      }
      for (let second = Math.ceil((clock - TRACE_WINDOW_MS) / 1000); second * 1000 <= clock; second++) {
        const position = x(second * 1000);
        context.strokeStyle = '#1e293b';
        context.beginPath(); context.moveTo(position, top); context.lineTo(position, bottom); context.stroke();
        context.fillStyle = '#94a3b8'; context.fillText(`${((second * 1000 - clock) / 1000).toFixed(1)}s`, position - 10, height - 9);
      }
      if (view === 'velocity') {
        context.strokeStyle = '#34d399'; context.setLineDash([5, 5]);
        context.beginPath(); context.moveTo(left, y(limit)); context.lineTo(right, y(limit)); context.stroke();
        context.setLineDash([]); context.fillStyle = '#6ee7b7';
        context.fillText(`Accuracy ${limit} u/s`, left + 8, y(limit) - 6);
      }
      const series: [keyof Pick<TraceSample, 'velocity' | 'jitter' | 'oversleep'>, string][] =
        view === 'velocity' ? [['velocity', '#22d3ee']] : [['jitter', '#22d3ee'], ['oversleep', '#fbbf24']];
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
  const speed = telemetry?.hud?.currentSpeed ?? 0;
  const duration = telemetry?.hud?.lastBrakeMs ?? 0;
  const ticks = telemetry?.hud?.lastBrakeTicks ?? 0;
  const hasBrake = duration > 0;
  const verdict = hasBrake ? telemetry?.hud?.lastResult ?? 'IDLE' : 'IDLE';
  const settled = live && speed <= threshold;
  const core = (value: number | undefined) => value === undefined || value === 0xffffffff ? '—' : `#${value}`;
  const panel = 'rounded-xl border border-slate-800 bg-slate-900/70';
  return (
    <div className="space-y-4 font-mono">
      <div className="grid gap-3 md:grid-cols-3">
        <section className={`${panel} p-5 border-l-2 ${settled ? 'border-l-emerald-400' : 'border-l-amber-400'}`}>
          <h2 className="text-xs text-slate-300">REALTIME VELOCITY</h2>
          <div className="mt-3 flex flex-wrap items-baseline gap-2">
            <strong className={`text-[42px] leading-none tracking-tight ${!live ? 'text-slate-500' : settled ? 'text-emerald-400' : 'text-amber-400'}`}>{live ? speed.toFixed(1) : '—'}</strong>
            <span className="text-sm text-slate-400">u/s</span>
            <span className={`ml-auto text-[11px] ${!live ? 'text-slate-400' : settled ? 'text-emerald-400' : 'text-amber-400'}`}>{!live ? 'NO LIVE DATA' : settled ? 'ACCURATE' : 'MOVING'}</span>
          </div>
          <p className="mt-3 text-[11px] text-slate-400">Engine estimate · {telemetry?.profile?.name ?? '—'} · ≤ {threshold} u/s</p>
        </section>
        <section className={`${panel} p-5`}>
          <h2 className="text-xs text-slate-300">BRAKE VERDICT / LAST DURATION</h2>
          <div className="mt-3 flex items-baseline gap-3">
            <strong className={`text-3xl ${verdict === 'FINE' ? 'text-emerald-400' : verdict === 'EARLY' || verdict === 'OVER' ? 'text-rose-400' : 'text-slate-500'}`}>{verdict}</strong>
            <span className="text-sm text-cyan-300">{hasBrake ? `${Number(duration.toFixed(3))} ms` : '—'}</span>
          </div>
          <p className="mt-3 text-[11px] text-slate-400">{hasBrake ? `${Number(ticks.toFixed(3))} ticks @ 64 Hz · Pre ${telemetry?.hud?.lastPreSpeed.toFixed(1)} u/s` : 'Awaiting first brake'}</p>
        </section>
        <section className={`${panel} p-5`}>
          <h2 className="text-xs text-slate-300">HARDWARE DECOUPLING / JITTER HEALTH</h2>
          <div className="mt-3 flex items-baseline gap-2"><strong className="text-3xl text-cyan-300">{telemetry?.metrics?.timerSampleCount ? telemetry.metrics.timerJitterP99Us : '—'}</strong><span className="text-sm text-slate-400">µs P99</span></div>
          <p className={`mt-3 text-[11px] ${telemetry?.affinity?.smtCollision ? 'text-rose-400' : 'text-slate-400'}`}>Timer {core(telemetry?.affinity?.timingCore)} · Hook {core(telemetry?.affinity?.hookCore)}{telemetry?.affinity?.smtCollision ? ' · SMT collision' : ''}</p>
        </section>
      </div>

      <div className="grid gap-4 lg:grid-cols-[minmax(0,1.7fr)_minmax(0,1fr)]">
        <section className={`${panel} p-4 min-w-0`}>
          <div className="flex flex-wrap items-center justify-between gap-3">
            <h2 className="text-xs font-semibold text-slate-200">{mode === 'velocity' ? 'LIVE VELOCITY & DECELERATION TRACE' : 'REALTIME QPC JITTER & OVERSLEEP TRACE'}</h2>
            <div className="flex rounded border border-slate-700 p-0.5">
              {(['velocity', 'timing'] as const).map(view => <button key={view} aria-pressed={mode === view} onClick={() => setMode(view)} className={`rounded px-2 py-1 text-[11px] ${mode === view ? 'bg-cyan-500/15 text-cyan-300' : 'text-slate-400 hover:text-white'}`}>{view === 'velocity' ? 'VELOCITY' : 'QPC / WAKE'}</button>)}
            </div>
          </div>
          <div className="mt-3 flex flex-wrap justify-between gap-2 text-[11px] text-slate-400">
            <span>{mode === 'velocity' ? 'Cyan: velocity · Dashed: accuracy threshold' : 'Cyan: jitter P50 · Amber: oversleep P50 (last 128 timings)'}</span>
            <span className={live ? 'text-emerald-400' : 'text-rose-400'}>{live ? 'LIVE' : 'DISCONNECTED'} · {history.length}/100 samples</span>
          </div>
          <canvas ref={canvasRef} role="img" aria-label={mode === 'velocity' ? `Live velocity trace with accuracy threshold ${threshold} u/s` : 'Live QPC jitter and wake oversleep trace in microseconds'} className="mt-3 h-64 w-full rounded-lg bg-slate-950" />
          <p className="mt-3 text-[11px] text-slate-400">3.3s sliding window · Auto scale · {mode === 'velocity' ? 'Source physics estimate from delivered keys' : telemetry?.metrics?.timerSampleCount ? 'Timing statistics retained while idle' : 'Idle: no timer samples yet; zero is expected'}</p>
        </section>

        <section className={`${panel} p-4`}>
          <h2 className="text-xs font-semibold text-slate-200">SUB-TICK STEP & KEY STATE MATRIX</h2>
          <div className="mx-auto my-5 grid max-w-[220px] grid-cols-3 gap-2">
            {[null, { key: 'W', index: 0 }, null, { key: 'A', index: 2 }, { key: 'S', index: 1 }, { key: 'D', index: 3 }].map((entry, position) => {
              if (!entry) return <div key={position} />;
              const physical = live && !!telemetry?.keys?.phys[entry.index];
              const logical = live && !!telemetry?.keys?.logical[entry.index];
              return <div key={entry.key} aria-label={`${entry.key}: physical ${physical ? 'down' : 'up'}, delivered ${logical ? 'down' : 'up'}`} className={`rounded-lg border p-3 text-center ${physical ? 'border-cyan-400 bg-cyan-500/15 text-cyan-300' : logical ? 'border-amber-400 bg-amber-500/15 text-amber-300' : 'border-slate-700 bg-slate-950 text-slate-400'}`}><strong className="text-xl">{entry.key}</strong><div className="mt-1 text-[9px]">P {physical ? 'ON' : 'OFF'} / L {logical ? 'ON' : 'OFF'}</div></div>;
            })}
          </div>
          <p className="text-center text-[10px] text-slate-400">Cyan: physical · Amber: delivered only</p>
          <div className="mt-5 border-t border-slate-800 pt-4">
            <div className="flex justify-between text-[11px] text-slate-300"><span>LAST BRAKE · 64 Hz</span><span>{hasBrake ? `${Number(ticks.toFixed(3))} ticks` : 'IDLE'}</span></div>
            <div className="mt-3 grid grid-cols-8 gap-1">
              {Array.from({ length: 8 }, (_, tick) => <div key={tick} className={`rounded border py-2 text-center text-[10px] ${hasBrake && tick === 0 ? 'border-rose-500/40 bg-rose-500/10 text-rose-300' : hasBrake && tick === 7 && verdict === 'FINE' ? 'border-emerald-500/40 bg-emerald-500/10 text-emerald-300' : hasBrake && tick > 0 && tick < 7 && tick <= Math.ceil(ticks) ? 'border-cyan-500/40 bg-cyan-500/10 text-cyan-300' : 'border-slate-800 text-slate-500'}`}>T{tick}</div>)}
            </div>
            <p className="mt-3 text-[10px] leading-5 text-slate-400">T0: Release → T1–T6: Counter-Accel → T7: Settle<br />Phase guide; hold duration above shows actual tick equivalents.</p>
          </div>
        </section>
      </div>

      <section aria-label="Micro diagnostics" className={`${panel} grid gap-3 p-3 text-[11px] sm:grid-cols-2 lg:grid-cols-5`}>
        {[
          ['Hook P50', `${telemetry?.metrics?.hookLatencyP50Us ?? 0} µs`],
          ['Hook P99', `${telemetry?.metrics?.hookLatencyP99Us ?? 0} µs`],
          ['Wake Oversleep', `${telemetry?.metrics?.wakeOversleepUs ?? 0} µs`],
          ['Spin Duration', `${telemetry?.metrics?.spinDurationUs ?? 0} µs`],
          ['Winsock SSE Rate', `${live ? packetRate(history, now) : 0} packets/s`]
        ].map(([label, value]) => <div key={label} className="flex justify-between gap-2"><span className="text-slate-400">{label}</span><span className="text-cyan-300">{value}</span></div>)}
      </section>
    </div>
  );
}
