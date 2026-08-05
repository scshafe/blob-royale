import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  plugins: [react()],
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true,
    proxy: {
      '/api': {
        changeOrigin: true,
        target: 'http://127.0.0.1:8000',
        ws: true,
      },
    },
  },
  preview: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true,
  },
  test: {
    clearMocks: true,
    environment: 'jsdom',
    fileParallelism: false,
    isolate: true,
    maxWorkers: 1,
    pool: 'threads',
    setupFiles: './src/setupTests.ts',
  },
});
