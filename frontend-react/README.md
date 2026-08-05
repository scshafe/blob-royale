# Blob Royale web client

This directory owns the browser interface for loading game configuration, controlling the current simulation, and rendering server snapshots. Its public artifact is the production bundle in `dist/`.

The client depends on the native Blob Royale server's HTTP and WebSocket endpoints. The current legacy server address remains `192.168.86.12:8000`; the protocol and configuration boundary are intentionally deferred to the later protocol cutover.

## Toolchain

- Node.js 22.23.1
- npm 10.9.8
- Vite for development and production builds
- Vitest with React Testing Library and jsdom for component tests
- ESLint flat configuration and Prettier for source quality

Install exact locked dependencies with `npm ci`. Use `npm run dev` for the development server, `npm run test:ci` for a non-watch test run, and `npm run build` for a production bundle.

From the repository root, `./scripts/run-linux-toolchain -- ./scripts/verify-web` is the canonical verification path. It intentionally rejects non-Linux and non-x86_64 execution.
