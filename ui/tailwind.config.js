/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  darkMode: 'class',
  theme: {
    extend: {
      fontFamily: { mono: ['Consolas', 'monospace'], sans: ['Inter', 'system-ui', 'sans-serif'] },
    },
  },
  plugins: [],
};
