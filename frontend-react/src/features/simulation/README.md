# Simulation session domain

This directory owns the browser's complete Blob Royale capability: choosing a room from the protocol v3 lobby directory, joining that room's match session, decoding its frames, rendering the world, tuning shared room movement, and steering one blob. Its public exports are `SimulationApi`, `useLobbyDirectory`, `useMovementTuning`, `useRoomNavigation`, `useSimulationConnection`, `useThrustInput`, `entityRendererRegistry`, and `SimulationFeature` through `index.ts`.

**Rooms, since protocol 2.4.** The page is either on the directory or in one room, and the URL says which: `/` is the directory and `/?lobby=<id>` is a room, a query rather than a path because the bundle is served as a static directory behind `tailscale serve` with no single-page fallback. `useRoomNavigation` is the one place that decision is made and the URL written; a `popstate` reads the URL again, so the browser's back button is a leave. `SimulationShell` is the persistent frame: the app's name, a breadcrumb naming the level (`Rooms`, or `Rooms › Room 2`), and Leave. `SimulationFeature` mounts the three long-lived things above the view switch -- navigation, the connection into the current room, and the directory read while in none -- so choosing a room swaps the content and never re-creates a socket or a poll. Leaving is one transition: the room becomes `null`, the connection hook disposes its socket, and the directory hook, now enabled, reads the directory once immediately and then once a second, and only while the page is visible.

**What a browser cannot see.** The server refuses a join it will not admit with `404 LOBBY.NOT_FOUND`, `409 LOBBY.FULL`, or `503 LOBBY.UNAVAILABLE`, but the WebSocket API never exposes the HTTP response a declined upgrade was refused with: from here every one of them is a socket that closed before it opened. `useSimulationConnection` therefore reads the directory once after such a close and derives the refusal from the listing -- full, not serving, or not listed -- and sends the player back to the directory with that sentence as the notice, never retrying; a listing that predicts no refusal, or a directory that cannot be read, means the close was transport and keeps the bounded backoff. The one refusal that arrives as the server's own words is `1013 lobby_full`, the last-seat race lost after admission, which is a close reason and is read as one. `lobbyDirectorySelectors.ts` holds the rule and every sentence, so the directory's disabled Join buttons and the connection's refusals cannot disagree. The directory also predicts the one refusal admission cannot: a room whose every seat is filled past `countdown`, or filled with no bot to displace, has no seat a join could take, and its Join is disabled with that sentence rather than letting a hopeful session be closed `lobby_full`.

`SimulationApi` is the only transport implementation. It fetches immutable protocol v1 configuration without browser credentials, reads the lobby directory without caching it, enforces 10-second fetch and socket-connect timeouts, owns at most one room session socket (`/api/v3/lobbies/<id>/session`), validates the welcome, every snapshot, the directory, and every v3 failure envelope against the accepted v3 schemas before a component sees them, and is the only file in the domain that writes to a socket. `useSimulationConnection` is the only connection-state and reconnect policy: every retry uses a fresh API and configuration fetch, clears stale session state, follows the finite 1/2/4/8/16/16-second backoff sequence, and resets that budget only after a valid snapshot. `useThrustInput` is the only place a keyboard becomes a command. The canvas, HUD, overlay, debug panel, and viewer are presentational and receive only validated readonly values.

Two rules of the wire shape this domain and are worth restating here. The own body is resolved every frame by `controllable.controller_id`, never from `welcome.entity_id`, because elimination and the lobby wipe destroy an entity and the server seats the same controller on a new one. And an open socket with no frames is the ordinary state of a joiner the mode has deferred to the next lobby, not a failure to connect.

`rendering/` holds the `@extension-point entity_renderer` registry: one file per drawn component kind, one registration line each, and a stated reason for every kind that carries no pixels. `sessionSelectors.ts` holds the derivations the wire deliberately does not publish twice — alive count, own entity, own placement, elapsed phase time, the zone-exposure report, and the match overlay description — so components stay presentational and each rule is testable without a renderer.

