# Simulation session domain

This directory owns the browser's complete Blob Royale capability: choosing a room from the protocol v3 lobby directory, joining that room's match session, decoding its frames, rendering the world, tuning shared room movement, and steering one blob. Its public exports are `SimulationApi`, `useLobbyDirectory`, `useMovementTuning`, `useRoomNavigation`, `useSimulationConnection`, `useThrustInput`, `entityRendererRegistry`, and `SimulationFeature` through `index.ts`.

**Rooms, since protocol 2.4.** The page is either on the directory or in one room, and the URL says which: `/` is the directory and `/?lobby=<id>` is a room, a query rather than a path because the bundle is served as a static directory behind `tailscale serve` with no single-page fallback. `useRoomNavigation` is the one place that decision is made and the URL written; a `popstate` reads the URL again, so the browser's back button is a leave. `SimulationShell` is the persistent frame: the app's name, a breadcrumb naming the level (`Rooms`, or `Rooms › Room 2`), and Leave. `SimulationFeature` mounts the three long-lived things above the view switch -- navigation, the connection into the current room, and the directory read while in none -- so choosing a room swaps the content and never re-creates a socket or a poll. Leaving is one transition: the room becomes `null`, the connection hook disposes its socket, and the directory hook, now enabled, reads the directory once immediately and then once a second, and only while the page is visible.

**What a browser cannot see.** The server refuses a join it will not admit with `404 LOBBY.NOT_FOUND`, `409 LOBBY.FULL`, or `503 LOBBY.UNAVAILABLE`, but the WebSocket API never exposes the HTTP response a declined upgrade was refused with: from here every one of them is a socket that closed before it opened. `useSimulationConnection` therefore reads the directory once after such a close and derives the refusal from the listing -- full, not serving, or not listed -- and sends the player back to the directory with that sentence as the notice, never retrying; a listing that predicts no refusal, or a directory that cannot be read, means the close was transport and keeps the bounded backoff. The one refusal that arrives as the server's own words is `1013 lobby_full`, the last-seat race lost after admission, which is a close reason and is read as one. `lobbyDirectorySelectors.ts` holds the rule and every sentence, so the directory's disabled Join buttons and the connection's refusals cannot disagree. The directory also predicts the one refusal admission cannot: a room whose every seat is filled past `countdown`, or filled with no bot to displace, has no seat a join could take, and its Join is disabled with that sentence rather than letting a hopeful session be closed `lobby_full`.

`SimulationApi` is the only transport implementation. It fetches immutable protocol v1 configuration without browser credentials, reads the lobby directory without caching it, enforces 10-second fetch and socket-connect timeouts, owns at most one room session socket (`/api/v3/lobbies/<id>/session`), validates the welcome, every snapshot, the directory, and every v3 failure envelope against the accepted v3 schemas before a component sees them, and is the only file in the domain that writes to a socket. `useSimulationConnection` is the only connection-state and reconnect policy: every retry uses a fresh API and configuration fetch, clears stale session state, follows the finite 1/2/4/8/16/16-second backoff sequence, and resets that budget only after a valid snapshot. `useThrustInput` is the only place a keyboard, or an ability button, becomes a command. The canvas, HUD, overlay, debug panel, and viewer are presentational and receive only validated readonly values.

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

**The cliff is the edge of the ground, and the void is everything the ground is not.** A cliff is
the boundary of _(envelope ∩ positive ground) − the union of holes_ — the same set
`terrain_supports_point` decides — so on every runnable map the corridor edge is the entire cliff,
and no shipped map or e2e fixture authors a hole at all. `drawTerrain` rims that boundary out of
geometry it already has, with no second geometry source and no computed region. Hole rims fall out
of the existing clip: each circle is stroked while the complement clips are still live, after the
ground pass and before the `restore()` that pops the stack, so the inner half of the stroke is
clipped away along with any arc interior to an overlapping neighbour, and what survives is the
outline of the union — correct by construction rather than by extra logic. Corridor rims are an
underprint, because Canvas exposes no outline operation for a wide stroke and the compiled road
boundary is `simulation::detail` rather than wire data: every polyline is stroked in the rim colour
at `2 * half_width + 2 * rim` and then in the road colour at `2 * half_width` over it, so the union
of the wide strokes minus the union of the narrow ones is exactly the road-union outline and
overlapping corridors need no special case. All rim passes precede all surface passes — interleaved,
one road's rim would overprint a crossing road's surface — and one `round` cap/join convention is
set once for both, because a corridor's positive ground is a union of capsules and the rim
offsetting it has to be one too. `TERRAIN_CLIFF_RIM_WORLD_UNITS` is authored in **world** units
beside the fill constants and projected like any other world quantity, so the rim scales with the
camera rather than describing it; both techniques spend it twice about the boundary and keep half,
so a hole rim and a road rim read as the same thickness.

