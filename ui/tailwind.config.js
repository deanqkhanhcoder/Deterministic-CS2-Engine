/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        background: '#090a0f',
        panel: '#11141c',
        'panel-border': '#1e2433',
        accent: '#f59e0b',
        'accent-glow': 'rgba(245, 158, 11, 0.15)',
        cyan: {
          400: '#22d3ee',
          500: '#06b6d4',
          glow: 'rgba(6, 182, 212, 0.15)',
        },
        emerald: {
          400: '#34d399',
          500: '#10b981',
          glow: 'rgba(16, 185, 129, 0.15)',
        },
        rose: {
          400: '#fb7185',
          500: '#f43f5e',
          glow: 'rgba(244, 63, 94, 0.15)',
        }
      },
      fontFamily: {
        mono: ['JetBrains Mono', 'Fira Code', 'Consolas', 'monospace'],
        sans: ['Inter', 'system-ui', 'sans-serif']
      }
    },
  },
  plugins: [],
}