Session v3 requires complete immutable authored `terrain` in welcome, never in tick frames.
`terrainValidation.ts` adds aggregate limits, unique names per family, closed-envelope containment,
representable nonzero segments, and the shared negative-zero policy after the generated schema.
`SimulationApi` checks exact agreement with fetched v1 world bounds before publishing the welcome.
There is no inferred, clipped, repaired, or default terrain. Reconnection clears the old welcome;
snapshots retain the current welcome's terrain identity.

`rendering/terrainRenderer.ts` owns the bottom world layer: envelope intersection with solid ground
or the union of opaque round-capped/round-joined corridor strokes, minus the union of holes. An
envelope clip followed by one complement clip per hole subtracts overlapping holes without XOR
refill, and all clips precede every road stroke. The race renderer draws gates/objectives only;
its temporary track/width wire mirror contributes no pixels. Shared geometry goldens drive the
client schema/semantic and render-trace cases; C++ owns exact support-probe classification. Canvas
rasterization is a pixel approximation, not another support solver or a physics tolerance.

**World space is not the viewport.** `useSimulationCamera` selects a session-local centre and
`rendering/worldProjection.ts` supplies the single uniform translated projection for all entity
and mode-state layers, including the race course. The default is **Follow player**, resolving the
current body by controller identity on each snapshot, retaining the last centre while bodyless,
and resuming on a replacement body. Before the first body the view uses map centre. A new immutable
welcome token or requested room resets the camera, even if numeric IDs repeat; it does not reset
the viewer's debug disclosure.

**Manual view** freezes the current centre. Drag the map or use the named pan buttons to move the
view independently; **Follow player** returns to tracking. Manual centres stay within world
bounds, while strict follow never clamps and may show outside-map background. Buttons use
Tab/Enter/Space, without also activating propulsion. WASD/arrows no longer steer. No camera API
receives a gameplay sender; the canonical input owner may release held thrust on interaction.
Pointer capture ends on release, cancellation, loss of capture, blur, or switching to follow.