**The arena border is not a cliff.** A hole edge kills you; the arena edge folds you back. The map
boundary keeps exactly the stroke `SimulationCanvas` already draws from `configuration.world`,
unchanged and unrestyled: restyling it would invent the second geometry source the single-owner rule
exists to prevent, and would also tell the player the wrong thing about what happens there.

**Void feedback is terrain-side, not per body.** The unsupported region — inside a hole on solid
ground, off-road on corridor ground — is a recessed mid tone under the near-black rim, and it is not
drawn by finding it. `TERRAIN_VOID_FILL` covers the whole envelope first, before the hole
complements, precisely so it shows through them; the ground passes then paint over everything that
is supported, and what is still showing the underlay is exactly _envelope minus supported ground_.
That is the whole of the void treatment, it is the bottom of the layer stack, and it is therefore
"beneath entities" in the z-order sense the stack has always meant. The reading that was rejected
is worth recording, because it is the one a reader reaches for first: a mark drawn under a body
whose centre is over void. The client cannot decide that. Answering "this centre is unsupported" is
a second implementation of `terrain_supports_point` and its asymmetric tolerances, which that
function's own note and ADR 0008 both forbid, and the antialiasing licence covers inexact _pixels_,
not a computed boolean. The state also never occurs: a ground-bound body
terminates at its last supported point and loses its body in the same tick, so no snapshot ever
carries a ground-bound centre over void, and the only bodies out there are `floating` hazards that
by definition never fall — marking one would be a confident lie. The registry could not host it in
any case: one registration per component kind, and the canvas is barred from branching on a kind.

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
session authority, observed stun containment, persistent `controllable.input_generation`, and, since
Step 21, the published half of each ability's suppression.
Stun locks against the authoritative snapshot's `[activation_tick, expiry_tick)` window; elapsed
browser time cannot unlock it. A fresh non-repeat Space press captures the exact optional
generation, and every held aim update and zero release retains that activation token. A generation
change cancels held and pending input even if every stun snapshot was missed, including old zero
releases. Sender-only replacement retains the old token for its cancellation zero; same-body
throttling survives generation changes. Generation survives same-entity returns but does not add
an identity for invisible entity destruction/replacement. Stun is drawn as of Step 20, against this
same window and this same tick, so the mark on a peer and the lock on the own blob cannot disagree.

`shield` arrived in protocol 3.0's Step 18 row, registered non-visually at first and drawn as of
Step 20 alongside stun. The component publishes one
`activation_tick` and three absolute endpoints over it — `shield_expiry_tick`,
`perfect_expiry_tick`, `cooldown_expiry_tick` — plus `parry_stun_duration_ticks`, and
`sessionProtocolValidation` checks the orderings JSON Schema cannot: activation at most the
snapshot tick, `activation_tick <= perfect_expiry_tick <= shield_expiry_tick`,
`activation_tick <= cooldown_expiry_tick`, and a positive `parry_stun_duration_ticks`. A zero-length
protection window is **valid**, not a violation: a stun cancels protection by shortening it to the
cancellation tick while the cooldown keeps running, so a live component with no protection left is
an ordinary frame. Read the intervals against the snapshot's own tick; elapsed browser time unlocks
nothing, exactly as with stun. Step 18 added no sender for it — keys and buttons were the controls
step — and Step 21 is that step: `SimulationApi` is still the only file in the domain that writes to
a socket, and the shield shape the outbound `SessionCommand` union has carried since Step 18 is now
constructed, with the explicit `input_generation: null` its schema requires.

