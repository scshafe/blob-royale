import react from '@vitejs/plugin-react';
import { configDefaults, defineConfig } from 'vitest/config';

const sameOriginApiProxy = {
  '/api': {
    changeOrigin: true,
    target: 'http://127.0.0.1:8000',
    ws: true,
  },
};

export default defineConfig({
  plugins: [react()],
  server: {
    host: '127.0.0.1',
    port: 5173,
    proxy: sameOriginApiProxy,
    strictPort: true,
  },
  preview: {
    host: '127.0.0.1',
    port: 5173,
    proxy: sameOriginApiProxy,
    strictPort: true,
  },
  test: {
    clearMocks: true,
    environment: 'jsdom',
    exclude: [...configDefaults.exclude, 'e2e/**'],
    fileParallelism: false,
    isolate: true,
    maxWorkers: 1,
    pool: 'threads',
    setupFiles: './src/setupTests.ts',
  },
});