`useCanvasViewport` measures available CSS width, capped at 960 pixels with a 3:2 aspect and integer
layout rounding. Scale is one world unit per CSS pixel, so resizing changes visible extent rather
than world geometry or body readability. DPR only affects the backing buffer, capped at 4; hidden
zero-area views do not draw. The true projected map boundary, not the viewport border, is drawn
against a distinct outside-map background. HUD, overlays, and controls stay in screen space.
The larger-world camera requirement is defined once in
[`ADR 0004 — World space and the client viewport`](../../../../docs/architecture/0004-gameplay-architecture.md#world-space-and-the-client-viewport--owner-direction-2026-09-09).
Complete validated snapshots remain intact: offscreen geometry is clipped only by the canvas,
and unknown kinds still fail closed before rendering. The camera follow-up's own tests establish
its automated scope; earlier whole-map-fit tests and compact live playtests do not.

Phase time is reported as elapsed rather than remaining, and the reason is availability. The `[royale]` phase durations — `countdown_seconds`, `zone_shrink_seconds` — are composition-root configuration: `GET /api/v1/config` publishes only `world`, `simulation`, and `presentation` under an `additionalProperties: false` schema, and no v3 frame carries them. The HUD therefore counts phase time up from `phase_started_tick`, converted by the published `ticks_per_second`, instead of counting a duration it would have to invent down.

Zone exposure was the same until protocol 2.2, and is no longer. `elimination_grace_ticks` now travels in the royale mode-state block, so the HUD counts the real remainder down and the exposure ring thickens against the same denominator `zone_elimination` enforces. The elapsed reading survives as the fallback for a mode that publishes no grace — `sandbox` publishes the `none` block — because the one rule that has not changed is that this client never renders a duration the server did not send.

**The hill is read from the frame.** A `king_of_the_hill` frame carries the hill as an entity with a `hill` component, which `rendering/hillRenderer.ts` draws as a filled amber disc at the zone layer; the scores as `score` components on the participant entities; each player's progress toward its next point as `hill_presence`; and a knocked-out player's wait as `respawn_timer` on an entity that still has its name and score and has lost only its body. The mode-state block publishes the three constants none of that can supply -- `points_to_win`, `point_interval_ticks`, `time_limit_ticks` -- and `sessionSelectors.hillHudReport` resolves the whole hill section from them once per frame: time left as the limit minus the running ticks elapsed, the own score against the threshold, the own presence against the interval (a zero interval saturates rather than divides, like the grace), the return countdown, and the scoreboard as every `controllable` entity ranked by points with ascending entity id breaking ties. The HUD is keyed on that report rather than on `match.mode`, because the block's schema id is the closed vocabulary the client already fails closed on and the block is what every hill row divides by; a royale frame carries no block and renders exactly the rows it always did. The results overlay says how the winner won in the mode's own words -- the hill's is its points -- and, as everywhere here, no countdown is a clock: each is a tick difference from the frame.

**The lobby is operated here, through `LobbyPanel`.** While a match is in `lobby` and the welcome
advertised `start_match`, the sidebar shows the seat grid read from `match.seats` -- each seat empty,
a named person, or a named bot, with the session's own seat marked -- a seat-count control, a bot
menu, and Start. Every rule the panel renders is a selector in `lobbySelectors.ts` so it is testable
without a renderer, and every press is one closed command through `sendCommand`. Right-click on an
empty seat opens the bot menu and suppresses the browser's own; the seat is also a button, so the
same menu opens from the keyboard, and Escape or a click outside closes it. The menu is walked with
Tab; former WASD/arrow steering bindings are retired, not reassigned to lobby navigation.
`welcome.npc_controller_kinds` publishes unprofiled selections; optional `welcome.npc_profiles`
publishes complete `{npc_kind, profile_name}` choices. They are projections of one immutable
catalogue. `npcCatalogue.ts` owns their shared partition/identity checks and exact optional-profile
membership. `npcSeatOptions` in `lobbySelectors.ts` expands both projections in published order
into one scrollable menu. A profile button immediately submits its complete `seat_npc` declaration;
the browser keeps no secondary profile draft or numeric personality settings. Both joining and
occupied seats retain the profile label even when the bot has a display name. The seat-count
control is floored one above the highest occupied
seat and capped at `welcome.seat_count_maximum`, which are the two asks the tick would ignore, and it
sends one `set_seat_count` a quarter of a second after the last change: a dragged control emits a
change per step and the session's command bucket holds thirty tokens. Start is enabled exactly when
every seat is filled and every NPC seat has its controller, which is the server's own start
condition, so the button is never enabled for a press the tick would only remember.

The current welcome catalogue lives on `SimulationSessionIdentity` and in the API's snapshot
sequence context. Every published NPC seat and outgoing `seat_npc` must match it exactly; missing,
unknown, cross-kind, or malformed profiles fail before rendering or sending. Snapshot updates keep
that authority; reconnects and room changes replace it, and disconnect clears it. Legacy no-profile
welcomes, commands, seats, diagnostic choices, and their order retain their original wire shape.
`fixtures/tacticalProfileFrames.ts` supplies named protocol/UI cases, and
`e2e/blobRoyaleBrowserTacticalProfiles.spec.ts` covers configured and menu-selected declarations
through a fresh browser session in the same room. Profile parameters remain strictly authored
server configuration; combat settings are not exposed here.

**Room movement tuning** uses `useMovementTuning` above the conditional room view, so removing a
panel or replacing a body cannot restart its request IDs. IDs belong to the requested room and
the immutable welcome identity, never a body's ID or reused numeric controller identity. The
hook owns local drafts and UI timing; `SimulationApi` remains the only request/result correlator
and sender. `MovementTuningPanel` presents acceleration and normal top speed, published limits,
authored defaults, and the current room revision/effective tick independently of request outcome.
Editing sends nothing; Apply submits one complete pair and Reset submits authored defaults.

A dirty draft retains its values and base revision when another player changes the room. Review
explicitly accepts comparison against the currently displayed revision without replacing those
values; another room update requires review again. Only the API's matching committed result can
say applied/rejected. A changed shared revision or matching values never prove success, and
reviewing current values cannot turn an interrupted request's unknown outcome into applied.
Reconnect never automatically resends. The server-advertised minimum interval and rate refusal
bound new attempts; pending requests, invalid drafts, absent seated authority, and exhausted safe
IDs/revisions prevent submission. Body presence is not tuning authority.

The tuning section marks `data-gameplay-input="blocked"`, including its buttons. The canonical
`useThrustInput` clears held steering when focus enters that section or a native editing control,
allows native editing keys, and requires a fresh gameplay press after leaving. Unmarked camera
buttons retain their native activation bindings. The owner-selected movement control is cursor
direction plus held Space. Input observation uses the same Canvas projection as rendering;
normalization, go activation, throttling, and cancellation remain in `useThrustInput`. A stationary
pointer is re-evaluated when the body, camera, or viewport changes. Left drag remains camera panning,
not propulsion, and WASD/arrows no longer generate thrust. An observed body/session change discards
old activation; the wire cannot reveal same-entity body recreation wholly between snapshots.

`selectThrustInputOptions` is the shared feature/test composition for owned body availability,
session authority, observed stun containment, and persistent `controllable.input_generation`.
Stun locks against the authoritative snapshot's `[activation_tick, expiry_tick)` window; elapsed
browser time cannot unlock it. A fresh non-repeat Space press captures the exact optional
generation, and every held aim update and zero release retains that activation token. A generation
change cancels held and pending input even if every stun snapshot was missed, including old zero
releases. Sender-only replacement retains the old token for its cancellation zero; same-body
throttling survives generation changes. Generation survives same-entity returns but does not add
an identity for invisible entity destruction/replacement. Stun has an explicit non-visual renderer
registration until the ability presentation step.

`shield` joins it there, as of protocol 3.0's Step 18 row: an explicit non-visual registration with
a stated reason, until the ability presentation step draws it. The component publishes one
`activation_tick` and three absolute endpoints over it — `shield_expiry_tick`,
`perfect_expiry_tick`, `cooldown_expiry_tick` — plus `parry_stun_duration_ticks`, and
`sessionProtocolValidation` checks the orderings JSON Schema cannot: activation at most the
snapshot tick, `activation_tick <= perfect_expiry_tick <= shield_expiry_tick`,
`activation_tick <= cooldown_expiry_tick`, and a positive `parry_stun_duration_ticks`. A zero-length
protection window is **valid**, not a violation: a stun cancels protection by shortening it to the
cancellation tick while the cooldown keeps running, so a live component with no protection left is
an ordinary frame. Read the intervals against the snapshot's own tick; elapsed browser time unlocks
nothing, exactly as with stun. This step adds no sender — keys and buttons are the controls step, so
`SimulationApi` is still the only file in the domain that writes to a socket, and the outbound
`SessionCommand` union carries the shield shape without anything constructing one yet.

The domain depends on React, Ajv, browser Fetch/WebSocket/History APIs, and generated artifacts sourced from `docs/protocol/schema/v1` and `docs/protocol/schema/v3`. It has no dependency on process lifecycle, Axios, a router library, or class-shaped wire models; its one poll is the directory's, on a timeout chain rescheduled after each read rather than an interval, and it never touches the socket. Generated files are replaced only through `npm run generate:protocol`; `npm run generate:protocol:check` verifies drift without writing.
