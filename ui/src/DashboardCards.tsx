import type { ReactNode } from 'react';

type Tone = 'ok' | 'danger' | 'warning' | 'neutral';
const toneClass: Record<Tone, string> = { ok: 'tone-ok', danger: 'tone-danger', warning: 'tone-warning', neutral: 'tone-neutral' };

export interface KPICardProps {
  title: string;
  value: ReactNode;
  unit?: string;
  detail: ReactNode;
  tone?: Tone;
}

export function KPICard({ title, value, unit, detail, tone }: KPICardProps) {
  return <section className="card min-w-0 px-6 py-4">
    <h2 className="text-sm font-medium">{title}</h2>
    <div className="mt-4 flex flex-wrap items-baseline gap-2">
      <strong className={`text-3xl font-bold tabular-nums ${tone ? toneClass[tone] : ''}`}>{value}</strong>
      {unit && <span className="text-xs text-[var(--muted)]">{unit}</span>}
    </div>
    <div className="mt-4 text-xs text-[var(--muted)]">{detail}</div>
  </section>;
}

export interface ChartCardProps {
  title: string;
  description: string;
  actions?: ReactNode;
  loading: boolean;
  samples: number;
  children: ReactNode;
}

export function ChartCard({ title, description, actions, loading, samples, children }: ChartCardProps) {
  return <section className="card min-w-0 px-6 py-4">
    <div className="flex flex-wrap items-center justify-between gap-4">
      <h2 className="text-sm font-medium">{title}</h2>{actions}
    </div>
    <p className="mt-4 text-xs text-[var(--muted)]">{description}</p>
    <div className="relative mt-4">
      {/* Keep the canvas mounted so its animation lifecycle survives empty/loading states. */}
      <div className={loading || !samples ? 'invisible' : ''}>{children}</div>
      {loading ? <div role="status" aria-label="Loading chart" className="absolute inset-0 rounded-lg bg-[var(--bg)] motion-safe:animate-pulse" /> :
        !samples && <div role="status" className="absolute inset-0 flex items-center justify-center rounded-lg bg-[var(--bg)] px-4 text-center text-sm text-[var(--muted)]">No samples yet. Waiting for telemetry.</div>}
    </div>
  </section>;
}

export interface StatusStripProps {
  items: { label: string; value: ReactNode; tone?: Tone }[];
}

export function StatusStrip({ items }: StatusStripProps) {
  return <section aria-label="Micro diagnostics" className="card flex flex-wrap items-center justify-between gap-x-6 gap-y-4 px-6 py-4 text-xs">
    {items.map(({ label, value, tone }) => <div key={label} className="flex flex-wrap items-baseline gap-2">
      <span className="text-[var(--muted)]">{label}</span>
      <span className={`font-medium tabular-nums ${tone ? toneClass[tone] : ''}`}>{value}</span>
    </div>)}
  </section>;
}