`charge` arrived a step later, in protocol 3.0's Step 19, as the second outbound command shape and
the second to wait until Step 21 for a sender — and it is the one ability component still registered
non-visually. As of Step 20 that is a settled decision rather than a deferral, and its reason says
so instead of pointing at the step that would resolve it. The component publishes only
`activation_tick` and `cooldown_expiry_tick`, and `sessionProtocolValidation` checks the two
orderings JSON Schema cannot — activation at most the snapshot tick, and `cooldown_expiry_tick`
**strictly** greater than `activation_tick`. That strictness is the one place a reader must not
carry shield's rules across: a zero-length shield window is a valid cancelled shield, while a
zero-length charge cooldown is unreachable by construction, so a frame carrying one is malformed and
fails closed. Read the interval against the snapshot's own tick; elapsed browser time readies
nothing.

Two client-side traps are worth naming because both are ways to draw a confident lie. **The
direction is not a strength**: the outbound `charge` payload's `x` and `y` are normalized by the
server, so `{x: 0.5, y: 0}` charges exactly as hard as `{x: 1, y: 0}`, and a control must not scale
a burst by pointer distance the way an analog thrust legitimately may — Step 21's sends the same
normalized unit aim the thrust path produces, and nothing between the pointer and the wire carries a
magnitude. Its
`input_generation` is **optional** — omitted, never null — following `set_thrust` rather than
`shield`, because the payload has other required members. **And the burst does not decay.** The
server ships `drag_per_second=0`, so a charged body keeps its speed until something else changes it,
and nothing published bounds that speed: `match.movement`'s ceiling is a propulsion parameter and
the server's safety envelope is not on the wire at all. A client that animated a decaying burst, or
derived a maximum speed from `match.movement`, would be drawing a world the server is not
simulating.

**Combat feedback, and what it can and cannot promise.** Step 20 turns shield and stun into drawn
kinds and leaves charge non-visual, and it carries the one client-contract change the wire did not
force: `EntityRenderFrame` gains `tickSequence: number | null`, threaded from the snapshot in
`SimulationCanvas` under the rule `eliminationGraceTicks` and `ownEntityId` already follow — a
renderer receives what it may read and never goes looking. Nothing else widens; terrain in
particular stays off the frame. Without the tick a renderer could read only component _presence_,
which is exactly the reading the published shield forbids, and would paint protection on a shield
the status system already cancelled.

`rendering/shieldRenderer.ts` therefore answers three states rather than two: the perfect opening
while `activation_tick <= tick < perfect_expiry_tick` as a gold pair of rings straddling the
ordinary radius, ordinary protection while `activation_tick <= tick < shield_expiry_tick` as one sky
ring, and **nothing at all** otherwise. The third is the majority of published shield frames by
duration — 160 protected ticks inside a component that lives at least 360 — and it is the whole
argument against presence-keyed drawing: a live cooldown is not protection.
`rendering/stunRenderer.ts` draws a broken violet ring on the same rule over
`[activation_tick, expiry_tick)`, the identical window `selectThrustInputOptions` locks input
against, with the break carrying the meaning as a shape rather than a hue. Both take geometry from
the entity's own `physics_body`, as `zoneExposureRenderer`, `lethalOnContactRenderer` and
`controllableLabelRenderer` already do, and with the same discipline: a bodyless tick is legal —
Step 18 declined a `dependentRequired` edge precisely so it would be — so an absent body returns
silently, neither renderer caches a previous position, and neither constructs a projection input it
has not checked. A frame that carries no tick draws nothing, because a guessed tick would be a
guessed state. Their ring widths are logical CSS pixels, deliberately unlike the terrain rim's world
units: a cliff rim is geometry and must scale with the ground it rims, while a status ring is a
readability affordance that has to stay visible when the blob is three pixels across. They share a
`status` layer added between `exposure` and `label`, because `visualEntityRenderers()` falls back to
the kind's spelling inside a layer, and spelling is not a reason for one mark to paint over another.

**A perfect mark is a state mark, not an event mark.** The perfect opening is
`shield_perfect_window_seconds=0.08` — thirty-two ticks at the four hundred ticks per second the
server runs — while what reaches the browser is `presentation.snapshots_per_second`, twenty in the
checked-in configuration and the configuration's to change. The mark is drawn on the frames that
land inside the window and on no others, so at a coarser cadence a real perfect parry can produce no
marked frame at all, and the absence of a mark is not evidence that there was no perfect opening.
Nothing here interpolates between frames or animates toward the next one: the only clock this client
may read is the tick the snapshot carries.

