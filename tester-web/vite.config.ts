import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
        rewrite: (p) => p.replace(/^\/api/, ''),
      },
      '/ws': {
        target: 'ws://localhost:8081',
        ws: true,
        rewrite: (p) => p.replace(/^\/ws/, ''),
      },
      '/tempo': {
        target: 'http://localhost:3200',
        changeOrigin: true,
        rewrite: (p) => p.replace(/^\/tempo/, ''),
      },
      '/prometheus': {
        target: 'http://localhost:9090',
        changeOrigin: true,
        rewrite: (p) => p.replace(/^\/prometheus/, ''),
      },
    },
  },
});
