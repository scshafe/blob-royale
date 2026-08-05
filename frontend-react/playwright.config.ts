import { defineConfig } from '@playwright/test';
import path from 'node:path';

const productionOrigin = 'http://127.0.0.1:5173';

export default defineConfig({
  expect: {
    timeout: 10_000,
  },
  failOnFlakyTests: true,
  forbidOnly: true,
  fullyParallel: false,
  outputDir: path.resolve(
    import.meta.dirname,
    '../out/test-results/playwright',
  ),
  preserveOutput: 'failures-only',
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
      },
    },
  ],
  reporter: [['list']],
  retries: 0,
  testDir: './e2e',
  testMatch: '**/*.spec.ts',
  timeout: 45_000,
  use: {
    baseURL: productionOrigin,
    headless: true,
    screenshot: 'only-on-failure',
    trace: 'off',
    video: 'off',
  },
  webServer: {
    command: 'npm run preview -- --host 127.0.0.1 --port 5173 --strictPort',
    gracefulShutdown: {
      signal: 'SIGTERM',
      timeout: 5_000,
    },
    reuseExistingServer: false,
    stderr: 'pipe',
    stdout: 'pipe',
    timeout: 10_000,
    url: productionOrigin,
  },
  workers: 1,
});