**The screen-space readouts state remaining time and never state availability.** A cooldown must not
pan and scale with the camera, so it is a HUD row rather than a world-space mark, and the HUD is
also the only place that can reach `ticks_per_second` to turn a tick difference into seconds.
`sessionSelectors.abilityStatusReport` derives it once per frame from the own blob's components on
`selectThrustInputOptions`' template — absolute committed ticks read against the tick of the frame
that carried them — and `SimulationHud` only formats what it returns. Each window reports whether it
covers the tick, seconds remaining (clamped at zero once the endpoint has passed), and the elapsed
share of `endpoint - activation_tick`: stun over `expiry_tick`, shield protection, perfect and
cooldown over their three endpoints minus the one shared `activation_tick`, and charge cooldown over
`cooldown_expiry_tick`. **A stun fraction is not monotonic**, and nothing may assume it is: a merge
keeps the original activation and takes the maximum expiry, so the denominator grows while the
numerator's origin does not, and the share moves backwards. That division is guarded in one place
rather than trusted at every call site, because an empty window is a legal frame on the shield side
of the contract — a cancelled protection window, and the `cooldown_expiry_tick == activation_tick` a
zero authored cooldown publishes — where charge's strictly positive cooldown could never reach it.
An empty window reports no fraction at all and is never active, so no row renders a ratio it could
not compute. Two things are **not** derivable and are never implied. **No row says
charge is ready**: the final refusal belongs to the authored safety envelope, which is deliberately
server-side and not on the wire, so an elapsed cooldown reads _cooldown over_ and a live one reads
_cooling_. And **no row shows the shield's authored window length**: only the protection that
actually happened is published and a cancellation shortens it, so the row counts down the remainder
of the window in front of it and a "0.4 s of protection" bar could be neither drawn before a first
activation nor trusted after a cancelled one. The stun row is narrower still — it exists only while
the window contains the tick, because a published-but-elapsed stun locks nothing and a row saying
"Stunned" would be false.

Two rules bound every drawing path added here. **Nothing may throw**: there is no error boundary
anywhere in this client and the draw loop is an unguarded `useEffect`, so one throw from one
renderer blanks the whole application — an absent body returns, a zero denominator is guarded, and a
projection input is never constructed before it has been checked. **And a renderer is never where
something unknown is quietly skipped**: that clause is a validation rule, enforced by
`assertKnownSnapshotKinds` failing closed ahead of Ajv, and it forbids trimming a frame instead of
rejecting it. `protocolMutationCorpus.test.ts` carries the strict v3 half of that promise for the
three components this step reads — a shield window opening after the frame that carries it, a stun
window that expires on the tick it activated, and a charge cooldown that runs backwards — each
proving the whole frame is refused rather than handed to a renderer that would draw nothing and look
correct doing it.

**What the gate establishes, and what it does not.** `verify-web` asserts call sequences and
geometry arguments; it never looks at a pixel, and no shipped map authors a hole, so the hole-rim
path is exercised only by synthetic terrain fixtures. A green gate says the right calls happen with
the right numbers under translation, manual and follow camera, edge view, fractional device pixel
ratio and resize. It does not say that a cliff reads as a cliff, or that a perfect mark is legible
in the eighty milliseconds it exists. That judgement is the owner's, from images, not from this
suite.

**Ability controls take two keys the arena already gave up.** Step 21 binds shield to
`SHIELD_KEY_CODE = 'KeyS'` and charge to `CHARGE_KEY_CODE = 'KeyD'`, matched on `event.code` beside
`THRUST_GO_KEY_CODE` so both are layout-independent rather than letter-independent. The pool is not
an arbitrary reach: ADR 0008 records of Step 11a that WASD and the arrows no longer steer and that
those keys are available for later charge and shield bindings, and Step 11a froze exactly that pool
as `REMOVED_DIRECTION_KEYS` and pinned its inertness in three tests. Spending two of them shrinks
that pool to `KeyW`, `KeyA` and the four arrow codes, which stay reserved and stay asserted inert,
and turns the two it spent from stale reserved-key assertions into the binding cases the ability
tests press. This is not the return of directional steering: neither ability key steers anything,
and a held ability key is not a held axis. `KeyS` is the home-row key under the middle finger of the
hand whose thumb holds Space — the fastest key to reach without moving the go hand — and shield is
the reactive move; `KeyD` is adjacent under the index finger, and S for shield, D for dash is
ADR 0008's own mnemonic. Both abilities also get ordinary on-screen buttons, which are the
accessible path rather than a convenience, and both paths run through one activation function.

