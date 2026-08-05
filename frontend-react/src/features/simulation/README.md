# Simulation viewer domain

This directory owns the browser's complete protocol v1 simulation-viewing capability. Its public exports are `SimulationApi`, `useSimulationConnection`, and `SimulationFeature` through `index.ts`.

`SimulationApi` is the only transport implementation. It fetches immutable configuration without browser credentials, enforces 10-second fetch and socket-connect timeouts, validates canonical Draft 2020-12 schemas and semantic invariants, and owns at most one server-pushed snapshot socket. `useSimulationConnection` is the only connection-state and reconnect policy: every retry uses a fresh API and configuration fetch, clears stale state, follows the finite 1/2/4/8/16/16-second backoff sequence, and resets that budget only after a valid snapshot. The canvas, debug panel, and viewer are functional presentation components and receive only validated readonly values.

The domain depends on React, Ajv, browser Fetch/WebSocket APIs, and generated artifacts sourced from `docs/protocol/schema/v1`. It has no dependency on simulation commands, process lifecycle, Axios, polling, or class-shaped wire models. Generated files are replaced only through `npm run generate:protocol`; `npm run generate:protocol:check` verifies drift without writing.
