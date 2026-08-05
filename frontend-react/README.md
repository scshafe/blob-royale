# Blob Royale web client

This directory owns the read-only browser interface for loading public simulation configuration and rendering complete server-pushed snapshots. Its public artifact is the production bundle in `dist/`. Protocol v1 intentionally exposes no command, process-lifecycle, polling, or client-send surface.

`SimulationApi` is the canonical browser transport. It derives exact `/api/v1/config` and `/api/v1/snapshots` endpoints from the page authority, validates Draft 2020-12 schemas and cross-message invariants, bounds configuration and WebSocket connection attempts to 10 seconds, owns at most one WebSocket, and rejects oversized or malformed data before rendering. `useSimulationConnection` owns reducer state, StrictMode-safe cleanup, and the finite 1/2/4/8/16/16-second reconnect budget. Every retry creates a fresh transport, refetches configuration, and resets the retry budget only after the first valid snapshot. Canvas and debug components receive immutable validated values and remain presentational.

The Vite development and preview servers bind only `127.0.0.1`. The development `/api` proxy targets `http://127.0.0.1:8000` and supports the same-origin WebSocket upgrade.

## Generated protocol boundary

The accepted files under `../docs/protocol/schema/v1` are the only wire truth. `npm run generate:protocol` deterministically generates checked-in deep-readonly TypeScript types and bundled Ajv schema objects. `npm run generate:protocol:check` writes nothing and fails when generated artifacts drift from those canonical schemas. Never hand-edit files under `src/features/simulation/generated`.

## Toolchain

- Node.js 22.23.1
- npm 10.9.8
- Vite for development and production builds
- strict TypeScript
- Ajv Draft 2020-12 validation with standard format support
- Vitest with React Testing Library and jsdom for component tests
- ESLint flat configuration and Prettier for source quality

Install exact locked dependencies with `npm ci`. Use `npm run dev` for the loopback development server, `npm run typecheck` for strict checking, `npm run test:ci` for a non-watch test run, and `npm run build` for a production bundle.

From the repository root, `./scripts/run-linux-toolchain -- ./scripts/verify-web` is the canonical verification path. It intentionally rejects non-Linux and non-x86_64 execution.