**Shift was rejected, and not as a matter of taste.** ADR 0008 proposed Shift for shield, and a
dated amendment supersedes that proposal there rather than leaving two answers on the record. The
go-key guard filters `altKey`, `ctrlKey` and `metaKey` and deliberately does _not_ filter
`shiftKey` — the pinned modifier test enumerates exactly those three plus `isComposing` — so
Shift+Space thrusts today, and a bare-Shift shield would therefore fire on the leading half of every
Shift+Tab a keyboard user makes. Five presses of Shift is also the Windows Sticky Keys gesture;
Shift is two codes, `ShiftLeft` and `ShiftRight`, against a one-constant-per-action model; and
modifier keys do not auto-repeat at all, so the key-repeat rule ADR 0008 requires could only have
passed vacuously against it.

**One input owner, and deliberately not one send path.** The ability bindings live inside
`useThrustInput`'s one layout effect and inherit its whole lifecycle for free: the editing and
camera-button guard, the camera-gesture and blur cancellations, and the body, session, welcome and
`input_generation` lifetime that already decides when held thrust dies. What they do not reuse is
`flush`, and the distance between "the existing sender" and "the existing send path" is the whole
point. `flush` implements a _level_: its change-only gate would silently swallow a second identical
pulse, which for a pulse is not a redundant send but the player's next shield; its 50 ms coalescing
timer would park that pulse for up to 50 ms against an 80 ms perfect opening; its single parked
timer can drop a pending send on an effect re-run; its `currentTransmission.direction` bookkeeping
is level state a pulse does not have; and its shared refusal latch means one refused ability send
would drop held thrust with it. What an ability reuses is the `sendCommand` reference and the guards
around it, never the fifty-odd lines of level semantics; the activation path does not even read the
sender's result, because consulting it would be the first step back toward that shared latch.

**Two seams carry the abilities, and both exist to keep them out of the input lifetime.**
`ThrustInputOptions` gains `abilityUnavailable`, one boolean per `SimulationAbility`, and
`ThrustInputControls` gains `activateAbility(ability)` — the stable activation boundary a button
calls, exactly as `observeAim` is the stable observational boundary the Canvas calls. Availability
moves with almost every snapshot and the input effect must not be rebuilt when it does: a rebuild
re-declares `goHeld`, so a cooldown merely _starting_ would drop the thrust a player is holding. So
availability is written to a ref after every commit and read only from inside an event handler, and
the effect keeps the narrow dependency list Step 11a gave it. `activateAbility` returns nothing on
purpose — a successful send is not an activation, only the published component proves the tick
admitted the pulse — so no caller can manufacture readiness out of having called it. And
`SimulationAbility` is `Extract<SessionCommandKind, 'charge' | 'shield'>` rather than a fresh string
union, so a renamed or retired command kind is a compile error here instead of a binding that
silently sends nothing. `ThrustTransmission` became `InputTransmission` in the same step, because
what it carries across a rebuild is now the session's input rather than thrust alone: the remembered
aim and the per-ability last-attempt timestamps are body state, and re-deriving them from nothing on
every rebuild is exactly the bug described next.

**Rate discipline is mandatory, because the bucket closes the socket rather than refusing.** The
per-session command budget is a token bucket of capacity 30 refilling at 20 per second, the token is
charged before any parsing, and an exhausted bucket is `1008 command_rate_exceeded` — a disconnect
mid-match, not a refused command. Held thrust with a moving cursor already runs at the full 20/s
refill rate, so two unthrottled ability keys on top of it drain the burst in seconds, and they buy
nothing doing it: the server's mailbox coalesces same-kind inputs, so several pulses inside one tick
are at most one attempt. The primary mitigation is therefore published state rather than a timer —
an activation is suppressed while the client can see a live cooldown for that ability — and
`ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS`, applied per ability, is the backstop for the gap
between a press and the snapshot that would show that cooldown. The authored cooldowns are
`shield_cooldown_seconds=0.9` and `charge_cooldown_seconds=1.2`, so its third of a second costs a
player nothing they could otherwise have spent.

