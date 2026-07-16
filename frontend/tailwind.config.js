/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        mcdeploy: {
          bg: '#0c0f0c',
          card: '#141814',
          border: '#242b24',
          green: '#1ebd56',
          darkgreen: '#147a38',
          text: '#e2e8f0',
          muted: '#8e9e8e',
          accent: '#5cff96'
        }
      },
      fontFamily: {
        sans: ['Outfit', 'Inter', 'sans-serif'],
        mono: ['JetBrains Mono', 'Fira Code', 'monospace']
      }
    },
  },
  plugins: [],
}
