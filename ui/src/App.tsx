import { useState, useEffect, useRef } from 'react';
import {
  Activity, Shield, Zap, RefreshCw, Power,
  Crosshair, Sliders, Check, Save, Sun, Moon
} from 'lucide-react';
import { RuntimeConfig, TelemetryData, BrakeProfile, EngineState, SocdMode } from './types';
import TelemetryDashboard from './TelemetryDashboard';
import { initialTheme, applyTheme } from './theme';
import type { Theme } from './theme';
import { appendTelemetry } from './telemetry';
import type { TraceSample } from './telemetry';

const DEFAULT_PORT = 47650;
const PROFILE_NAMES = ['Disabled', 'Rifle (AK/M4)', 'Pistol (USP/Glock)', 'Sniper (AWP/Scout)', 'SMG (MP9/Mac10)'];

const DEFAULT_CONFIG: RuntimeConfig = {
  socdMode: SocdMode.FULL,
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
    { overlapDurationUs: 0, brakeBiasMultiplier: 1.0, authorityBiasMs: 0.0, aggressivenessCurve: 1.0, momentumMemoryMs: 25.0, accuracyThreshold: 34.0 }
  ]
};

export default function App() {
  const [theme, setTheme] = useState<Theme>(initialTheme);
  useEffect(() => {
    applyTheme(theme);
  }, [theme]);
  useEffect(() => {
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    const followSystem = () => {
      let saved: string | null = null;
      try { saved = localStorage.getItem('marco_theme'); } catch { /* Keep system fallback. */ }
      if (saved !== 'light' && saved !== 'dark') setTheme(media.matches ? 'dark' : 'light');
    };
    media.addEventListener('change', followSystem);
    return () => media.removeEventListener('change', followSystem);
  }, []);
  const toggleTheme = () => {
    const next = theme === 'dark' ? 'light' : 'dark';
    try { localStorage.setItem('marco_theme', next); } catch { /* Still switch for this session. */ }
    setTheme(next);
  };
  const [token, setToken] = useState<string>(() => {
    const urlParams = new URLSearchParams(window.location.search);
    return urlParams.get('token') || localStorage.getItem('marco_token') || '';
  });
  const [host] = useState<string>(() => {
    return window.location.hostname ? `http://${window.location.hostname}:${DEFAULT_PORT}` : `http://127.0.0.1:${DEFAULT_PORT}`;
  });

  const [connected, setConnected] = useState<boolean>(false);
  const [config, setConfig] = useState<RuntimeConfig>(DEFAULT_CONFIG);
  const [telemetry, setTelemetry] = useState<TelemetryData | null>(null);
  const [selectedProfileIndex, setSelectedProfileIndex] = useState<number>(1);
  const [activeTab, setActiveTab] = useState<'telemetry' | 'profiles' | 'bhop' | 'physics'>('telemetry');
  const [saving, setSaving] = useState<boolean>(false);
  const [statusMsg, setStatusMsg] = useState<string>('');

  const [pendingPower, setPendingPower] = useState<boolean | null>(null);
  const powerPendingRef = useRef(false);
  const configPendingRef = useRef(false);
  const serverConfigRef = useRef<RuntimeConfig>(DEFAULT_CONFIG);
  const configFetchId = useRef(0);
  const [traceHistory, setTraceHistory] = useState<TraceSample[]>([]);

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

  // Keep unsaved form edits when telemetry announces an unrelated state change.
  const acceptConfig = (data: RuntimeConfig, replace = false) => {
    const baseline = serverConfigRef.current;
    setConfig(prev => {
      if (replace || baseline.safeModeEnabled !== data.safeModeEnabled) return data;
      const merged = { ...data };
      for (const key of Object.keys(data) as (keyof RuntimeConfig)[]) {
        if (key === 'brakeProfiles') {
          merged.brakeProfiles = data.brakeProfiles.map((profile, index) => {
            const next = { ...profile };
            for (const field of Object.keys(profile) as (keyof BrakeProfile)[]) {
              if (prev.brakeProfiles[index][field] !== baseline.brakeProfiles[index][field]) {
                next[field] = prev.brakeProfiles[index][field];
              }
            }
            return next;
          });
        } else if (key !== 'activeBrakeProfileIndex' && key !== 'safeModeEnabled' && key !== 'socdMode' &&
            JSON.stringify(prev[key]) !== JSON.stringify(baseline[key])) {
          Object.assign(merged, { [key]: prev[key] });
        }
      }
      return merged;
    });
    serverConfigRef.current = data;
    setSelectedProfileIndex(data.activeBrakeProfileIndex);
    setConnected(true);
  };

  const request = async (path: string, body?: unknown) => {
    const res = await fetch(`${host}${path}`, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Marco-Token': token },
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(5000)
    });
    const data = await res.json();
    if (res.status === 401) {
      localStorage.removeItem('marco_token');
      setToken('');
    }
    if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
    return data;
  };

  const fetchConfig = async (replace = false) => {
    const id = ++configFetchId.current;
    const data: RuntimeConfig = await request('/api/config');
    if (id === configFetchId.current && !configPendingRef.current) acceptConfig(data, replace);
  };

  useEffect(() => {
    if (!token) return;
    let disposed = false;
    fetchConfig().catch(() => { if (!disposed) setConnected(false); });
    const es = new EventSource(`${host}/api/events?token=${encodeURIComponent(token)}`);
    const onTelemetry = (e: MessageEvent) => {
      if (disposed) return;
      try {
        const data: TelemetryData = JSON.parse(e.data);
        setTelemetry(data);
        const receivedAt = performance.now();
        setTraceHistory(previous => appendTelemetry(previous, data, receivedAt));
        setConnected(true);
      } catch {
        setStatusMsg('Invalid telemetry received from daemon');
      }
    };
    es.addEventListener('telemetry', onTelemetry);
    es.addEventListener('state_changed', (e) => {
      onTelemetry(e);
      if (!configPendingRef.current) {
        fetchConfig().catch(() => { if (!disposed) setConnected(false); });
      }
    });
    es.onopen = () => {
      if (!configPendingRef.current) fetchConfig().catch(() => setConnected(false));
    };
    es.onerror = () => { if (!disposed) setConnected(false); };
    return () => {
      disposed = true;
      ++configFetchId.current;
      es.close();
    };
  }, [host, token]);

  // Serialize config mutations; SSE reads cannot overwrite optimistic updates.
  const mutateConfig = async (path: string, body: unknown, message: string,
                              optimistic?: RuntimeConfig, replace = true, committedProfile?: number) => {
    if (configPendingRef.current) return;
    const previous = config;
    configPendingRef.current = true;
    ++configFetchId.current;
    setSaving(true);
    if (optimistic) {
      setConfig(optimistic);
      setSelectedProfileIndex(optimistic.activeBrakeProfileIndex);
    }
    try {
      const data: RuntimeConfig = await request(path, body);
      if (committedProfile !== undefined) {
        setConfig(prev => ({ ...prev, brakeProfiles: prev.brakeProfiles.map((profile, idx) =>
          idx === committedProfile ? data.brakeProfiles[idx] : profile) }));
      }
      acceptConfig(data, replace);
      setStatusMsg(message);
    } catch (err) {
      setConfig(previous);
      setSelectedProfileIndex(previous.activeBrakeProfileIndex);
      setStatusMsg(err instanceof Error ? err.message : 'Failed to update configuration');
    } finally {
      configPendingRef.current = false;
      setSaving(false);
      fetchConfig().catch(() => setConnected(false));
    }
  };

  const toggleSafeMode = () => mutateConfig('/api/safemode',
    { enabled: !config.safeModeEnabled },
    `Safe Mode ${config.safeModeEnabled ? 'Deactivated' : 'Activated (Clamped)'}`,
    { ...config, safeModeEnabled: !config.safeModeEnabled });

  const revertToSnapshot = () => mutateConfig('/api/revert', {},
    'Configuration successfully reverted to pre-safemode snapshot.');

  const selectWeaponProfile = (idx: number) => mutateConfig('/api/config',
    { activeProfileIndex: idx }, `Switched Weapon Profile: ${PROFILE_NAMES[idx]}`,
    { ...config, activeBrakeProfileIndex: idx }, false);

  const selectSocdMode = (mode: SocdMode) => mutateConfig('/api/config',
    { socdMode: mode }, `SOCD mode: ${SocdMode[mode]}`,
    { ...config, socdMode: mode }, false);

  const engineStopped = pendingPower ?? telemetry?.suspended ?? true;
  const engineRunning = !engineStopped && connected && telemetry?.runtimeState !== EngineState.FailSafe;

  const toggleSuspend = async () => {
    if (powerPendingRef.current) return;
    const suspended = !engineStopped;
    powerPendingRef.current = true;
    setPendingPower(suspended);
    try {
      const data: TelemetryData = await request('/api/state', { suspended });
      setTelemetry(data);
      setStatusMsg(data.suspended ? 'Engine STOPPED' : 'Engine RUNNING');
    } catch (err) {
      setStatusMsg(err instanceof Error ? err.message : 'Failed to update engine state');
    } finally {
      powerPendingRef.current = false;
      setPendingPower(null);
    }
  };

  const saveConfig = () => mutateConfig('/api/config', config, 'Configuration saved to marco.ini');

  const resetProfile = () => {
    const next = { ...config, brakeProfiles: config.brakeProfiles.map((profile, idx) =>
      idx === selectedProfileIndex ? { ...DEFAULT_CONFIG.brakeProfiles[idx] } : profile) };
    // Send only this profile so other unsaved edits stay in the form.
    void mutateConfig('/api/config', {
      [`profile_${selectedProfileIndex}`]: next.brakeProfiles[selectedProfileIndex]
    }, `Reset defaults: ${PROFILE_NAMES[selectedProfileIndex]}`, next, false, selectedProfileIndex);
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

  return (
    <div className="min-h-screen bg-[var(--bg)] text-[var(--fg)] flex flex-col font-sans">
      {/* ── Precision Utility Header ── */}
      <header className="border-b border-[var(--border)] bg-[var(--card)] px-6 py-4 flex flex-wrap items-center justify-between gap-4 sticky top-0 z-50">
        <div className="flex items-center gap-4">
          <div className="px-2 py-0.5 rounded-lg bg-[var(--bg)] border border-[var(--border)] text-[var(--fg)] font-bold text-xs">
            Marco
          </div>
          <span className="text-xs font-medium text-[var(--fg)]">
            CS2 motion engine
          </span>
          <span className="text-xs text-[var(--muted)]">v29</span>
        </div>

        {/* Global Action Bar */}
        <div className="flex flex-wrap items-center gap-4">
          <button type="button" onClick={toggleTheme} aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`}
            className="flex items-center gap-2 rounded-lg border border-[var(--border)] px-4 py-2 text-xs font-medium hover:bg-[var(--bg)]">
            {theme === 'dark' ? <Sun className="h-4 w-4" /> : <Moon className="h-4 w-4" />}
            {theme === 'dark' ? 'Light theme' : 'Dark theme'}
          </button>
          {/* CS2 Target Status */}
          <div className="flex items-center gap-1.5 px-4 py-2 rounded-lg bg-[var(--bg)] border border-[var(--border)] text-xs">
            <span className={`w-2 h-2 rounded-full ${telemetry?.targetActive ? 'bg-[var(--ok)]' : 'bg-[var(--muted)]'}`} />
            <span className={telemetry?.targetActive ? 'text-[var(--fg)] font-medium' : 'text-[var(--muted)]'}>
              {telemetry?.targetName || 'CS2 Process'}
            </span>
          </div>

          {/* Engine Power Toggle */}
          <button
            onClick={toggleSuspend}
            aria-pressed={!engineStopped}
            disabled={!connected || pendingPower !== null}
            className={`px-4 py-2 rounded-lg text-xs  font-medium flex items-center gap-1.5 border transition ${
              !engineRunning
                ? 'state-danger'
                : 'state-ok'
            }`}
          >
            <Power className="w-4 h-4" />
            {engineRunning ? 'Running' : 'Stopped'}
          </button>

          {/* Safe Mode Toggle */}
          <button
            onClick={toggleSafeMode}
            aria-pressed={config.safeModeEnabled}
            disabled={!connected || saving}
            className={`px-4 py-2 rounded-lg text-xs  font-medium flex items-center gap-1.5 border transition ${
              config.safeModeEnabled
                ? 'state-warning'
                : 'bg-[var(--bg)] text-[var(--muted)] border-transparent hover:text-[var(--fg)]'
            }`}
          >
            <Shield className="w-3 h-3" />
            Safe mode: {config.safeModeEnabled ? 'On' : 'Off'}
          </button>

          {/* Revert Snapshot Button */}
          <button
            onClick={revertToSnapshot}
            disabled={!connected || saving}
            title="Restore un-clamped snapshot configuration"
            className="px-4 py-2 rounded-lg text-xs font-medium flex items-center gap-1 bg-[var(--bg)] hover:bg-[var(--bg)] text-[var(--fg)] border border-[var(--border)] transition"
          >
            <RefreshCw className="w-3 h-3 text-[var(--fg)]" />
            Revert snapshot
          </button>

          {/* Connection Pill */}
          <div className="flex items-center gap-1.5 pl-2 border-l border-[var(--border)] text-xs text-[var(--muted)]">
            <span className={`w-2 h-2 rounded-full ${connected ? 'bg-[var(--ok)]' : 'bg-[var(--danger)]'}`} />
            <span>127.0.0.1:{DEFAULT_PORT}</span>
          </div>
        </div>
      </header>

      {/* ── Status Toast ── */}
      {statusMsg && (
        <div className="fixed bottom-4 right-4 z-50 max-w-[calc(100vw-2rem)] rounded-lg bg-[var(--card)] border border-[var(--border)] px-4 py-3 text-xs text-[var(--fg)] flex items-center gap-4 shadow-lg">
          <span role="status">{statusMsg}</span>
          <button aria-label="Dismiss notification" onClick={() => setStatusMsg('')} className="text-[var(--muted)] hover:text-[var(--fg)]">✕</button>
        </div>
      )}

      {/* ── Tab Bar ── */}
      <div className="border-b border-[var(--border)] bg-[var(--card)] px-6 flex gap-4 overflow-x-auto">
        {[
          { id: 'telemetry', label: 'Telemetry HUD', icon: Activity },
          { id: 'profiles', label: 'Weapon profiles', icon: Crosshair },
          { id: 'bhop', label: 'Bunnyhop', icon: Zap },
          { id: 'physics', label: 'Physics parameters', icon: Sliders }
        ].map(tab => {
          const Icon = tab.icon;
          const active = activeTab === tab.id;
          return (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id as typeof activeTab)}
              aria-current={active ? 'page' : undefined}
              className={`py-4 whitespace-nowrap text-xs  font-medium flex items-center gap-2 border-b-2 transition ${
                active
                  ? 'border-[var(--accent)] text-[var(--fg)]'
                  : 'border-transparent text-[var(--muted)] hover:text-[var(--fg)]'
              }`}
            >
              <Icon className="w-3.5 h-3.5" />
              {tab.label}
            </button>
          );
        })}
      </div>

      {/* ── Main Utility Container ── */}
      <main className="px-6 py-4 flex-1 max-w-7xl mx-auto w-full space-y-6">

        {/* ════ TAB 1: TELEMETRY HUD ════ */}
        {activeTab === 'telemetry' && (
          <TelemetryDashboard telemetry={telemetry} history={traceHistory} connected={connected}
            threshold={telemetry?.profile?.accuracyThreshold ?? config.brakeProfiles[config.activeBrakeProfileIndex].accuracyThreshold} />
        )}

        {/* ════ TAB 2: WEAPON PROFILES & TUNING ════ */}
        {activeTab === 'profiles' && (
          <div className="space-y-4">
            {/* Weapon Selector Cards */}
            <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
              {[
                { idx: 1, name: 'Rifle', desc: 'AK-47 / M4A4 / M4A1-S', threshold: 34.0, dur: '109 ms' },
                { idx: 2, name: 'Pistol', desc: 'USP-S / Glock / Deagle', threshold: 34.0, dur: '109 ms' },
                { idx: 3, name: 'Sniper', desc: 'AWP / SSG 08', threshold: 17.0, dur: '125 ms' },
                { idx: 4, name: 'SMG', desc: 'MP9 / MAC-10 / MP7', threshold: 34.0, dur: '109 ms' }
              ].map(w => {
                const isActive = config.activeBrakeProfileIndex === w.idx;
                const isSelected = selectedProfileIndex === w.idx;
                return (
                  <button
                    type="button"
                    aria-pressed={isActive}
                    disabled={!connected || saving}
                    key={w.idx}
                    onClick={() => selectWeaponProfile(w.idx)}
                    className={`p-4 rounded-lg border shadow-sm cursor-pointer transition ${
                      isActive
                        ? 'state-ok'
                        : isSelected
                          ? 'bg-[var(--bg)] border-[var(--accent)] text-[var(--fg)]'
                          : 'bg-[var(--card)] border-[var(--border)] text-[var(--muted)] hover:border-[var(--border)]'
                    }`}
                  >
                    <div className="flex items-center justify-between">
                      <span className="font-medium text-sm">{w.name}</span>
                      {isActive && (
                        <span className="text-xs tone-ok font-bold flex items-center gap-1">
                          <Check className="w-3 h-3" /> Active
                        </span>
                      )}
                    </div>
                    <p className="text-xs text-[var(--muted)] mt-1">{w.desc}</p>
                    <div className="flex flex-wrap gap-2 justify-between items-center mt-4 text-xs text-[var(--muted)]">
                      <span>Threshold: <strong className="text-[var(--fg)]">{config.brakeProfiles[w.idx].accuracyThreshold} u/s</strong></span>
                      <span>Brake: <strong className="text-[var(--fg)]">{w.dur}</strong></span>
                    </div>
                  </button>
                );
              })}
            </div>

            {/* Profile Detail Tuner */}
            <div className="bg-[var(--card)] border border-[var(--border)] rounded-lg shadow-sm px-6 py-4 space-y-6">
              <div className="flex flex-wrap items-center justify-between gap-4 border-b border-[var(--border)] pb-4">
                <div>
                  <div className="text-sm font-medium text-[var(--fg)] flex flex-wrap items-center gap-2">
                    <span>Editing profile: {PROFILE_NAMES[selectedProfileIndex]}</span>
                    <span className="text-xs px-2 py-0.5 rounded-lg bg-[var(--bg)] text-[var(--fg)]">
                      Accuracy threshold: &le; {activeWeapon.accuracyThreshold.toFixed(1)} u/s
                    </span>
                  </div>
                  <p className="text-xs text-[var(--muted)] mt-0.5">
                    Sub-tick deceleration simulation parameters aligned with Valve PM_Friction / Accelerate
                  </p>
                </div>

                <div className="flex items-center gap-2">
                <button
                  onClick={resetProfile}
                  disabled={!connected || saving}
                  className="px-4 py-2 border border-[var(--accent)] hover:bg-[var(--bg)] text-[var(--fg)] font-medium text-xs rounded-lg transition disabled:opacity-50"
                >
                  Reset default
                </button>
                <button
                  onClick={saveConfig}
                  disabled={!connected || saving}
                  className="px-4 py-2 bg-[var(--accent)] hover:opacity-90 text-[var(--accent-fg)] font-medium text-xs rounded-lg transition flex items-center gap-1.5"
                >
                  <Save className="w-3.5 h-3.5" />
                  {saving ? 'Saving…' : 'Apply & save'}
                </button>
                </div>
              </div>

              {/* Sliders Grid */}
              <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs">
                {/* Accuracy Threshold */}
                <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-[var(--fg)] font-medium">Accuracy threshold:</span>
                    <span className="text-[var(--fg)] font-bold tabular-nums">{activeWeapon.accuracyThreshold.toFixed(1)} u/s</span>
                  </div>
                  <input
                    type="range"
                    disabled={saving}
                    min="10"
                    max="60"
                    step="1"
                    aria-label="Accuracy threshold" value={activeWeapon.accuracyThreshold}
                    onChange={(e) => updateProfileField('accuracyThreshold', parseFloat(e.target.value))}
                    className="w-full accent-[var(--accent)]"
                  />
                  <div className="text-xs text-[var(--muted)] flex justify-between">
                    <span>17.0 u/s (Sniper standard)</span>
                    <span>34.0 u/s (Rifle/Pistol standard)</span>
                  </div>
                </div>

                {/* Key Overlap Window */}
                <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-[var(--fg)] font-medium">Key overlap window:</span>
                    <span className="text-[var(--fg)] font-bold tabular-nums">{activeWeapon.overlapDurationUs} µs</span>
                  </div>
                  <input
                    type="range"
                    disabled={saving}
                    min="0"
                    max="8000"
                    step="500"
                    aria-label="Key overlap window" value={activeWeapon.overlapDurationUs}
                    onChange={(e) => updateProfileField('overlapDurationUs', parseInt(e.target.value, 10))}
                    className="w-full accent-[var(--accent)]"
                  />
                  <div className="text-xs text-[var(--muted)]">
                    Auto Counter-Strafe uses this bounded overlap independently of SOCD mode.
                  </div>
                </div>

                {/* Brake Bias Multiplier */}
                <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-[var(--fg)] font-medium">Brake bias multiplier:</span>
                    <span className="text-[var(--fg)] font-bold tabular-nums">{activeWeapon.brakeBiasMultiplier.toFixed(2)}x</span>
                  </div>
                  <input
                    type="range"
                    disabled={saving}
                    min="0.80"
                    max="1.30"
                    step="0.01"
                    aria-label="Brake bias multiplier" value={activeWeapon.brakeBiasMultiplier}
                    onChange={(e) => updateProfileField('brakeBiasMultiplier', parseFloat(e.target.value))}
                    className="w-full accent-[var(--accent)]"
                  />
                  <div className="text-xs text-[var(--muted)]">
                    Standard is 1.00x (pure mathematical physics deceleration without arbitrary scaling).
                  </div>
                </div>

                {/* Momentum Memory */}
                <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                  <div className="flex justify-between items-center">
                    <span className="text-[var(--fg)] font-medium">Momentum memory:</span>
                    <span className="text-[var(--fg)] font-bold tabular-nums">{activeWeapon.momentumMemoryMs.toFixed(1)} ms</span>
                  </div>
                  <input
                    type="range"
                    disabled={saving}
                    min="10"
                    max="60"
                    step="1"
                    aria-label="Momentum memory" value={activeWeapon.momentumMemoryMs}
                    onChange={(e) => updateProfileField('momentumMemoryMs', parseFloat(e.target.value))}
                    className="w-full accent-[var(--accent)]"
                  />
                  <div className="text-xs text-[var(--muted)]">
                    Memory horizon across rapid A-D alternating strafes (Rifle: 35ms, Sniper: 40ms).
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ════ TAB 3: BUNNYHOP AUTOMATION ════ */}
        {activeTab === 'bhop' && (
          <div className="bg-[var(--card)] border border-[var(--border)] rounded-lg shadow-sm px-6 py-4 space-y-4">
            <div className="flex flex-wrap items-center justify-between gap-4 border-b border-[var(--border)] pb-4">
              <div>
                <span className="text-sm font-medium text-[var(--fg)]">Bunnyhop automation</span>
                <p className="text-xs text-[var(--muted)] mt-0.5">
                  Sub-tick scroll wheel pulse generation and cadence alignment
                </p>
              </div>
              <button
                onClick={saveConfig}
                disabled={!connected || saving}
                className="px-4 py-2 bg-[var(--accent)] hover:opacity-90 text-[var(--accent-fg)] font-medium text-xs rounded-lg transition flex items-center gap-1.5"
              >
                <Save className="w-3.5 h-3.5" />
                {saving ? 'Saving…' : 'Save to marco.ini'}
              </button>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs">
              <div className="space-y-4">
                <div className="flex items-center justify-between p-4 bg-[var(--bg)] rounded-lg border border-[var(--border)]">
                  <span className="text-[var(--fg)]">Bhop subsystem:</span>
                  <button
                    onClick={() => setConfig(prev => ({ ...prev, bhopEnabled: !prev.bhopEnabled }))}
                    className={`px-3 py-1 rounded-lg font-bold ${
                      config.bhopEnabled
                        ? 'state-ok border'
                        : 'bg-[var(--bg)] text-[var(--muted)]'
                    }`}
                  >
                    {config.bhopEnabled ? 'Enabled' : 'Disabled'}
                  </button>
                </div>

                <div className="space-y-1.5">
                  <span className="text-[var(--muted)] text-xs">Execution cadence mode:</span>
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
                        className={`p-4 rounded-lg border text-left transition ${
                          config.bhopMode === m.id
                            ? 'bg-[var(--bg)] border-[var(--accent)] text-[var(--fg)] font-medium'
                            : 'bg-[var(--bg)] border-[var(--border)] text-[var(--muted)] hover:text-[var(--fg)]'
                        }`}
                      >
                        {m.name}
                      </button>
                    ))}
                  </div>
                </div>
              </div>

              <div className="space-y-4">
                <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                  <div className="flex justify-between">
                    <span className="text-[var(--fg)]">Airborne lock delay:</span>
                    <span className="text-[var(--fg)] font-bold tabular-nums">{config.airborneDelayMs} ms</span>
                  </div>
                  <input
                    type="range"
                    disabled={saving}
                    min="100"
                    max="600"
                    step="10"
                    aria-label="Airborne lock delay" value={config.airborneDelayMs}
                    onChange={(e) => setConfig(prev => ({ ...prev, airborneDelayMs: parseInt(e.target.value, 10) }))}
                    className="w-full accent-[var(--accent)]"
                  />
                </div>

                <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                  <div className="flex justify-between">
                    <span className="text-[var(--fg)]">Scroll burst gap:</span>
                    <span className="text-[var(--fg)] font-bold tabular-nums">{config.scrollBurstGapMs} ms</span>
                  </div>
                  <input
                    type="range"
                    disabled={saving}
                    min="1"
                    max="10"
                    step="1"
                    aria-label="Scroll burst gap" value={config.scrollBurstGapMs}
                    onChange={(e) => setConfig(prev => ({ ...prev, scrollBurstGapMs: parseInt(e.target.value, 10) }))}
                    className="w-full accent-[var(--accent)]"
                  />
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ════ TAB 4: PHYSICS CONSTANTS ════ */}
        {activeTab === 'physics' && (
          <div className="bg-[var(--card)] border border-[var(--border)] rounded-lg shadow-sm px-6 py-4 space-y-4">
            <div className="flex flex-wrap items-center justify-between gap-4 border-b border-[var(--border)] pb-4">
              <div>
                <span className="text-sm font-medium text-[var(--fg)]">Source SDK physics pipeline</span>
                <p className="text-xs text-[var(--muted)] mt-0.5">
                  Internal simulation parameters matching Counter-Strike 2 engine settings
                </p>
              </div>
              <button
                onClick={saveConfig}
                disabled={!connected || saving}
                className="px-4 py-2 bg-[var(--accent)] hover:opacity-90 text-[var(--accent-fg)] font-medium text-xs rounded-lg transition flex items-center gap-1.5"
              >
                <Save className="w-3.5 h-3.5" />
                {saving ? 'Saving…' : 'Save to marco.ini'}
              </button>
            </div>

            <section aria-label="SOCD Control" className="rounded-lg border border-[var(--border)] bg-[var(--bg)] p-4">
              <h2 className="text-xs font-medium text-[var(--fg)]">SOCD control</h2>
              <div className="mt-4 grid grid-cols-1 sm:grid-cols-3 gap-4">
                {[
                  { mode: SocdMode.FULL, label: 'FULL', detail: 'High Performance', color: 'state-ok' },
                  { mode: SocdMode.HUMANIZED, label: 'HUMANIZED', detail: 'Fixed 8 ms overlap', color: 'state-warning' },
                  { mode: SocdMode.OFF, label: 'OFF', detail: 'Native Passthrough', color: 'border-[var(--accent)] bg-[var(--bg)] text-[var(--fg)]' }
                ].map(option => <button key={option.mode} type="button" aria-pressed={config.socdMode === option.mode}
                  disabled={!connected || saving} onClick={() => selectSocdMode(option.mode)}
                  className={`rounded-lg border p-4 text-left transition disabled:opacity-50 ${config.socdMode === option.mode ? option.color : 'border-[var(--border)] text-[var(--muted)] hover:border-[var(--accent)]'}`}>
                  <span className="flex items-center justify-between text-sm font-bold tabular-nums">{option.label}{config.socdMode === option.mode && <Check className="h-4 w-4" />}</span>
                  <span className="mt-1 block text-xs">{option.detail}</span>
                </button>)}
              </div>
              <p className="mt-4 text-xs leading-5 text-[var(--muted)]">FULL: latest key wins immediately. HUMANIZED: fixed 8 ms overlap, then latest key wins. OFF: native opposing-key input. Auto Counter-Strafe remains active on release in every mode.</p>
              <p className="mt-1 text-xs text-[var(--muted)]">Changes apply and save immediately. Release W/A/S/D before switching. Bhop is controlled separately.</p>
            </section>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs">
              <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                <div className="flex justify-between">
                  <span className="text-[var(--fg)]">sv_friction (Friction Coefficient):</span>
                  <span className="text-[var(--fg)] font-bold tabular-nums">{config.physFriction.toFixed(2)}</span>
                </div>
                <input
                  type="range"
                    disabled={saving}
                  min="2.0"
                  max="10.0"
                  step="0.1"
                  aria-label="sv_friction" value={config.physFriction}
                  onChange={(e) => setConfig(prev => ({ ...prev, physFriction: parseFloat(e.target.value) }))}
                  className="w-full accent-[var(--accent)]"
                />
              </div>

              <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                <div className="flex justify-between">
                  <span className="text-[var(--fg)]">sv_accelerate (Opposing Acceleration):</span>
                  <span className="text-[var(--fg)] font-bold tabular-nums">{config.physAccelerate.toFixed(2)}</span>
                </div>
                <input
                  type="range"
                    disabled={saving}
                  min="2.0"
                  max="10.0"
                  step="0.1"
                  aria-label="sv_accelerate" value={config.physAccelerate}
                  onChange={(e) => setConfig(prev => ({ ...prev, physAccelerate: parseFloat(e.target.value) }))}
                  className="w-full accent-[var(--accent)]"
                />
              </div>

              <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                <div className="flex justify-between">
                  <span className="text-[var(--fg)]">sv_stopspeed:</span>
                  <span className="text-[var(--fg)] font-bold tabular-nums">{config.physStopSpeed.toFixed(1)} u/s</span>
                </div>
                <input
                  type="range"
                    disabled={saving}
                  min="40.0"
                  max="120.0"
                  step="1.0"
                  aria-label="sv_stopspeed" value={config.physStopSpeed}
                  onChange={(e) => setConfig(prev => ({ ...prev, physStopSpeed: parseFloat(e.target.value) }))}
                  className="w-full accent-[var(--accent)]"
                />
              </div>

              <div className="bg-[var(--bg)] border border-[var(--border)] p-4 rounded-lg space-y-2">
                <div className="flex justify-between">
                  <span className="text-[var(--fg)]">sv_maxspeed:</span>
                  <span className="text-[var(--fg)] font-bold tabular-nums">{config.physMaxSpeed.toFixed(1)} u/s</span>
                </div>
                <input
                  type="range"
                    disabled={saving}
                  min="200.0"
                  max="300.0"
                  step="1.0"
                  aria-label="sv_maxspeed" value={config.physMaxSpeed}
                  onChange={(e) => setConfig(prev => ({ ...prev, physMaxSpeed: parseFloat(e.target.value) }))}
                  className="w-full accent-[var(--accent)]"
                />
              </div>
            </div>
          </div>
        )}
      </main>
    </div>
  );
}
