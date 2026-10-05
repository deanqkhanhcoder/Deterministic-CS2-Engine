export type Theme = 'light' | 'dark';

export function resolveTheme(saved: string | null, prefersDark: boolean): Theme {
  return saved === 'light' || saved === 'dark' ? saved : prefersDark ? 'dark' : 'light';
}

export function initialTheme(): Theme {
  let saved: string | null = null;
  try { saved = localStorage.getItem('marco_theme'); } catch { /* Storage may be disabled. */ }
  return resolveTheme(saved, window.matchMedia('(prefers-color-scheme: dark)').matches);
}

export function applyTheme(theme: Theme) {
  document.documentElement.classList.toggle('dark', theme === 'dark');
}
