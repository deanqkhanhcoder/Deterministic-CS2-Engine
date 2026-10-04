import React, { useState, useEffect, useRef } from 'react';
import { 
  Activity, Shield, ShieldAlert, Cpu, Zap, RefreshCw, Power, 
  Crosshair, Sliders, Check, AlertCircle, Save, CheckCircle2
} from 'lucide-react';
import { RuntimeConfig, TelemetryData, BrakeProfile } from './types';

const DEFAULT_PORT = 47650;
const PROFILE_NAMES = ['Disabled', 'Rifle (AK/M4)', 'Pistol (USP/Glock)', 'Sniper (AWP/Scout)', 'SMG (MP9/Mac10)'];

const DEFAULT_CONFIG: RuntimeConfig = {
  quickTapMs: 30, maxScaleMs: 80, crouchMult: 0.75, latencyMarginMs: 6, minStopMs: 4, lutMaxMs: 350,
  walkMemoryMs: 130, walkRatioSkip: 0.65, walkRatioLight: 0.35, minWalkStopMs: 15, walkMaxStopMs: 22,
  decayK: 0.005, dirChangePenaltyMs: 60, tapSpamWindowMs: 60, stopStrengthMin: 0.25,
  tapSpamAlpha: 0.15, tapSpamHalfLifeMs: 150, minTapUs: 2500,
  physMaxSpeed: 250.0, physFriction: 5.2, physStopSpeed: 80.0, physAccelerate: 5.5,
  hardwareDebounceUs: 2000, humanizeMinUs: 0, humanizeMaxUs: 0,
  activeBrakeProfileIndex: 1, safeModeEnabled: false,
  bhopEnabled: false, bhopMode: 4, airborneDelayMs: 350, scrollBurstGapMs: 2,
  landingScanMs: 450, airborneLockMs: 350, spamIntervalMs: 2,
  brakeProfiles: [
    { overlapDurationUs: 0, brakeBiasMultiplier: 1.0, authorityBiasMs: 0.0, aggressivenessCurve: 1.0, momentumMemoryMs: 40.0, accuracyThreshold: 34.0 },
    { overlapDurationUs: 0, brakeBiasMultiplier: 1.0, authorityBiasMs: 0.0, aggressivenessCurve: 1.0, momentumMemoryMs: 35.0, accuracyThreshold: 34.0 },
    { overlapDurationUs: 0, brakeBiasMultiplier: 1.0, authorityBiasMs: 0.0, aggressivenessCurve: 1.0, momentumMemoryMs: 25.0, accuracyThreshold: 34.0 },
    { overlapDurationUs: 0, brakeBiasMultiplier: 1.0, authorityBiasMs: 0.0, aggressivenessCurve: 1.0, momentumMemoryMs: 40.0, accuracyThreshold: 17.0 },
    { overlapDurationUs: 0, brakeBiasMultiplier: 1.0, authorityBiasMs: 0.0, aggressivenessCurve: 1.0, momentumMemoryMs: 20.0, accuracyThreshold: 34.0 }
  ]
};

