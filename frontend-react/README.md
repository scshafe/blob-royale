# Blob Royale web client

This directory owns the browser interface that joins a Blob Royale match: it loads public simulation configuration over protocol v1, holds one protocol v3 session socket, processes the complete published world and renders its known visual components, and turns cursor aim plus held Space into `set_thrust` commands. Processing the complete world does not require showing the whole map at once. Its public artifact is the production bundle in `dist/`.

`SimulationApi` is the canonical browser transport. It derives `/api/v1/config`, `/api/v3/lobbies`, and canonical `/api/v3/lobbies/<lobby_id>/session` endpoints from the page authority, opens the session socket with the `blob-royale.session.v3` subprotocol, validates every inbound frame against the accepted Draft 2020-12 schemas with Ajv, and exposes `sendCommand`, which is a no-op that reports `false` unless the session is open, welcomed, and the kind is one the welcome advertised.

Decoding fails closed, as protocol v3 § "Versioning and fail-closed decoding" requires. The client reads `meta.protocol_version` before interpreting anything else and closes `1003 client_version_unsupported` on a major it does not share or a minor above its own. A component kind, mode-state schema id, or command kind the accepted schema set does not name is logged by name once and closes `1003 client_kind_unsupported`. Nothing unknown is ignored. Intentional offscreen clipping of validated, known geometry is different from silently omitting an unknown kind.

`useSimulationConnection` owns reducer state, StrictMode-safe cleanup, and the finite 1/2/4/8/16/16-second reconnect budget. A reconnect is a new join by contract — new request id, new controller id, new entity id, nothing resumed. It exposes the welcome identity (controller id, first entity id, display name, mode, map, accepted commands), the match section, the published entities, and the own entity resolved from the current frame by controller id. **An open socket carrying no frames is not a stalled connection.** The spawn policy defers a joiner while a match runs and the welcome cannot exist before the session owns a body, so that state is reported as `awaiting_match` and rendered as "waiting for the next match".

Shared movement state publishes the authored defaults, current pair, limits, revision, and effective
tick. `set_movement_tuning` uses that same `sendCommand` path with caller-supplied strictly increasing
request ids; the API reserves pending before socket send and validates exact correlated results
before accepting a snapshot. Its `idle | pending | resolved | unknown` state never infers success
from a changed room revision, local send success, or a reconnect. Room changes clear old exchange
state; interrupted same-room retries preserve unknown and never replay a request. `useMovementTuning`
owns the UI draft, explicit Apply/Reset, and review of peer changes without becoming another sender.
`MovementTuningPanel` shows authoritative values separately from exact request outcomes.

`useThrustInput` is the canonical aim/propulsion owner. Click the arena and hold Space to accelerate
in the cursor's direction, at the room's configured strength regardless of pointer distance.
Releasing Space clears intent and coasts; it does not zero velocity. WASD/arrows no longer steer.
Canvas supplies pointer and projected-body observations through its existing projection/handlers;
the hook alone normalizes aim and emits changed zero/unit commands, at most every 50 ms. Body,
camera, and viewport updates recompute aim under a stationary cursor. An exact-center aim is zero.
Leaving/cancelling, editing, camera interaction, blur, disconnect, or an observed body replacement
requires a fresh go activation; merely holding the key through cancellation cannot resume thrust.
The wire has no incarnation token for a body removed/recreated wholly between delivered snapshots,
so that invisible transition is not claimed as detectable. Native UI Space/Enter remains native.

Rendering goes through `rendering/entityRendererRegistry.ts`, tagged `@extension-point entity_renderer`. It is keyed by component kind: `physics_body` draws a disc or a static obstacle with the own-body highlight, `controllable` draws the display name under its body, `zone` draws the safe zone from its own component, and every remaining kind is registered as non-visual with a stated reason. Adding a component kind is a new `rendering/<kind>Renderer.ts` plus one registration line — `SimulationCanvas` names no kind — and the registry's `satisfies Record<SessionComponentKind, …>` fails the build if a generated kind has no entry.

The Vite development and preview servers bind only `127.0.0.1`. The development `/api` proxy targets `http://127.0.0.1:8000` and supports the same-origin WebSocket upgrade.

## Larger worlds and the client camera

`SimulationCanvas` shows a local window onto the world through one uniform translated
`WorldProjection`, shared by entity and mode-state rendering. Scale is one world unit per CSS
pixel. The responsive 3:2 viewport is capped at 960×640 CSS pixels; resizing changes the visible
world extent. Display density only changes the backing buffer, capped at a pixel ratio of 4.

**Follow player** is the default: the current body stays centred even near map edges, where the
view shows outside-map background and the actual map boundary. Bodyless follow retains its last
centre and reacquires the current body by controller identity. **Manual view** lets the user drag
the map or activate named pan buttons independently, then choose **Follow player** to resume.
Manual centres stay within the world rectangle. Tab/Enter/Space activate ordinary camera buttons;
the same Space press cannot also activate propulsion. Camera code has no gameplay sender. Its
interaction can cancel previously held thrust through the canonical input owner, never start it.

A new room or immutable welcome identity resets the camera without resetting unrelated debug
disclosure. World coordinates, simulation rules, and complete validated snapshots remain unchanged
as the camera moves. HUD and controls remain in screen space.

The canonical requirements and edge cases live in
[`ADR 0004 — World space and the client viewport`](../docs/architecture/0004-gameplay-architecture.md#world-space-and-the-client-viewport--owner-direction-2026-09-09).
The 2026-09-10 camera follow-up records its own larger-than-viewport browser verification;
earlier whole-map-fit tests are not camera evidence. The recorded live hill deployment predates
these controls; deployment and human large-map playtesting remain separate work.

## Generated protocol boundary

The accepted files under `../docs/protocol/schema/v1` and `../docs/protocol/schema/v3` are the only wire truth. `npm run generate:protocol` deterministically generates checked-in deep-readonly TypeScript types and bundled Ajv schema objects for both versions. `npm run generate:protocol:check` writes nothing and fails when generated artifacts drift from those canonical schemas; `npm run validate:protocol-examples` validates every golden example offline. Never hand-edit files under `src/features/simulation/generated`. The generated v3 types are deliberately looser than the schemas where the generator cannot express a conditional shape, so wire documents are narrowed by Ajv validation and never by a cast.

Active session transport uses only `/api/v3/lobbies` and `/api/v3/lobbies/<id>/session` with
`blob-royale.session.v3`; there is no root session alias. Configuration stays on v1. Example
validation explicitly requires v1 `Accepted`, v2 `Historical`, and v3 `Accepted`, retaining every
historical example without generating an active v2 client boundary. Welcome terrain is required,
semantically validated before callbacks, retained between snapshots, and rendered below mode
objectives and entities with the same camera projection.

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
