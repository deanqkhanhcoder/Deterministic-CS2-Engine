import React, { useState, useEffect, useRef } from 'react';
import { 
  Activity, Shield, ShieldAlert, Cpu, Zap, RefreshCw, Power, 
  Crosshair, Sliders, CheckCircle, AlertTriangle, KeyRound
} from 'lucide-react';
import { RuntimeConfig, TelemetryData, BrakeProfile } from './types';

const DEFAULT_PORT = 47650;
const PROFILE_NAMES = ['Disabled', 'Rifle (AK/M4)', 'Pistol (USP/Glock)', 'Sniper (AWP/Scout)', 'SMG (MP9/Mac10)'];

export default function App() {
  const [token, setToken] = useState<string>(() => {
    const urlParams = new URLSearchParams(window.location.search);
    return urlParams.get('token') || localStorage.getItem('marco_token') || '';
  });
  const [host, setHost] = useState<string>(() => {
    return window.location.hostname ? `http://${window.location.hostname}:${DEFAULT_PORT}` : `http://127.0.0.1:${DEFAULT_PORT}`;
  });

  const [connected, setConnected] = useState<boolean>(false);
  const [config, setConfig] = useState<RuntimeConfig | null>(null);
  const [telemetry, setTelemetry] = useState<TelemetryData | null>(null);
  const [selectedProfileIndex, setSelectedProfileIndex] = useState<number>(1);
  const [activeTab, setActiveTab] = useState<'telemetry' | 'profiles' | 'bhop' | 'physics'>('telemetry');
  const [saving, setSaving] = useState<boolean>(false);
  const [statusMsg, setStatusMsg] = useState<string>('');

  const eventSourceRef = useRef<EventSource | null>(null);
  const timelineCanvasRef = useRef<HTMLCanvasElement | null>(null);

  // Sync token to localStorage and URL
  useEffect(() => {
    if (token) {
      localStorage.setItem('marco_token', token);
    }
  }, [token]);

  // Fetch initial config
  const fetchConfig = async () => {
    try {
      const res = await fetch(`${host}/api/config`, {
        headers: { 'X-Marco-Token': token }
      });
      if (res.ok) {
        const data = await res.json();
        setConfig(data);
        setSelectedProfileIndex(data.activeBrakeProfileIndex || 1);
        setConnected(true);
      } else if (res.status === 401) {
        setConnected(false);
        setStatusMsg('Auth failed: invalid token');
      }
    } catch {
      setConnected(false);
    }
  };

  // Connect SSE for Realtime Telemetry (~30Hz)
  useEffect(() => {
    if (!token && window.location.port !== `${DEFAULT_PORT}`) return;

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
        console.error('SSE telemetry parse err', err);
      }
    });

    es.addEventListener('state_changed', (e) => {
      try {
        const data = JSON.parse(e.data);
        setTelemetry(data);
        fetchConfig();
      } catch (err) {
        console.error('SSE state err', err);
      }
    });

    es.onerror = () => {
      setConnected(false);
    };

    return () => {
      es.close();
    };
  }, [host, token]);

  // Draw real-time timeline graph on Canvas
  useEffect(() => {
    const canvas = timelineCanvasRef.current;
    if (!canvas || !telemetry?.timeline) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const { width, height } = canvas;
    ctx.clearRect(0, 0, width, height);

    // Draw background grid lines
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

    // Jitter Line (Cyan)
    ctx.strokeStyle = '#06b6d4';
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < count; i++) {
      const val = Math.min(jitter[i] / 10, maxVal);
      const y = height - (val / maxVal) * (height - 10) - 5;
      if (i === 0) ctx.moveTo(0, y);
      else ctx.lineTo(i * step, y);
    }
    ctx.stroke();

    // Oversleep Line (Amber)
    ctx.strokeStyle = '#f59e0b';
    ctx.lineWidth = 1.5;
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
    if (!config) return;
    const nextVal = !config.safeModeEnabled;
    try {
      const res = await fetch(`${host}/api/safemode`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
        body: JSON.stringify({ enabled: nextVal })
      });
      if (res.ok) {
        setConfig(prev => prev ? { ...prev, safeModeEnabled: nextVal } : null);
        setStatusMsg(`Safe Mode ${nextVal ? 'Activated' : 'Deactivated'}`);
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
        setStatusMsg('Successfully reverted to pre-safemode configuration snapshot!');
      }
    } catch (err) {
      console.error(err);
    }
  };

  const selectWeaponProfile = async (idx: number) => {
    try {
      const res = await fetch(`${host}/api/profile`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
        body: JSON.stringify({ index: idx })
      });
      if (res.ok) {
        setSelectedProfileIndex(idx);
        setConfig(prev => prev ? { ...prev, activeBrakeProfileIndex: idx } : null);
        setStatusMsg(`Switched weapon profile to: ${PROFILE_NAMES[idx]}`);
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
    if (!config) return;
    setSaving(true);
    try {
      const res = await fetch(`${host}/api/config`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
        body: JSON.stringify(config)
      });
      if (res.ok) {
        setStatusMsg('Configuration saved successfully to marco.ini');
      }
    } catch (err) {
      console.error(err);
      setStatusMsg('Failed to save configuration');
    } finally {
      setSaving(false);
    }
  };

  const updateCurrentProfileField = (field: keyof BrakeProfile, val: number) => {
    if (!config) return;
    const profiles = [...config.brakeProfiles];
    profiles[selectedProfileIndex] = {
      ...profiles[selectedProfileIndex],
      [field]: val
    };
    setConfig({ ...config, brakeProfiles: profiles });
  };

  return (
    <div className="min-h-screen bg-[#090a0f] text-slate-100 flex flex-col">
      {/* ── Top Header Navigation ── */}
      <header className="border-b border-[#1e2433] bg-[#11141c]/80 backdrop-blur px-6 py-3.5 flex flex-wrap items-center justify-between gap-4 sticky top-0 z-50">
        <div className="flex items-center gap-3">
          <div className="w-9 h-9 rounded-lg bg-amber-500/10 border border-amber-500/30 flex items-center justify-center text-amber-400 font-black">
            M
          </div>
          <div>
            <div className="flex items-center gap-2">
              <span className="font-bold tracking-wider text-base">MARCO ENGINE</span>
              <span className="text-xs px-2 py-0.5 rounded bg-amber-500/20 text-amber-300 font-mono font-medium border border-amber-500/30">
                v27.7 HEADLESS
              </span>
            </div>
            <p className="text-xs text-slate-400 font-mono">Deterministic Sub-Tick Motion Service</p>
          </div>
        </div>

        {/* Global Controls & Status */}
        <div className="flex items-center gap-3">
          {/* Target CS2 Focus Indicator */}
          <div className="flex items-center gap-2 px-3 py-1.5 rounded-md bg-[#090a0f] border border-[#1e2433] text-xs font-mono">
            <span className={`w-2 h-2 rounded-full ${telemetry?.targetActive ? 'bg-emerald-400 animate-pulse' : 'bg-slate-600'}`} />
            <span className="text-slate-300">
              {telemetry?.targetName || 'CS2 Process'}
            </span>
          </div>

          {/* Suspend Toggle */}
          <button
            onClick={toggleSuspend}
            className={`px-3 py-1.5 rounded-md text-xs font-mono font-semibold flex items-center gap-1.5 border transition ${
              telemetry?.suspended 
                ? 'bg-rose-500/20 text-rose-300 border-rose-500/40 hover:bg-rose-500/30' 
                : 'bg-emerald-500/20 text-emerald-300 border-emerald-500/40 hover:bg-emerald-500/30'
            }`}
          >
            <Power className="w-3.5 h-3.5" />
            {telemetry?.suspended ? 'SUSPENDED' : 'ACTIVE'}
          </button>

          {/* Safe Mode Toggle */}
          <button
            onClick={toggleSafeMode}
            className={`px-3 py-1.5 rounded-md text-xs font-mono font-semibold flex items-center gap-1.5 border transition ${
              config?.safeModeEnabled 
                ? 'bg-amber-500/20 text-amber-300 border-amber-500/50 hover:bg-amber-500/30' 
                : 'bg-[#1e2433] text-slate-400 border-transparent hover:text-slate-200'
            }`}
          >
            <Shield className="w-3.5 h-3.5" />
            SAFE MODE {config?.safeModeEnabled ? 'ON' : 'OFF'}
          </button>

          {/* Emergency Snapshot Revert Button */}
          <button
            onClick={revertToSnapshot}
            title="Restore un-overridden configuration snapshot"
            className="px-3 py-1.5 rounded-md text-xs font-mono font-semibold flex items-center gap-1.5 bg-[#1e2433] hover:bg-slate-700 text-slate-300 border border-slate-600 transition"
          >
            <RefreshCw className="w-3.5 h-3.5 text-cyan-400" />
            REVERT SNAPSHOT
          </button>

          {/* Connection Status Indicator */}
          <div className="flex items-center gap-2 pl-2 border-l border-slate-800">
            <span className={`w-2.5 h-2.5 rounded-full ${connected ? 'bg-emerald-400' : 'bg-rose-500 animate-ping'}`} />
            <span className="text-xs font-mono text-slate-400">
              {connected ? 'CONNECTED' : 'DISCONNECTED'}
            </span>
          </div>
        </div>
      </header>

      {/* ── Status Toast / Banner ── */}
      {statusMsg && (
        <div className="bg-amber-500/10 border-b border-amber-500/20 px-6 py-2 text-xs font-mono text-amber-300 flex items-center justify-between">
          <span>{statusMsg}</span>
          <button onClick={() => setStatusMsg('')} className="text-slate-400 hover:text-white">✕</button>
        </div>
      )}

      {/* ── Token Authentication Bar (when disconnected) ── */}
      {!connected && (
        <div className="bg-[#11141c] border-b border-rose-500/30 px-6 py-3 flex items-center gap-4 text-xs font-mono">
          <AlertTriangle className="w-4 h-4 text-rose-400 shrink-0" />
          <span className="text-slate-300">IPC Daemon Connection:</span>
          <input
            type="text"
            placeholder="Daemon URL"
            value={host}
            onChange={(e) => setHost(e.target.value)}
            className="bg-[#090a0f] border border-[#1e2433] rounded px-2.5 py-1 text-slate-200 w-64 focus:outline-none focus:border-cyan-500"
          />
          <div className="flex items-center gap-2">
            <KeyRound className="w-3.5 h-3.5 text-amber-400" />
            <input
              type="text"
              placeholder="Paste token from ./marco.token"
              value={token}
              onChange={(e) => setToken(e.target.value)}
              className="bg-[#090a0f] border border-[#1e2433] rounded px-2.5 py-1 text-slate-200 w-72 focus:outline-none focus:border-amber-500"
            />
          </div>
          <button 
            onClick={fetchConfig}
            className="px-3 py-1 rounded bg-cyan-600 hover:bg-cyan-500 text-white font-medium transition"
          >
            Connect
          </button>
        </div>
      )}

      {/* ── Navigation Tabs ── */}
      <div className="border-b border-[#1e2433] bg-[#0d0f17] px-6 flex gap-6">
        {[
          { id: 'telemetry', label: 'TELEMETRY & PHYSICS', icon: Activity },
          { id: 'profiles', label: 'WEAPON PROFILES & TUNING', icon: Crosshair },
          { id: 'bhop', label: 'BUNNYHOP AUTOMATION', icon: Zap },
          { id: 'physics', label: 'SUB-TICK ENGINE PARAMS', icon: Sliders }
        ].map(tab => {
          const Icon = tab.icon;
          const active = activeTab === tab.id;
          return (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id as any)}
              className={`py-3 text-xs font-mono font-semibold flex items-center gap-2 border-b-2 transition ${
                active 
                  ? 'border-amber-400 text-amber-400' 
                  : 'border-transparent text-slate-400 hover:text-slate-200 hover:border-slate-700'
              }`}
            >
              <Icon className="w-3.5 h-3.5" />
              {tab.label}
            </button>
          );
        })}
      </div>

      {/* ── Main Workspace Content ── */}
      <main className="p-6 flex-1 max-w-7xl mx-auto w-full space-y-6">
        {/* ══ Tab 1: Telemetry & Physics Visualizer ══ */}
        {activeTab === 'telemetry' && (
          <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
            {/* Movement Key State Sub-tick Visualizer */}
            <div className="bg-[#11141c] border border-[#1e2433] rounded-xl p-5 flex flex-col justify-between">
              <div>
                <h3 className="text-xs font-mono font-bold text-slate-400 tracking-wider mb-4 flex items-center justify-between">
                  <span>SUB-TICK KEY RECONSTRUCTION</span>
                  <span className="text-[10px] text-cyan-400 font-normal">PHYSICAL vs LOGICAL</span>
                </h3>

                {/* Key Grid Layout */}
                <div className="grid grid-cols-3 gap-3 max-w-[240px] mx-auto my-4">
                  <div />
                  {/* W Key */}
                  <div className={`p-3 rounded-lg border text-center font-mono transition ${
                    telemetry?.keys.phys[0] 
                      ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold shadow-lg shadow-cyan-500/20' 
                      : telemetry?.keys.logical[0]
                        ? 'bg-amber-500/20 border-amber-500 text-amber-300'
                        : 'bg-[#090a0f] border-[#1e2433] text-slate-500'
                  }`}>
                    <div className="text-sm">W</div>
                    <div className="text-[9px] uppercase tracking-tighter">
                      {telemetry?.keys.phys[0] ? 'PHYS' : telemetry?.keys.logical[0] ? 'BRAKE' : 'IDLE'}
                    </div>
                  </div>
                  <div />

                  {/* A Key */}
                  <div className={`p-3 rounded-lg border text-center font-mono transition ${
                    telemetry?.keys.phys[2] 
                      ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold shadow-lg shadow-cyan-500/20' 
                      : telemetry?.keys.logical[2]
                        ? 'bg-amber-500/20 border-amber-500 text-amber-300'
                        : 'bg-[#090a0f] border-[#1e2433] text-slate-500'
                  }`}>
                    <div className="text-sm">A</div>
                    <div className="text-[9px] uppercase tracking-tighter">
                      {telemetry?.keys.phys[2] ? 'PHYS' : telemetry?.keys.logical[2] ? 'BRAKE' : 'IDLE'}
                    </div>
                  </div>

                  {/* S Key */}
                  <div className={`p-3 rounded-lg border text-center font-mono transition ${
                    telemetry?.keys.phys[1] 
                      ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold shadow-lg shadow-cyan-500/20' 
                      : telemetry?.keys.logical[1]
                        ? 'bg-amber-500/20 border-amber-500 text-amber-300'
                        : 'bg-[#090a0f] border-[#1e2433] text-slate-500'
                  }`}>
                    <div className="text-sm">S</div>
                    <div className="text-[9px] uppercase tracking-tighter">
                      {telemetry?.keys.phys[1] ? 'PHYS' : telemetry?.keys.logical[1] ? 'BRAKE' : 'IDLE'}
                    </div>
                  </div>

                  {/* D Key */}
                  <div className={`p-3 rounded-lg border text-center font-mono transition ${
                    telemetry?.keys.phys[3] 
                      ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 font-bold shadow-lg shadow-cyan-500/20' 
                      : telemetry?.keys.logical[3]
                        ? 'bg-amber-500/20 border-amber-500 text-amber-300'
                        : 'bg-[#090a0f] border-[#1e2433] text-slate-500'
                  }`}>
                    <div className="text-sm">D</div>
                    <div className="text-[9px] uppercase tracking-tighter">
                      {telemetry?.keys.phys[3] ? 'PHYS' : telemetry?.keys.logical[3] ? 'BRAKE' : 'IDLE'}
                    </div>
                  </div>
                </div>
              </div>

              {/* Axis States */}
              <div className="border-t border-[#1e2433] pt-4 space-y-2 font-mono text-xs">
                <div className="flex justify-between items-center text-slate-400">
                  <span>Strafe Axis (X):</span>
                  <span className="font-semibold text-slate-200">
                    {telemetry?.axisStateX === 1 ? 'RIGHT (D)' : telemetry?.axisStateX === 2 ? 'LEFT (A)' : telemetry?.axisStateX === 3 ? 'CONFLICT' : 'NEUTRAL'}
                  </span>
                </div>
                <div className="flex justify-between items-center text-slate-400">
                  <span>Forward Axis (Y):</span>
                  <span className="font-semibold text-slate-200">
                    {telemetry?.axisStateY === 1 ? 'FORWARD (W)' : telemetry?.axisStateY === 2 ? 'BACK (S)' : telemetry?.axisStateY === 3 ? 'CONFLICT' : 'NEUTRAL'}
                  </span>
                </div>
              </div>
            </div>

            {/* Performance & Jitter Meters */}
            <div className="bg-[#11141c] border border-[#1e2433] rounded-xl p-5 lg:col-span-2 flex flex-col justify-between">
              <div>
                <div className="flex items-center justify-between mb-4">
                  <h3 className="text-xs font-mono font-bold text-slate-400 tracking-wider">
                    MICROSECOND PRECISION TELEMETRY
                  </h3>
                  <div className="flex items-center gap-4 text-xs font-mono">
                    <span className="flex items-center gap-1.5 text-cyan-400">
                      <span className="w-2 h-2 rounded-full bg-cyan-400" /> Jitter (p50)
                    </span>
                    <span className="flex items-center gap-1.5 text-amber-400">
                      <span className="w-2 h-2 rounded-full bg-amber-400" /> Oversleep
                    </span>
                  </div>
                </div>

                {/* Live Real-time Canvas Graph */}
                <div className="bg-[#090a0f] border border-[#1e2433] rounded-lg p-2 h-44 mb-4">
                  <canvas ref={timelineCanvasRef} width={600} height={160} className="w-full h-full" />
                </div>
              </div>

              {/* Real-time Hardware Metrics Grid */}
              <div className="grid grid-cols-2 md:grid-cols-4 gap-3 font-mono">
                <div className="bg-[#090a0f] p-3 rounded-lg border border-[#1e2433]">
                  <div className="text-[10px] text-slate-400 uppercase">Hook Latency p50</div>
                  <div className="text-lg font-bold text-cyan-300 mt-1">
                    {telemetry?.metrics.hookLatencyP50Us || 0} <span className="text-xs font-normal text-slate-500">µs</span>
                  </div>
                </div>

                <div className="bg-[#090a0f] p-3 rounded-lg border border-[#1e2433]">
                  <div className="text-[10px] text-slate-400 uppercase">Timer Jitter</div>
                  <div className="text-lg font-bold text-cyan-300 mt-1">
                    {telemetry?.metrics.timerJitterUs || 0} <span className="text-xs font-normal text-slate-500">µs</span>
                  </div>
                </div>

                <div className="bg-[#090a0f] p-3 rounded-lg border border-[#1e2433]">
                  <div className="text-[10px] text-slate-400 uppercase">Wake Oversleep</div>
                  <div className="text-lg font-bold text-amber-300 mt-1">
                    {telemetry?.metrics.wakeOversleepUs || 0} <span className="text-xs font-normal text-slate-500">µs</span>
                  </div>
                </div>

                <div className="bg-[#090a0f] p-3 rounded-lg border border-[#1e2433]">
                  <div className="text-[10px] text-slate-400 uppercase">CPU Core Affinity</div>
                  <div className="text-sm font-bold text-emerald-400 mt-1 flex items-center gap-1">
                    <Cpu className="w-3.5 h-3.5" />
                    Core #{telemetry?.affinity.timingCore || 0}
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ══ Tab 2: Weapon Profiles & Tuning ══ */}
        {activeTab === 'profiles' && (
          <div className="space-y-6">
            {/* Weapon Selector Cards */}
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
              {[
                { idx: 1, name: 'RIFLE', desc: 'AK-47 / M4A4 / M4A1-S', threshold: '34 u/s' },
                { idx: 2, name: 'PISTOL', desc: 'USP-S / Glock / Deagle', threshold: '34 u/s' },
                { idx: 3, name: 'SNIPER', desc: 'AWP / SSG 08', threshold: '17 u/s' },
                { idx: 4, name: 'SMG', desc: 'MP9 / MAC-10 / MP7', threshold: '34 u/s' }
              ].map(w => {
                const isActive = (config?.activeBrakeProfileIndex || 1) === w.idx;
                const isSelected = selectedProfileIndex === w.idx;
                return (
                  <div
                    key={w.idx}
                    onClick={() => {
                      setSelectedProfileIndex(w.idx);
                      selectWeaponProfile(w.idx);
                    }}
                    className={`p-4 rounded-xl border cursor-pointer transition relative ${
                      isActive 
                        ? 'bg-amber-500/10 border-amber-500/60 shadow-lg shadow-amber-500/10' 
                        : isSelected
                          ? 'bg-slate-800/50 border-cyan-500/50'
                          : 'bg-[#11141c] border-[#1e2433] hover:border-slate-700'
                    }`}
                  >
                    {isActive && (
                      <div className="absolute top-3 right-3 flex items-center gap-1 text-[10px] font-mono text-amber-400">
                        <CheckCircle className="w-3.5 h-3.5" /> ACTIVE
                      </div>
                    )}
                    <div className="text-base font-bold font-mono text-slate-100">{w.name}</div>
                    <div className="text-xs text-slate-400 mt-1">{w.desc}</div>
                    <div className="text-[10px] font-mono text-cyan-400 mt-3">Accuracy Threshold: {w.threshold}</div>
                  </div>
                );
              })}
            </div>

            {/* Profile Tuner Sliders */}
            {config?.brakeProfiles[selectedProfileIndex] && (
              <div className="bg-[#11141c] border border-[#1e2433] rounded-xl p-6 space-y-6">
                <div className="flex items-center justify-between border-b border-[#1e2433] pb-4">
                  <div>
                    <h3 className="text-sm font-mono font-bold text-slate-200">
                      TUNING PROFILE: {PROFILE_NAMES[selectedProfileIndex]}
                    </h3>
                    <p className="text-xs text-slate-400 font-mono mt-0.5">
                      Adjust exact physical deceleration parameters in real-time
                    </p>
                  </div>
                  <button
                    onClick={saveConfig}
                    disabled={saving}
                    className="px-4 py-2 bg-amber-500 hover:bg-amber-400 text-black font-mono font-bold text-xs rounded-lg transition"
                  >
                    {saving ? 'SAVING...' : 'SAVE TO MARCO.INI'}
                  </button>
                </div>

                <div className="grid grid-cols-1 md:grid-cols-2 gap-6 font-mono text-xs">
                  {/* Accuracy Threshold Slider */}
                  <div className="space-y-2">
                    <div className="flex justify-between">
                      <span className="text-slate-300">Accuracy Threshold (Stop Speed):</span>
                      <span className="text-amber-400 font-bold">
                        {config.brakeProfiles[selectedProfileIndex].accuracyThreshold.toFixed(1)} u/s
                      </span>
                    </div>
                    <input
                      type="range"
                      min="10"
                      max="60"
                      step="1"
                      value={config.brakeProfiles[selectedProfileIndex].accuracyThreshold}
                      onChange={(e) => updateCurrentProfileField('accuracyThreshold', parseFloat(e.target.value))}
                      className="w-full accent-amber-500"
                    />
                    <p className="text-[10px] text-slate-500">
                      Rifle standard is 34.0 u/s (Sniper: 17.0 u/s). Simulation terminates when velocity drops below this.
                    </p>
                  </div>

                  {/* Overlap Duration (Must be 0us) */}
                  <div className="space-y-2">
                    <div className="flex justify-between">
                      <span className="text-slate-300">Key Overlap Window:</span>
                      <span className="text-cyan-400 font-bold">
                        {config.brakeProfiles[selectedProfileIndex].overlapDurationUs} µs
                      </span>
                    </div>
                    <input
                      type="range"
                      min="0"
                      max="10000"
                      step="500"
                      value={config.brakeProfiles[selectedProfileIndex].overlapDurationUs}
                      onChange={(e) => updateCurrentProfileField('overlapDurationUs', parseInt(e.target.value, 10))}
                      className="w-full accent-cyan-500"
                    />
                    <p className="text-[10px] text-slate-500">
                      Set to 0 µs to eliminate overlap and preserve full counter-acceleration on sub-tick.
                    </p>
                  </div>

                  {/* Brake Bias Multiplier */}
                  <div className="space-y-2">
                    <div className="flex justify-between">
                      <span className="text-slate-300">Brake Bias Multiplier:</span>
                      <span className="text-amber-400 font-bold">
                        {config.brakeProfiles[selectedProfileIndex].brake_bias_multiplier.toFixed(2)}x
                      </span>
                    </div>
                    <input
                      type="range"
                      min="0.80"
                      max="1.30"
                      step="0.01"
                      value={config.brakeProfiles[selectedProfileIndex].brake_bias_multiplier}
                      onChange={(e) => updateCurrentProfileField('brakeBiasMultiplier', parseFloat(e.target.value))}
                      className="w-full accent-amber-500"
                    />
                    <p className="text-[10px] text-slate-500">
                      Neutral standard is 1.00x for mathematically pure SDK physics deceleration.
                    </p>
                  </div>

                  {/* Momentum Memory */}
                  <div className="space-y-2">
                    <div className="flex justify-between">
                      <span className="text-slate-300">Momentum Memory Window:</span>
                      <span className="text-cyan-400 font-bold">
                        {config.brakeProfiles[selectedProfileIndex].momentum_memory_ms.toFixed(1)} ms
                      </span>
                    </div>
                    <input
                      type="range"
                      min="10"
                      max="60"
                      step="1"
                      value={config.brakeProfiles[selectedProfileIndex].momentum_memory_ms}
                      onChange={(e) => updateCurrentProfileField('momentumMemoryMs', parseFloat(e.target.value))}
                      className="w-full accent-cyan-500"
                    />
                    <p className="text-[10px] text-slate-500">
                      Time horizon to retain continuous directional momentum during directional transitions.
                    </p>
                  </div>
                </div>
              </div>
            )}
          </div>
        )}

        {/* ══ Tab 3: Bunnyhop Automation ══ */}
        {activeTab === 'bhop' && (
          <div className="bg-[#11141c] border border-[#1e2433] rounded-xl p-6 space-y-6">
            <div className="flex items-center justify-between border-b border-[#1e2433] pb-4">
              <div>
                <h3 className="text-sm font-mono font-bold text-slate-200">BUNNYHOP AUTOMATION SUBSYSTEM</h3>
                <p className="text-xs text-slate-400 font-mono mt-0.5">
                  Sub-tick spacebar scroll simulation and cadence generation
                </p>
              </div>
              <button
                onClick={saveConfig}
                disabled={saving}
                className="px-4 py-2 bg-amber-500 hover:bg-amber-400 text-black font-mono font-bold text-xs rounded-lg transition"
              >
                {saving ? 'SAVING...' : 'SAVE TO MARCO.INI'}
              </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6 font-mono text-xs">
              <div className="space-y-4">
                <div className="flex items-center justify-between p-3 bg-[#090a0f] rounded-lg border border-[#1e2433]">
                  <span className="text-slate-300">Bunnyhop Engine:</span>
                  <button
                    onClick={() => setConfig(prev => prev ? { ...prev, bhopEnabled: !prev.bhopEnabled } : null)}
                    className={`px-3 py-1 rounded font-bold ${
                      config?.bhopEnabled 
                        ? 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/40' 
                        : 'bg-slate-800 text-slate-400'
                    }`}
                  >
                    {config?.bhopEnabled ? 'ENABLED' : 'DISABLED'}
                  </button>
                </div>

                <div className="space-y-2">
                  <label className="text-slate-300">Execution Mode:</label>
                  <div className="grid grid-cols-2 gap-2">
                    {[
                      { id: 1, name: '1: Legit' },
                      { id: 2, name: '2: Aggressive' },
                      { id: 3, name: '3: Humanized' },
                      { id: 4, name: '4: Scroll Emulation' }
                    ].map(m => (
                      <button
                        key={m.id}
                        onClick={() => setConfig(prev => prev ? { ...prev, bhopMode: m.id } : null)}
                        className={`p-2.5 rounded border text-left transition ${
                          config?.bhopMode === m.id
                            ? 'bg-amber-500/20 border-amber-500 text-amber-300 font-bold'
                            : 'bg-[#090a0f] border-[#1e2433] text-slate-400 hover:text-slate-200'
                        }`}
                      >
                        {m.name}
                      </button>
                    ))}
                  </div>
                </div>
              </div>

              <div className="space-y-4">
                <div className="space-y-2">
                  <div className="flex justify-between">
                    <span className="text-slate-300">Airborne Lock Delay:</span>
                    <span className="text-cyan-400 font-bold">{config?.airborneDelayMs || 350} ms</span>
                  </div>
                  <input
                    type="range"
                    min="100"
                    max="600"
                    step="10"
                    value={config?.airborneDelayMs || 350}
                    onChange={(e) => setConfig(prev => prev ? { ...prev, airborneDelayMs: parseInt(e.target.value, 10) } : null)}
                    className="w-full accent-cyan-500"
                  />
                </div>

                <div className="space-y-2">
                  <div className="flex justify-between">
                    <span className="text-slate-300">Scroll Burst Gap:</span>
                    <span className="text-cyan-400 font-bold">{config?.scrollBurstGapMs || 2} ms</span>
                  </div>
                  <input
                    type="range"
                    min="1"
                    max="10"
                    step="1"
                    value={config?.scrollBurstGapMs || 2}
                    onChange={(e) => setConfig(prev => prev ? { ...prev, scrollBurstGapMs: parseInt(e.target.value, 10) } : null)}
                    className="w-full accent-cyan-500"
                  />
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ══ Tab 4: Sub-tick Engine Physics Parameters ══ */}
        {activeTab === 'physics' && (
          <div className="bg-[#11141c] border border-[#1e2433] rounded-xl p-6 space-y-6">
            <div className="flex items-center justify-between border-b border-[#1e2433] pb-4">
              <div>
                <h3 className="text-sm font-mono font-bold text-slate-200">SOURCE SDK / VALVE PHYSICS CONSTANTS</h3>
                <p className="text-xs text-slate-400 font-mono mt-0.5">
                  Governs offline LUT generation and continuous acceleration simulation
                </p>
              </div>
              <button
                onClick={saveConfig}
                disabled={saving}
                className="px-4 py-2 bg-amber-500 hover:bg-amber-400 text-black font-mono font-bold text-xs rounded-lg transition"
              >
                {saving ? 'SAVING...' : 'SAVE TO MARCO.INI'}
              </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-6 font-mono text-xs">
              <div className="space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_friction:</span>
                  <span className="text-amber-400 font-bold">{config?.physFriction.toFixed(2) || '5.20'}</span>
                </div>
                <input
                  type="range"
                  min="2.0"
                  max="10.0"
                  step="0.1"
                  value={config?.physFriction || 5.2}
                  onChange={(e) => setConfig(prev => prev ? { ...prev, physFriction: parseFloat(e.target.value) } : null)}
                  className="w-full accent-amber-500"
                />
              </div>

              <div className="space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_accelerate:</span>
                  <span className="text-amber-400 font-bold">{config?.physAccelerate.toFixed(2) || '5.50'}</span>
                </div>
                <input
                  type="range"
                  min="2.0"
                  max="10.0"
                  step="0.1"
                  value={config?.physAccelerate || 5.5}
                  onChange={(e) => setConfig(prev => prev ? { ...prev, physAccelerate: parseFloat(e.target.value) } : null)}
                  className="w-full accent-amber-500"
                />
              </div>

              <div className="space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_stopspeed:</span>
                  <span className="text-cyan-400 font-bold">{config?.physStopSpeed.toFixed(1) || '80.0'} u/s</span>
                </div>
                <input
                  type="range"
                  min="40.0"
                  max="120.0"
                  step="1.0"
                  value={config?.physStopSpeed || 80.0}
                  onChange={(e) => setConfig(prev => prev ? { ...prev, physStopSpeed: parseFloat(e.target.value) } : null)}
                  className="w-full accent-cyan-500"
                />
              </div>

              <div className="space-y-2">
                <div className="flex justify-between">
                  <span className="text-slate-300">sv_maxspeed:</span>
                  <span className="text-cyan-400 font-bold">{config?.physMaxSpeed.toFixed(1) || '250.0'} u/s</span>
                </div>
                <input
                  type="range"
                  min="200.0"
                  max="300.0"
                  step="1.0"
                  value={config?.physMaxSpeed || 250.0}
                  onChange={(e) => setConfig(prev => prev ? { ...prev, physMaxSpeed: parseFloat(e.target.value) } : null)}
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