export default function App() {
  const [token, setToken] = useState<string>(() => {
    const urlParams = new URLSearchParams(window.location.search);
    return urlParams.get('token') || localStorage.getItem('marco_token') || '';
  });
  const [host, setHost] = useState<string>(() => {
    return window.location.hostname ? `http://${window.location.hostname}:${DEFAULT_PORT}` : `http://127.0.0.1:${DEFAULT_PORT}`;
  });

  const [connected, setConnected] = useState<boolean>(false);
  const [config, setConfig] = useState<RuntimeConfig>(DEFAULT_CONFIG);
  const [telemetry, setTelemetry] = useState<TelemetryData | null>(null);
  const [selectedProfileIndex, setSelectedProfileIndex] = useState<number>(1);
  const [activeTab, setActiveTab] = useState<'telemetry' | 'profiles' | 'bhop' | 'physics'>('telemetry');
  const [saving, setSaving] = useState<boolean>(false);
  const [statusMsg, setStatusMsg] = useState<string>('');

  const eventSourceRef = useRef<EventSource | null>(null);
  const timelineCanvasRef = useRef<HTMLCanvasElement | null>(null);

  // Auto-fetch local token if missing
  useEffect(() => {
    if (!token) {
      fetch(`${host}/api/token`)
        .then(res => res.json())
        .then(data => {
          if (data?.token) {
            setToken(data.token);
            localStorage.setItem('marco_token', data.token);
          }
        })
        .catch(() => {});
    }
  }, [host, token]);

  // Sync token to localStorage
  useEffect(() => {
    if (token) {
      localStorage.setItem('marco_token', token);
    }
  }, [token]);

  // Fetch initial config from daemon
  const fetchConfig = async () => {
    try {
      const res = await fetch(`${host}/api/config`, {
        headers: { 'X-Marco-Token': token }
      });
      if (res.ok) {
        const data = await res.json();
        setConfig(data);
        if (data.activeBrakeProfileIndex >= 1 && data.activeBrakeProfileIndex <= 4) {
          setSelectedProfileIndex(data.activeBrakeProfileIndex);
        }
        setConnected(true);
      } else if (res.status === 401) {
        setConnected(false);
      }
    } catch {
      setConnected(false);
    }
  };

  // Connect SSE for Realtime Telemetry (~30Hz)
  useEffect(() => {
    fetchConfig();

    const sseUrl = `${host}/api/events?token=${encodeURIComponent(token)}`;
    const es = new EventSource(sseUrl);
    eventSourceRef.current = es;

    es.addEventListener('telemetry', (e) => {
      try {
        const data = JSON.parse(e.data);
        setTelemetry(data);
        setConnected(true);
      } catch (err) {
        console.error('SSE error', err);
      }
    });

    es.addEventListener('state_changed', (e) => {
      try {
        const data = JSON.parse(e.data);
        setTelemetry(data);
        fetchConfig();
      } catch (err) {
        console.error('SSE state change error', err);
      }
    });

    es.onerror = () => {
      setConnected(false);
    };

    return () => {
      es.close();
    };
  }, [host, token]);

  // Draw real-time timeline graph
  useEffect(() => {
    const canvas = timelineCanvasRef.current;
    if (!canvas || !telemetry?.timeline) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const { width, height } = canvas;
    ctx.clearRect(0, 0, width, height);

    ctx.strokeStyle = '#1e2433';
    ctx.lineWidth = 1;
    for (let y = 0; y < height; y += 25) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }

    const { jitter, oversleep } = telemetry.timeline;
    const count = jitter.length;
    if (count === 0) return;

    const step = width / (count - 1);
    const maxVal = 200; // 200 us scale

    // Jitter (Cyan)
    ctx.strokeStyle = '#22d3ee';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let i = 0; i < count; i++) {
      const val = Math.min(jitter[i] / 10, maxVal);
      const y = height - (val / maxVal) * (height - 10) - 5;
      if (i === 0) ctx.moveTo(0, y);
      else ctx.lineTo(i * step, y);
    }
    ctx.stroke();

    // Oversleep (Amber)
    ctx.strokeStyle = '#f59e0b';
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    for (let i = 0; i < count; i++) {
      const val = Math.min(oversleep[i] / 10, maxVal);
      const y = height - (val / maxVal) * (height - 10) - 5;
      if (i === 0) ctx.moveTo(0, y);
      else ctx.lineTo(i * step, y);
    }
    ctx.stroke();
  }, [telemetry]);

  // Actions
  const toggleSafeMode = async () => {
    const nextVal = !config.safeModeEnabled;
    try {
      const res = await fetch(`${host}/api/safemode`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
        body: JSON.stringify({ enabled: nextVal })
      });
      if (res.ok) {
        setConfig(prev => ({ ...prev, safeModeEnabled: nextVal }));
        setStatusMsg(`Safe Mode ${nextVal ? 'Activated (Clamped)' : 'Deactivated'}`);
      }
    } catch (err) {
      console.error(err);
    }
  };

  const revertToSnapshot = async () => {
    try {
      const res = await fetch(`${host}/api/revert`, {
        method: 'POST',
        headers: { 'X-Marco-Token': token }
      });
      if (res.ok) {
        await fetchConfig();
        setStatusMsg('Configuration successfully reverted to pre-safemode snapshot.');
      }
    } catch (err) {
      console.error(err);
    }
  };

  const selectWeaponProfile = async (idx: number) => {
    setSelectedProfileIndex(idx);
    try {
      const res = await fetch(`${host}/api/profile`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
        body: JSON.stringify({ index: idx })
      });
      if (res.ok) {
        setConfig(prev => ({ ...prev, activeBrakeProfileIndex: idx }));
        setStatusMsg(`Switched Weapon Profile: ${PROFILE_NAMES[idx]}`);
      }
    } catch (err) {
      console.error(err);
    }
  };

  const toggleSuspend = async () => {
    try {
      await fetch(`${host}/api/suspend`, {
        method: 'POST',
        headers: { 'X-Marco-Token': token }
      });
    } catch (err) {
      console.error(err);
    }
  };

  const saveConfig = async () => {
    setSaving(true);
    try {
      const res = await fetch(`${host}/api/config`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
        body: JSON.stringify(config)
      });
      if (res.ok) {
        setStatusMsg('Configuration saved to marco.ini');
      }
    } catch (err) {
      console.error(err);
      setStatusMsg('Failed to save configuration');
    } finally {
      setSaving(false);
    }
  };

  const updateProfileField = (field: keyof BrakeProfile, val: number) => {
    const profiles = [...config.brakeProfiles];
    profiles[selectedProfileIndex] = {
      ...profiles[selectedProfileIndex],
      [field]: val
    };
    setConfig({ ...config, brakeProfiles: profiles });
  };

  const activeWeapon = config.brakeProfiles[selectedProfileIndex] || config.brakeProfiles[1];
  const curSpeed = telemetry?.hud?.currentSpeed ?? 0.0;
  const lastBrakeMs = telemetry?.hud?.lastBrakeMs ?? 0;
  const lastBrakeTicks = telemetry?.hud?.lastBrakeTicks ?? 0;
  const lastResult = telemetry?.hud?.lastResult ?? 'FINE';

  return (
    <div className="min-h-screen bg-[#0c0e14] text-slate-200 flex flex-col font-sans selection:bg-cyan-500/20">
      {/* ── Precision Utility Header ── */}
      <header className="border-b border-[#1b202e] bg-[#121622] px-5 py-2.5 flex items-center justify-between gap-4 sticky top-0 z-50">
        <div className="flex items-center gap-3">
          <div className="px-2 py-0.5 rounded bg-cyan-500/10 border border-cyan-500/30 text-cyan-400 font-mono font-bold text-xs tracking-wider">
            MARCO
          </div>
          <span className="text-xs font-mono font-semibold tracking-wide text-slate-300">
            DETERMINISTIC CS2 MOTION ENGINE
          </span>
          <span className="text-[11px] text-slate-500 font-mono">v27.8</span>
        </div>

        {/* Global Action Bar */}
        <div className="flex items-center gap-2.5">
          {/* CS2 Target Status */}
          <div className="flex items-center gap-1.5 px-2.5 py-1 rounded bg-[#0c0e14] border border-[#1b202e] text-[11px] font-mono">
            <span className={`w-2 h-2 rounded-full ${telemetry?.targetActive ? 'bg-emerald-400' : 'bg-slate-600'}`} />
            <span className={telemetry?.targetActive ? 'text-slate-200 font-medium' : 'text-slate-500'}>
              {telemetry?.targetName || 'CS2 Process'}
            </span>
          </div>

          {/* Engine Power Toggle */}
          <button
            onClick={toggleSuspend}
            className={`px-2.5 py-1 rounded text-[11px] font-mono font-semibold flex items-center gap-1.5 border transition ${
              telemetry?.suspended 
                ? 'bg-rose-500/10 text-rose-400 border-rose-500/30 hover:bg-rose-500/20' 
                : 'bg-emerald-500/10 text-emerald-400 border-emerald-500/30 hover:bg-emerald-500/20'
            }`}
          >
            <Power className="w-3 h-3" />
            {telemetry?.suspended ? 'SUSPENDED' : 'RUNNING'}
          </button>

          {/* Safe Mode Toggle */}
          <button
            onClick={toggleSafeMode}
            className={`px-2.5 py-1 rounded text-[11px] font-mono font-semibold flex items-center gap-1.5 border transition ${
              config.safeModeEnabled 
                ? 'bg-amber-500/10 text-amber-400 border-amber-500/30 hover:bg-amber-500/20' 
                : 'bg-[#1b202e] text-slate-400 border-transparent hover:text-slate-200'
            }`}
          >
            <Shield className="w-3 h-3" />
            SAFE MODE: {config.safeModeEnabled ? 'ON' : 'OFF'}
          </button>

          {/* Revert Snapshot Button */}
          <button
            onClick={revertToSnapshot}
            title="Restore un-clamped snapshot configuration"
            className="px-2.5 py-1 rounded text-[11px] font-mono font-semibold flex items-center gap-1 bg-[#1b202e] hover:bg-slate-700 text-slate-300 border border-slate-700 transition"
          >
            <RefreshCw className="w-3 h-3 text-cyan-400" />
            REVERT SNAPSHOT
          </button>

          {/* Connection Pill */}
          <div className="flex items-center gap-1.5 pl-2 border-l border-[#1b202e] text-[11px] font-mono text-slate-400">
            <span className={`w-2 h-2 rounded-full ${connected ? 'bg-emerald-400' : 'bg-rose-500'}`} />
            <span>127.0.0.1:{DEFAULT_PORT}</span>
          </div>
        </div>
      </header>

      {/* ── Status Toast ── */}
      {statusMsg && (
        <div className="bg-cyan-500/10 border-b border-cyan-500/20 px-5 py-1.5 text-xs font-mono text-cyan-300 flex items-center justify-between">
          <span>{statusMsg}</span>
          <button onClick={() => setStatusMsg('')} className="text-slate-400 hover:text-white">✕</button>
        </div>
      )}

      {/* ── Tab Bar ── */}
      <div className="border-b border-[#1b202e] bg-[#0f121a] px-5 flex gap-4">
        {[
          { id: 'telemetry', label: 'TELEMETRY HUD', icon: Activity },
          { id: 'profiles', label: 'WEAPON PROFILES & TUNING', icon: Crosshair },
          { id: 'bhop', label: 'BUNNYHOP AUTOMATION', icon: Zap },
          { id: 'physics', label: 'PHYSICS PARAMETERS', icon: Sliders }
        ].map(tab => {
          const Icon = tab.icon;
          const active = activeTab === tab.id;
          return (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id as any)}
              className={`py-2.5 text-xs font-mono font-semibold flex items-center gap-2 border-b-2 transition ${
                active 
                  ? 'border-cyan-400 text-cyan-400' 
                  : 'border-transparent text-slate-400 hover:text-slate-200'
              }`}
            >
              <Icon className="w-3.5 h-3.5" />
              {tab.label}
            </button>
          );
        })}
      </div>

      {/* ── Main Utility Container ── */}
      <main className="p-5 flex-1 max-w-7xl mx-auto w-full space-y-5">
        
        {/* ════ TAB 1: TELEMETRY HUD ════ */}
        {activeTab === 'telemetry' && (
          <div className="space-y-4">
            {/* Top Stat Gauges */}
            <div className="grid grid-cols-1 md:grid-cols-4 gap-3 font-mono">
              {/* Realtime Velocity */}
              <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-3.5">
                <div className="text-[11px] text-slate-400 uppercase tracking-wider">Realtime Velocity</div>
                <div className="flex items-baseline gap-2 mt-1">
                  <span className={`text-2xl font-bold tracking-tight ${curSpeed <= 34.0 ? 'text-emerald-400' : curSpeed <= 150.0 ? 'text-amber-400' : 'text-slate-100'}`}>
                    {curSpeed.toFixed(1)}
                  </span>
                  <span className="text-xs text-slate-500">u/s</span>
                  {curSpeed <= 34.0 && (
                    <span className="text-[10px] px-1.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/30 ml-auto">
                      ACCURATE
                    </span>
                  )}
                </div>
              </div>

              {/* Last Counter-Strafe Result */}
              <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-3.5">
                <div className="text-[11px] text-slate-400 uppercase tracking-wider">Brake Result</div>
                <div className="flex items-center justify-between mt-1">
                  <span className={`text-xl font-bold tracking-tight ${
                    lastResult === 'FINE' ? 'text-emerald-400' : lastResult === 'EARLY' ? 'text-rose-400' : 'text-amber-400'
                  }`}>
                    {lastResult}
                  </span>
                  <span className="text-xs text-slate-400">
                    Pre: {telemetry?.hud?.lastPreSpeed?.toFixed(0) || '215'} u/s
                  </span>
                </div>
              </div>

              {/* Last Brake Duration */}
              <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-3.5">
                <div className="text-[11px] text-slate-400 uppercase tracking-wider">Last Brake Hold</div>
                <div className="flex items-baseline gap-2 mt-1">
                  <span className="text-2xl font-bold text-cyan-300 tracking-tight">
                    {lastBrakeMs || 93}
                  </span>
                  <span className="text-xs text-slate-500">ms ({lastBrakeTicks || 6} ticks)</span>
                </div>
              </div>

              {/* Timer Jitter & CPU Core */}
              <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-3.5">
                <div className="text-[11px] text-slate-400 uppercase tracking-wider">QPC Timer Jitter</div>
                <div className="flex items-baseline gap-2 mt-1">
                  <span className="text-2xl font-bold text-cyan-300 tracking-tight">
                    {telemetry?.metrics?.timerJitterUs || 0}
                  </span>
                  <span className="text-xs text-slate-500">µs (Core #{telemetry?.affinity?.timingCore ?? 0})</span>
                </div>
              </div>
            </div>

            {/* Sub-Tick Visualizer & Rolling Graph */}
            <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
              {/* Sub-Tick Tick Timeline & Key Matrix */}
              <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-4 space-y-4">
                <div className="flex items-center justify-between border-b border-[#1b202e] pb-2">
                  <span className="text-xs font-mono font-bold text-slate-300">SUB-TICK TIMELINE</span>
                  <span className="text-[10px] font-mono text-cyan-400">64-TICK ENGINE STEP</span>
                </div>

                {/* Tick Visualization Blocks */}
                <div className="space-y-1.5 font-mono text-[11px]">
                  <div className="flex justify-between text-slate-400 text-[10px]">
                    <span>T0: Release</span>
                    <span>T1-T6: Counter-Accel</span>
                    <span>T7: Settle (&lt;34 u/s)</span>
                  </div>
                  <div className="grid grid-cols-8 gap-1 h-5">
                    {[1, 2, 3, 4, 5, 6, 7, 8].map(tick => {
                      const isBrakeActive = tick <= (lastBrakeTicks || 6);
                      const isComplete = tick === 7;
                      return (
                        <div
                          key={tick}
                          className={`rounded border flex items-center justify-center text-[10px] font-bold ${
                            isComplete 
                              ? 'bg-emerald-500/20 border-emerald-500/40 text-emerald-300'
                              : isBrakeActive
                                ? 'bg-cyan-500/20 border-cyan-500/40 text-cyan-300'
                                : 'bg-[#0c0e14] border-[#1b202e] text-slate-600'
                          }`}
                        >
                          T{tick}
                        </div>
                      );
                    })}
                  </div>
                </div>

                {/* Movement Key State Matrix */}
                <div className="border-t border-[#1b202e] pt-3">
                  <div className="text-[11px] font-mono text-slate-400 mb-2.5 flex justify-between">
                    <span>KEY ROUTER MATRIX</span>
                    <span className="text-[10px] text-slate-500">PHYS vs SYNTHETIC</span>
                  </div>

                  <div className="grid grid-cols-3 gap-2 max-w-[200px] mx-auto">
                    <div />
                    <div className={`p-2 rounded border text-center font-mono ${
                      telemetry?.keys?.phys[0] ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold' :
                      telemetry?.keys?.logical[0] ? 'bg-amber-500/20 border-amber-500 text-amber-300' : 'bg-[#0c0e14] border-[#1b202e] text-slate-600'
                    }`}>
                      W
                    </div>
                    <div />

                    <div className={`p-2 rounded border text-center font-mono ${
                      telemetry?.keys?.phys[2] ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold' :
                      telemetry?.keys?.logical[2] ? 'bg-amber-500/20 border-amber-500 text-amber-300' : 'bg-[#0c0e14] border-[#1b202e] text-slate-600'
                    }`}>
                      A
                    </div>
                    <div className={`p-2 rounded border text-center font-mono ${
                      telemetry?.keys?.phys[1] ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold' :
                      telemetry?.keys?.logical[1] ? 'bg-amber-500/20 border-amber-500 text-amber-300' : 'bg-[#0c0e14] border-[#1b202e] text-slate-600'
                    }`}>
                      S
                    </div>
                    <div className={`p-2 rounded border text-center font-mono ${
                      telemetry?.keys?.phys[3] ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold' :
                      telemetry?.keys?.logical[3] ? 'bg-amber-500/20 border-amber-500 text-amber-300' : 'bg-[#0c0e14] border-[#1b202e] text-slate-600'
                    }`}>
                      D
                    </div>
                  </div>
                </div>
              </div>

              {/* Real-time Canvas Graph */}
              <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-4 lg:col-span-2 flex flex-col justify-between">
                <div>
                  <div className="flex items-center justify-between border-b border-[#1b202e] pb-2 mb-3">
                    <span className="text-xs font-mono font-bold text-slate-300">REALTIME QPC JITTER & OVERSLEEP TRACE</span>
                    <div className="flex items-center gap-3 text-[11px] font-mono">
                      <span className="text-cyan-400">■ Jitter (µs)</span>
                      <span className="text-amber-400">■ Oversleep (µs)</span>
                    </div>
                  </div>
                  <div className="bg-[#0c0e14] border border-[#1b202e] rounded p-2 h-44">
                    <canvas ref={timelineCanvasRef} width={600} height={160} className="w-full h-full" />
                  </div>
                </div>

                <div className="grid grid-cols-4 gap-2 font-mono text-[11px] mt-3 border-t border-[#1b202e] pt-3 text-slate-400">
                  <div>Hook P50: <span className="text-slate-200 font-semibold">{telemetry?.metrics?.hookLatencyP50Us || 0} µs</span></div>
                  <div>Hook P99: <span className="text-slate-200 font-semibold">{telemetry?.metrics?.hookLatencyP99Us || 0} µs</span></div>
                  <div>Wake Oversleep: <span className="text-amber-400 font-semibold">{telemetry?.metrics?.wakeOversleepUs || 0} µs</span></div>
                  <div>Spin Duration: <span className="text-cyan-400 font-semibold">{telemetry?.metrics?.spinDurationUs || 0} µs</span></div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ════ TAB 2: WEAPON PROFILES & TUNING ════ */}
        {activeTab === 'profiles' && (
          <div className="space-y-4">
            {/* Weapon Selector Cards */}
            <div className="grid grid-cols-2 md:grid-cols-4 gap-3">
              {[
                { idx: 1, name: 'RIFLE', desc: 'AK-47 / M4A4 / M4A1-S', threshold: 34.0, dur: '109 ms' },
                { idx: 2, name: 'PISTOL', desc: 'USP-S / Glock / Deagle', threshold: 34.0, dur: '109 ms' },
                { idx: 3, name: 'SNIPER', desc: 'AWP / SSG 08', threshold: 17.0, dur: '125 ms' },
                { idx: 4, name: 'SMG', desc: 'MP9 / MAC-10 / MP7', threshold: 34.0, dur: '109 ms' }
              ].map(w => {
                const isActive = config.activeBrakeProfileIndex === w.idx;
                const isSelected = selectedProfileIndex === w.idx;
                return (
                  <div
                    key={w.idx}
                    onClick={() => selectWeaponProfile(w.idx)}
                    className={`p-3.5 rounded-lg border cursor-pointer transition ${
                      isActive 
                        ? 'bg-cyan-500/10 border-cyan-500/50 text-slate-100' 
                        : isSelected
                          ? 'bg-[#181d2c] border-slate-600 text-slate-200'
                          : 'bg-[#121622] border-[#1b202e] text-slate-400 hover:border-slate-700'
                    }`}
                  >
                    <div className="flex items-center justify-between">
                      <span className="font-mono font-bold text-sm tracking-wide">{w.name}</span>
                      {isActive && (
                        <span className="text-[10px] font-mono text-cyan-400 font-bold flex items-center gap-1">
                          <Check className="w-3 h-3" /> ACTIVE
                        </span>
                      )}
                    </div>
                    <p className="text-[11px] text-slate-500 mt-1">{w.desc}</p>
                    <div className="flex justify-between items-center mt-3 text-[10px] font-mono text-slate-400">
                      <span>Threshold: <strong className="text-slate-200">{w.threshold} u/s</strong></span>
                      <span>Brake: <strong className="text-cyan-400">{w.dur}</strong></span>
                    </div>
                  </div>
                );
              })}
            </div>

            {/* Profile Detail Tuner */}
            <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-5 space-y-5">
              <div className="flex items-center justify-between border-b border-[#1b202e] pb-3">
                <div>
                  <div className="text-sm font-mono font-bold text-slate-200 flex items-center gap-2">
                    <span>EDITING PROFILE: {PROFILE_NAMES[selectedProfileIndex]}</span>
                    <span className="text-xs px-2 py-0.5 rounded bg-[#1b202e] text-cyan-400 font-mono">
                      Target Accuracy: &le; {activeWeapon.accuracyThreshold.toFixed(1)} u/s
                    </span>
                  </div>
                  <p className="text-[11px] text-slate-500 font-mono mt-0.5">
                    Sub-tick deceleration simulation parameters aligned with Valve PM_Friction / Accelerate
                  </p>
                </div>

                <button
                  onClick={saveConfig}
                  disabled={saving}
                  className="px-3.5 py-1.5 bg-cyan-600 hover:bg-cyan-500 text-white font-mono font-semibold text-xs rounded transition flex items-center gap-1.5"
                >
                  <Save className="w-3.5 h-3.5" />
                  {saving ? 'SAVING...' : 'APPLY & SAVE (MARCO.INI)'}
                </button>
              </div>

              {/* Sliders Grid */}
              <div className="grid grid-cols-1 md:grid-cols-2 gap-5 font-mono text-xs">
                {/* Accuracy Threshold */}
                <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-slate-300 font-medium">Accuracy Threshold:</span>
                    <span className="text-cyan-400 font-bold">{activeWeapon.accuracyThreshold.toFixed(1)} u/s</span>
                  </div>
                  <input
                    type="range"
                    min="10"
                    max="60"
                    step="1"
                    value={activeWeapon.accuracyThreshold}
                    onChange={(e) => updateProfileField('accuracyThreshold', parseFloat(e.target.value))}
                    className="w-full accent-cyan-500"
                  />
                  <div className="text-[10px] text-slate-500 flex justify-between">
                    <span>17.0 u/s (Sniper standard)</span>
                    <span>34.0 u/s (Rifle/Pistol standard)</span>
                  </div>
                </div>

                {/* Key Overlap Window */}
                <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-slate-300 font-medium">Key Overlap Window:</span>
                    <span className="text-cyan-400 font-bold">{activeWeapon.overlapDurationUs} µs</span>
                  </div>
                  <input
                    type="range"
                    min="0"
                    max="8000"
                    step="500"
                    value={activeWeapon.overlapDurationUs}
                    onChange={(e) => updateProfileField('overlapDurationUs', parseInt(e.target.value, 10))}
                    className="w-full accent-cyan-500"
                  />
                  <div className="text-[10px] text-slate-500">
                    Standard is 0 µs (zero overlap ensures instantaneous sub-tick counter-braking).
                  </div>
                </div>

                {/* Brake Bias Multiplier */}
                <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-slate-300 font-medium">Brake Bias Multiplier:</span>
                    <span className="text-cyan-400 font-bold">{activeWeapon.brakeBiasMultiplier.toFixed(2)}x</span>
                  </div>
                  <input
                    type="range"
                    min="0.80"
                    max="1.30"
                    step="0.01"
                    value={activeWeapon.brakeBiasMultiplier}
                    onChange={(e) => updateProfileField('brakeBiasMultiplier', parseFloat(e.target.value))}
                    className="w-full accent-cyan-500"
                  />
                  <div className="text-[10px] text-slate-500">
                    Standard is 1.00x (pure mathematical physics deceleration without arbitrary scaling).
                  </div>
                </div>

                {/* Momentum Memory */}
                <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-slate-300 font-medium">Momentum Memory:</span>
                    <span className="text-cyan-400 font-bold">{activeWeapon.momentumMemoryMs.toFixed(1)} ms</span>
                  </div>
                  <input
                    type="range"
                    min="10"
                    max="60"
                    step="1"
                    value={activeWeapon.momentumMemoryMs}
                    onChange={(e) => updateProfileField('momentumMemoryMs', parseFloat(e.target.value))}
                    className="w-full accent-cyan-500"
                  />
                  <div className="text-[10px] text-slate-500">
                    Memory horizon across rapid A-D alternating strafes (Rifle: 35ms, Sniper: 40ms).
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ════ TAB 3: BUNNYHOP AUTOMATION ════ */}
        {activeTab === 'bhop' && (
          <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-5 space-y-4">
            <div className="flex items-center justify-between border-b border-[#1b202e] pb-3">
              <div>
                <span className="text-sm font-mono font-bold text-slate-200">BUNNYHOP AUTOMATION</span>
                <p className="text-[11px] text-slate-500 font-mono mt-0.5">
                  Sub-tick scroll wheel pulse generation and cadence alignment
                </p>
              </div>
              <button
                onClick={saveConfig}
                disabled={saving}
                className="px-3.5 py-1.5 bg-cyan-600 hover:bg-cyan-500 text-white font-mono font-semibold text-xs rounded transition flex items-center gap-1.5"
              >
                <Save className="w-3.5 h-3.5" />
                {saving ? 'SAVING...' : 'SAVE TO MARCO.INI'}
              </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 font-mono text-xs">
              <div className="space-y-3">
                <div className="flex items-center justify-between p-3 bg-[#0c0e14] rounded border border-[#1b202e]">
                  <span className="text-slate-300">Bhop Subsystem:</span>
                  <button
                    onClick={() => setConfig(prev => ({ ...prev, bhopEnabled: !prev.bhopEnabled }))}
                    className={`px-3 py-1 rounded font-bold ${
                      config.bhopEnabled 
                        ? 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/40' 
                        : 'bg-slate-800 text-slate-400'
                    }`}
                  >
                    {config.bhopEnabled ? 'ENABLED' : 'DISABLED'}
                  </button>
                </div>

                <div className="space-y-1.5">
                  <span className="text-slate-400 text-[11px]">Execution Cadence Mode:</span>
                  <div className="grid grid-cols-2 gap-2">
                    {[
                      { id: 1, name: '1: Legit' },
                      { id: 2, name: '2: Aggressive' },
                      { id: 3, name: '3: Humanized' },
                      { id: 4, name: '4: Scroll Emulation' }
                    ].map(m => (
                      <button
                        key={m.id}
                        onClick={() => setConfig(prev => ({ ...prev, bhopMode: m.id }))}
                        className={`p-2.5 rounded border text-left transition ${
                          config.bhopMode === m.id
                            ? 'bg-cyan-500/10 border-cyan-500/60 text-cyan-300 font-bold'
                            : 'bg-[#0c0e14] border-[#1b202e] text-slate-400 hover:text-slate-200'
                        }`}
                      >
                        {m.name}
                      </button>
                    ))}
                  </div>
                </div>
              </div>

              <div className="space-y-3">
                <div className="bg-[#0c0e14] border border-[#1b202e] p-3 rounded space-y-2">
                  <div className="flex justify-between">
                    <span className="text-slate-300">Airborne Lock Delay:</span>
                    <span className="text-cyan-400 font-bold">{config.airborneDelayMs} ms</span>
                  </div>
                  <input
                    type="range"
                    min="100"
                    max="600"
                    step="10"
                    value={config.airborneDelayMs}
                    onChange={(e) => setConfig(prev => ({ ...prev, airborneDelayMs: parseInt(e.target.value, 10) }))}
                    className="w-full accent-cyan-500"
                  />
                </div>

                <div className="bg-[#0c0e14] border border-[#1b202e] p-3 rounded space-y-2">
                  <div className="flex justify-between">
                    <span className="text-slate-300">Scroll Burst Gap:</span>
                    <span className="text-cyan-400 font-bold">{config.scrollBurstGapMs} ms</span>
                  </div>
                  <input
                    type="range"
                    min="1"
                    max="10"
                    step="1"
                    value={config.scrollBurstGapMs}
                    onChange={(e) => setConfig(prev => ({ ...prev, scrollBurstGapMs: parseInt(e.target.value, 10) }))}
                    className="w-full accent-cyan-500"
                  />
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ════ TAB 4: PHYSICS CONSTANTS ════ */}
        {activeTab === 'physics' && (
          <div className="bg-[#121622] border border-[#1b202e] rounded-lg p-5 space-y-4">
            <div className="flex items-center justify-between border-b border-[#1b202e] pb-3">
              <div>
                <span className="text-sm font-mono font-bold text-slate-200">VALVE SOURCE SDK PHYSICS PIPELINE</span>
                <p className="text-[11px] text-slate-500 font-mono mt-0.5">
                  Internal simulation parameters matching Counter-Strike 2 engine settings
                </p>
              </div>
              <button
                onClick={saveConfig}
                disabled={saving}
                className="px-3.5 py-1.5 bg-cyan-600 hover:bg-cyan-500 text-white font-mono font-semibold text-xs rounded transition flex items-center gap-1.5"
              >
                <Save className="w-3.5 h-3.5" />
                {saving ? 'SAVING...' : 'SAVE TO MARCO.INI'}
              </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 font-mono text-xs">
              <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_friction (Friction Coefficient):</span>
                  <span className="text-cyan-400 font-bold">{config.physFriction.toFixed(2)}</span>
                </div>
                <input
                  type="range"
                  min="2.0"
                  max="10.0"
                  step="0.1"
                  value={config.physFriction}
                  onChange={(e) => setConfig(prev => ({ ...prev, physFriction: parseFloat(e.target.value) }))}
                  className="w-full accent-cyan-500"
                />
              </div>

              <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_accelerate (Opposing Acceleration):</span>
                  <span className="text-cyan-400 font-bold">{config.physAccelerate.toFixed(2)}</span>
                </div>
                <input
                  type="range"
                  min="2.0"
                  max="10.0"
                  step="0.1"
                  value={config.physAccelerate}
                  onChange={(e) => setConfig(prev => ({ ...prev, physAccelerate: parseFloat(e.target.value) }))}
                  className="w-full accent-cyan-500"
                />
              </div>

              <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_stopspeed:</span>
                  <span className="text-cyan-400 font-bold">{config.physStopSpeed.toFixed(1)} u/s</span>
                </div>
                <input
                  type="range"
                  min="40.0"
                  max="120.0"
                  step="1.0"
                  value={config.physStopSpeed}
                  onChange={(e) => setConfig(prev => ({ ...prev, physStopSpeed: parseFloat(e.target.value) }))}
                  className="w-full accent-cyan-500"
                />
              </div>

              <div className="bg-[#0c0e14] border border-[#1b202e] p-3.5 rounded space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_maxspeed:</span>
                  <span className="text-cyan-400 font-bold">{config.physMaxSpeed.toFixed(1)} u/s</span>
                </div>
                <input
                  type="range"
                  min="200.0"
                  max="300.0"
                  step="1.0"
                  value={config.physMaxSpeed}
                  onChange={(e) => setConfig(prev => ({ ...prev, physMaxSpeed: parseFloat(e.target.value) }))}
                  className="w-full accent-cyan-500"
                />
              </div>
            </div>
          </div>
        )}
      </main>
    </div>
  );
}
