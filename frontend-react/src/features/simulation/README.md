# Simulation session domain

This directory owns the browser's complete Blob Royale capability: joining a protocol v2 match session, decoding its frames, rendering the world, and steering one blob. Its public exports are `SimulationApi`, `useSimulationConnection`, `useThrustInput`, `entityRendererRegistry`, and `SimulationFeature` through `index.ts`.

`SimulationApi` is the only transport implementation. It fetches immutable protocol v1 configuration without browser credentials, enforces 10-second fetch and socket-connect timeouts, owns at most one `/api/v2/session` socket, validates the welcome and every snapshot against the accepted v2 schemas before a component sees them, and is the only file in the domain that writes to a socket. `useSimulationConnection` is the only connection-state and reconnect policy: every retry uses a fresh API and configuration fetch, clears stale session state, follows the finite 1/2/4/8/16/16-second backoff sequence, and resets that budget only after a valid snapshot. `useThrustInput` is the only place a keyboard becomes a command. The canvas, HUD, overlay, debug panel, and viewer are presentational and receive only validated readonly values.

Two rules of the wire shape this domain and are worth restating here. The own body is resolved every frame by `controllable.controller_id`, never from `welcome.entity_id`, because elimination and the lobby wipe destroy an entity and the server seats the same controller on a new one. And an open socket with no frames is the ordinary state of a joiner the mode has deferred to the next lobby, not a failure to connect.

`rendering/` holds the `@extension-point entity_renderer` registry: one file per drawn component kind, one registration line each, and a stated reason for every kind that carries no pixels. `sessionSelectors.ts` holds the derivations the wire deliberately does not publish twice — alive count, own entity, own placement, elapsed phase time, the zone-exposure report, and the match overlay description — so components stay presentational and each rule is testable without a renderer.

Phase time is reported as elapsed rather than remaining, and the reason is availability. The `[royale]` phase durations — `countdown_seconds`, `zone_shrink_seconds` — are composition-root configuration: `GET /api/v1/config` publishes only `world`, `simulation`, and `presentation` under an `additionalProperties: false` schema, and no v2 frame carries them. The HUD therefore counts phase time up from `phase_started_tick`, converted by the published `ticks_per_second`, instead of counting a duration it would have to invent down.

Zone exposure was the same until protocol 2.2, and is no longer. `elimination_grace_ticks` now travels in the royale mode-state block, so the HUD counts the real remainder down and the exposure ring thickens against the same denominator `zone_elimination` enforces. The elapsed reading survives as the fallback for a mode that publishes no grace — `sandbox` publishes the `none` block — because the one rule that has not changed is that this client never renders a duration the server did not send.

**Protocol 2.3 is on the wire and this domain does not yet use it.** The generated schemas and types
carry the four lobby command kinds, the `match.seats` roster with `match.start_requested`, and
`welcome.npc_controller_kinds`, and the client validates every one of them because the schemas are
closed and validation is not optional. Nothing renders them: the lobby view, the seat grid, the
right-click NPC menu, and the Start control are a later step, and until they land a royale match is
started by a client that is not this one. Two consequences are worth knowing before that work begins.
`welcome.npc_controller_kinds` is the menu — it is read from the server's controller registry, so a
new bot appears in it with no change here at all. And the overlay copy "The match starts once enough
blobs have joined the arena" is no longer true of the server it describes; a match now starts when
every seat is filled and somebody presses Start, and that sentence is the seat grid's to replace.

The domain depends on React, Ajv, browser Fetch/WebSocket APIs, and generated artifacts sourced from `docs/protocol/schema/v1` and `docs/protocol/schema/v2`. It has no dependency on process lifecycle, Axios, polling, or class-shaped wire models. Generated files are replaced only through `npm run generate:protocol`; `npm run generate:protocol:check` verifies drift without writing.