**The two payloads are not the same shape, and the wrong one fails silently.** `charge` carries
`x`, `y` and an _optional_ `input_generation`, exactly as `set_thrust` does, so it may reuse that
expression verbatim. `shield` may not: its member is required and nullable, so the never-invalidated
entity sends an explicit `input_generation: null` where charge omits the member entirely. Copying
the thrust expression into shield produces `{}`, the closed envelope refuses it, and `sendCommand`
logs a refusal and returns false — with no wire receipt, on exactly the players who have never been
stunned, which is everyone at the moment they first press shield. The two are encoded separately and
the never-invalidated case is tested for both. The charge direction is the same normalized aim the
thrust path produces, and the rule above still holds on it: the server normalizes, so a direction is
not a strength and pointer distance never becomes one.

**Remembered aim now survives a stun, which it did not before this step.**
`lastNonzeroAimDirection` is a closure-local of the layout effect, and that effect's dependencies
include `inputLocked` and `inputGeneration` — so every stun rebuilt the closure and wiped the
remembered aim, leaving a just-unstunned player with no aim to charge along until they moved the
mouse. The contracted discard set is body loss, body replacement, a new welcome, and disconnect, and
nothing else; the same-body condition that already carries the transmission across a generation or
lock change now carries the remembered aim on `InputTransmission` too. The direction an activation
sends is resolved _inside_ the effect by `chargeDirection`, never from the exposed React state in a
button's `onClick`, because the exposed value can lag the closure by a commit. That one function is
also the only place the fallback rule lives: current aim wins whenever it exists, an exact-centre
cursor falls back to the remembered nonzero aim, and `null` means no aim has ever existed on this
body. The fallback is deliberately the opposite of the centre-thrust rule two paragraphs of this
file already state, and the asymmetry is the wire's: a level with no direction is honestly zero
thrust, while a pulse with no direction is a zero vector the server refuses outright. And the
ability keydown does not inherit the thrust path's `!hasObservation` guard: that guard is a
held-control rule, and applying it here would refuse exactly the off-canvas case the last-nonzero
fallback exists for.

**A pulse still needs a held latch.** "A one-shot has no release" is true of what it sends and false
of what it must observe. The thrust path rejects a repeat with `event.repeat || goHeld`, and the
second half — the one keyup clears — is what makes it robust when a browser or a synthetic event
omits `repeat`; each ability keeps the same latch in a per-ability `heldAbilityKeys` set, and every
cancellation source clears it, blur included. Blur is exactly why the release is not left to keyup:
a blur swallows the keyup, and a latch that outlives its press wedges the key, so the next real
press reads as a repeat and that ability never fires again for the rest of the match. The latch
records that the key is physically down rather than that a pulse went out, so it is set on every
press this owner accepts and not only on one that sends: an activation suppressed by a live cooldown
still has to refuse the auto-repeat streaming along behind it.

**A button is a second repeat source, and it must not steal the go key.** A focused `<button>`
activates on Enter _keydown_ and repeats while Enter is held, and it also activates on Space, which
is propulsion. So each control takes both keys back: Enter and Space are prevented on the way in and
re-expressed as exactly one activation per press through `event.repeat`, and Space is prevented on
keyup as well, because keyup is where a space press actually activates a button. Behind both entry
points is the one `activate` closure, with the same availability check, the same generation token
and the same per-ability interval, so a button can never activate what a key could not. **And a
click must hand Space back.** A click leaves the button focused, `blocksGameplayInput` then reads
that focused button as UI and swallows the ability key, and the browser activates the focused button
on the next Space — the go key would quietly become the shield key for the rest of the match. A
click with a real pointer behind it (`event.detail > 0`) therefore blurs; a keyboard activation
never reaches `onClick` at all, and an assistive technology's synthesized click carries no click
count, so neither of them loses the focus its user is navigating with.

**Availability says why not, and still never says ready.** Step 20's rule is unchanged, and it is
why every value the `@canonical session_ability_availability` selector publishes
(`abilityAvailabilityReport`) is a negative: `canAttempt` says only that no reason the client can
see refuses this activation, there is no `isReady` member, and no sentence
spells "ready" or "available", because charge's last refusal is the authored safety envelope, which
is deliberately server-side and not on the wire. The reasons are asked in the server's own gate
order, taken from `ability_system.cpp`, so the single sentence a control shows names the gate that
would really refuse this pulse first rather than whichever check the selector happened to write
first. The kind is not among this welcome's advertised commands — `sendCommand` returns false for an
unadvertised kind before anything reaches the socket, so a control for one would otherwise look live
and silently no-op. There is no body here to command, which deliberately folds "not connected" and
"seated in another room" into the one answer a control can give. The match is not running, which
also subsumes the tick-zero refusal, since a match that has never run has never left `lobby`. The
blob is stunned — literally the boolean the thrust sender obeys, because both read one
`ownBodyFrame`. Live shield protection, which refuses **both** abilities and not only charge: a
re-tap would restart the perfect opening, a blob may not charge out of its own guard, and in a room
that authored a zero shield cooldown this term is the only thing refusing the re-tap. This ability's
own live published cooldown. And, for charge alone, no aim yet.

