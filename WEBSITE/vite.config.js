import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const daemon = env.VITE_MCDEPLOY_DEV_DAEMON || 'http://localhost:8082'

  return {
    plugins: [react()],
    build: {
      outDir: 'dist',
      emptyOutDir: true
    },
    server: {
      proxy: {
        '/api': { target: daemon, changeOrigin: true, ws: true }
      }
    }
  }
})