/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    "./index.html",
    "./renderer/**/*.{js,ts,jsx,tsx}",
    "./modules/**/*.{js,ts,jsx,tsx}",
    "./core/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // ─── Semantic surface tokens ───────────────────────────
        // Layered backgrounds — base < panel < elevated < hover
        surface: {
          base:     '#0d1117',  // app background (GitHub dark)
          panel:    '#161b22',  // dock / panel background
          elevated: '#1c2128',  // dropdowns, popovers, toolbar
          hover:    '#21262d',  // hover state
          active:   '#2d333b',  // pressed / active state
        },
        // ─── Borders ───────────────────────────────────────────
        edge: {
          DEFAULT:  '#30363d',  // standard border
          muted:    '#21262d',  // subtle border
          strong:   '#484f58',  // emphasized border
        },
        // ─── Text ──────────────────────────────────────────────
        fg: {
          primary:   '#e6edf3',  // high-contrast text
          secondary: '#7d8590',  // labels, metadata
          muted:     '#484f58',  // disabled, hints
        },
        // ─── Accents ───────────────────────────────────────────
        accent: {
          DEFAULT:   '#06b6d4',  // cyan — primary action, selection
          hover:     '#22d3ee',  // cyan light — hover
          muted:     '#0e7490',  // cyan dark — muted accent
        },
        ok:    '#3fb950',  // green — success, valid
        warn:  '#d29922',  // yellow — warning
        err:   '#f85149',  // red — error, delete
        info:  '#58a6ff',  // blue — info, link
        // ─── Legacy compat (do not remove) ─────────────────────
        geo: {
          dark:   '#0d1117',
          panel:  '#161b22',
          accent: '#06b6d4',
        },
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
        mono: ['JetBrains Mono', 'Fira Code', 'monospace'],
      },
      fontSize: {
        '2xs': ['11px', { lineHeight: '16px' }],
        '3xs': ['10px', { lineHeight: '14px' }],
      },
      borderRadius: {
        '4.5': '4.5px',
      },
      boxShadow: {
        'panel':  '0 1px 3px rgba(0,0,0,0.3), 0 1px 2px rgba(0,0,0,0.2)',
        'overlay':'0 8px 24px rgba(0,0,0,0.5), 0 2px 6px rgba(0,0,0,0.3)',
        'rail':   '0 0 0 1px rgba(6,182,212,0.4), 0 0 8px rgba(6,182,212,0.2)',
      },
      transitionTimingFunction: {
        'smooth': 'cubic-bezier(0.4, 0, 0.2, 1)',
      },
    },
  },
  plugins: [],
};