**One authority, two readers, and one predicate.** `publishedBlockingReason` is shared:
`selectThrustInputOptions` reduces it to the one bit a sender can act on and hands it to the input
owner, while the report turns it into the sentence beside the button, so a key press cannot do what
the control next to it says is impossible, and the two cannot drift the way a second copy of the
rule would. The rejected alternative is the obvious one — let the availability selector re-read
`stun`, `physics_body` and the tick for itself. Both window reads go through the plain containment
predicate and deliberately _not_ through `abilityStatusReport`: that report reads the same windows
against the same tick, but it needs a positive `ticks_per_second` to divide by and reports nothing
at all without one, and an activation gate must never fall open because a cadence was missing. Same
components, same predicate, so a row reading "Cooling 0.6 s" and a control reading "cooling down"
cannot disagree about whether the window is live. The one reason outside the shared half is the aim,
which is not published state at all: it lives inside the input owner, so it arrives as an _input_ to
the report rather than a read — feeding a hook's own state back through its own options would be a
loop — and a zero vector counts as no aim, because the charge schema records that a zero-magnitude
direction is a silent refusal consuming no cooldown rather than a scaled-down burst.

Those reasons and the controls live outside the Match-status table on purpose: that table is pinned
against both `/ready/i` and `/available/i` — and "unavailable" contains the second — a second
assertion pins its rowheader list exactly, and a control is not a status row in any case.

**An unavailable control stays reachable, which is why it is not `disabled`.** The camera buttons
and the lobby's Start express "present but not pressable" with the `disabled` attribute, and for
them that is right: their reason is ambient and visible in the panel around them. Here the reason
_is_ the control, and `disabled` removes the element from the tab order — so the one node whose
`aria-describedby` carries that sentence would be the one node a screen-reader user could never land
on. Each ability button therefore carries `aria-disabled`, refuses the press itself rather than
having the browser refuse it, and lets `App.css` draw the look `:disabled` would have drawn. Each
reason is a settled sentence and never a countdown: the seconds live in the ability rows of the
status table, which read a published window against the frame's own tick, and a reason that changed
every snapshot would be an `aria-describedby` re-read every snapshot. For the same reason the
section is not a live region. Each button also advertises its key with `aria-keyshortcuts`, and both
that label and the hint under the controls are derived from `SHIELD_KEY_CODE` and `CHARGE_KEY_CODE`
rather than typed a second time, so a rebinding cannot leave the screen and the keyboard disagreeing.

**The accessibility limitation, stated plainly rather than buried.** Aim exists only for a mouse
pointer inside the canvas. `observeAim` is fed by the Canvas's pointer handlers, and
`lastNonzeroAimDirection` only ever becomes nonzero after a pointer has moved over the arena, so a
player using touch, a pen, or the keyboard alone never has an aim at all: shield is fully usable
from the keyboard and from its button, being a pulse with no direction, and **charge is permanently
unavailable to them**, reporting `no_aim` with "Move the pointer over the arena to aim first."
showing forever — which is at least the honest rendering of it. In a step titled "accessible ability
controls" that is a real gap, and it is recorded here rather than described as a niche case.
Closing it means inventing a keyboard aim — a held direction, the last thrust direction, a target
lock — which is a control design neither the plan nor ADR 0008 authorizes and which would be a
second aim owner beside the cursor. It is therefore a named open question for the owner, not a
shipped decision.

The domain depends on React, Ajv, browser Fetch/WebSocket/History APIs, and generated artifacts sourced from `docs/protocol/schema/v1` and `docs/protocol/schema/v3`. It has no dependency on process lifecycle, Axios, a router library, or class-shaped wire models; its one poll is the directory's, on a timeout chain rescheduled after each read rather than an interval, and it never touches the socket. Generated files are replaced only through `npm run generate:protocol`; `npm run generate:protocol:check` verifies drift without writing.
