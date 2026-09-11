<!-- canonical: lobbies_as_rooms -- N concurrent matches behind one listener, one socket per join -->

# 6. Host several lobbies as rooms behind one listener

* **Status:** Accepted. Drafted 2026-09-08; the owner agreed all four decisions on 2026-09-09; accepted 2026-09-09 at Step 17 of `.claude/plans/2026-09-09-lobbies-as-rooms.md`, with Steps 1 to 16 of that plan built and verified. Where the built thing departs from the text below, a dated amendment says so beside the text it amends; the decisions are otherwise left as agreed.
* **Date:** 2026-09-08
* **Deciders:** Project owner

## Context and Problem Statement

The owner wants a player to connect, see which lobbies exist, join one, play, and leave it,
returning to a lobby browser rather than being disconnected. Four facts in the tree at `c05b770`
make that a restructure rather than a client change:

* The server owns exactly one simulation. `BlobRoyaleApplication` holds one
  `runtime::SimulationRuntime` (`src/application/blob_royale_application.hpp:87`) driven by one
  worker thread.
* Every session is bound to that simulation at upgrade time. `MatchSessionContext`
  (`src/server/match_session_context.hpp`) carries one `CommandSink&` and the server holds one
  `const SnapshotPublication&`, and `/api/v2/session` hands both to every admitted socket.
* The welcome names the match: `entity_id`, `controller_id`, `mode`, `map`,
  `accepted_command_kinds`, `npc_controller_kinds`
  (`docs/protocol/schema/v2/welcome-data.schema.json`).
* Every bound is per server and therefore per match: 32 sockets, 1,024 published entities, one
  egress budget (`src/server/server_limits.hpp`, `src/protocol/protocol_v2_constants.hpp`).

Protocol v2 § "Explicitly out of scope" lists multiple rooms and says a second room "is treated as
a new protocol amendment that defines authentication and ownership together". This is that
amendment. Authentication stays the tailnet; ownership stays the socket.

The review that precedes this note (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`) found
that a session's leave path can orphan its entity, that a match can start with nobody in it, and
that the seat count escapes the map's bound. Those are not lobby-browser problems, but every one of
them is a session-lifecycle problem, and this design fixes them as part of its own mechanism rather
than beside it.

**The target, stated so the design can be measured against it.** One host, `cole-ubuntu-pc`, AMD
Ryzen 5 5600G, containerised with `--cpus 2 --memory 1g` (ADR 0001). A handful of friends: at most
32 concurrent sessions, at most 8 per person, in **up to four concurrent rooms of up to eight seats
each** at the deployed configuration, with a hard ceiling of eight rooms compiled in. Anything above
that is a scale nobody asked for and is deliberately not designed for.

The constraints that bind, from the brief: determinism per match, the accepted physics baseline
unchanged, fail closed on configuration, one capability with one implementation, a protocol minor
for any new kind or member, and verification on both compiler lanes.

## Considered Options

### Routing: how a session reaches a room

* **A. One socket per join, an HTTP directory for browsing.** `GET /api/v2/lobbies` lists the
  rooms; `GET /api/v2/lobbies/<lobby_id>/session` upgrades into one. Leaving is closing the socket
  and re-reading the directory. Every v2 invariant stays literally true: one connection is one
  match, one `controller_id`, one strictly increasing `tick_sequence`, and the welcome still names
  the match because the socket really is bound to one.
* **B. One socket per player, `join_lobby` and `leave_lobby` commands, a re-targeted stream.** The
  browser is pushed rather than polled and a player never re-upgrades. The cost is that the
  snapshot stream changes identity mid-connection: `tick_sequence` restarts, `entity_id` and
  `controller_id` come from a different room's id spaces, and a third server message kind is
  needed to announce the change. Under v2's own rules that changes what a session means, so it is
  a major version: a new route, a new subprotocol, a session state machine on the server, and a
  stream epoch on the client.
* **C. Keep one match and build only the client's lobby view.** The honest "do nothing" option.
  It delivers the seat grid and Start, and it cannot deliver "see what lobbies exist" or two groups
  playing at once.

### Stepping: one loop over N worlds, or N loops

* **A. One `SimulationRuntime` per room, each with its own worker thread.** The class is unchanged.
  A room's tick is a function of its own committed world and its own mailbox, exactly as today, so
  ADR 0002's single writer and ADR 0003's serial reference hold per room. A throw in one room's
  step moves that runtime to `kFailed` and touches nothing else. Cost: N threads waking at 400 Hz
  on 2 CPUs, each with its own clock.
* **B. One worker stepping N worlds in sequence each quantum.** One clock and one number for
  "how late are we", and N is bounded explicitly by the sum of step times. Cost: one slow room
  delays every room, a throw inside the loop needs a new failure boundary that removes one world
  and keeps stepping the rest, and `SimulationRuntime` is rewritten around a list.

### Lobby lifecycle: who creates a room

* **A. A fixed pool from configuration.** `[lobbies] count=4` creates four rooms at startup from
  the `[match]` template; the server names them by ordinal; nothing reaps them. An empty room is
  an idle runtime in `lobby`.
* **B. Dynamic rooms.** `POST /api/v2/lobbies` creates, a name is chosen, an idle room is reaped
  after a timeout, a global cap refuses creation. Each of those is a new decision, a new error
  code, and a new abuse surface, and none of them is needed to put four rooms on a wall for a
  handful of friends.

### Seating a person

* **A. Server-issued `join` and `leave` commands.** Admission enqueues a `join` for the session's
  controller; the tick seats it in the lowest empty seat, displacing the lowest-indexed NPC seat
  when none is empty (the owner's 2026-09-08 decision). `close_session` enqueues a `leave`; the
  tick frees the seat and destroys every entity that controller drives. Both are ordinary commands
  in the batch, so seating and leaving are replayable and race-free by batch order.
* **B. The runtime writes seats directly.** Rejected before it was considered: nothing outside a
  tick may hold a `GameWorld&` (ADR 0002 § "Ownership and lifecycle").

## Decision Outcome

**Amended 2026-09-09 (plan Step 17, acceptance).** Plan Steps 1 to 16 built this design: the two
server-issued commands, the seat reconciliation, the bounded clock, N rooms behind one listener, the
directory and the room target, protocol 2.4, the client's directory and lobby views, and a browser
flow across two rooms. The departures are recorded beside the text they amend -- § "The lobby
lifecycle" (no spectators; `lobby_full` before the welcome; "Room", not "Lobby"), § "Protocol
consequence: 2.4" (which schema and constant names shipped, and which envelopes name the room),
§ "Failure modes" (what a browser can and cannot see of a refusal), and § "Backpressure and
admission" (what `409` names) -- and one fact this text never stated is now stated in
`docs/protocol/v2.md` § "The lobby directory": admission compares against the room's *live*
`seat_count`, which anyone in that lobby may change, so an occupant who shrinks a lobby lowers what
admission accepts, which is no more authority than the lobby commands already grant.

**Chosen: routing A, stepping A, lifecycle A, seating A.** Rooms are a fixed pool of
`SimulationRuntime`s behind one listener; a player joins a room by opening one socket to it and
leaves by closing that socket; the browser is a polled HTTP page. This is protocol **2.4**.

### Rooms

A **room** is one `SimulationRuntime`, one `ControllerHost` for its bots, one `MatchSessionContext`,
and one small integer `lobby_id` issued by ordinal from 1. The application owns a `std::vector` of
them, constructed from the `[match]` template with `seed = [match] seed + (lobby_id - 1)` so two
rooms never replay the same hazard stream, and the same `[royale]` and `[hazard.*]` sections. The
count is a required key, `[lobbies] count`, validated in `[1, kMaximumLobbyCount]` with
`kLobbyDirectoryLimit = 8` in `protocol_v2_constants.hpp`, the directory's `maxItems`; the deployed configuration says `4`. Per-room maps
and modes are not built (§ "What is deliberately not built"); the section-family loader that
`[hazard.<kind>]` already uses is the obvious home for `[lobby.<ordinal>]` overrides when someone
wants them.

`lobby_seat_count` moves from `[royale]` to `[match]`: it is a fact about who is playing, not a
royale balance number, and the roster is seeded **only for a mode whose accepted command kinds
include `start_match`**. `sandbox` publishes the empty roster the protocol already promises
(review finding 5).

### The routing model

Protocol v2 gains two targets and keeps its one.

| Method | Target | Purpose |
|---|---|---|
| `GET` | `/api/v2/lobbies` | The directory: every room's `lobby_id`, `mode`, `map`, `phase`, `phase_started_tick`, `tick_sequence`, `seat_count`, `seat_count_maximum`, `filled_seat_count`, `npc_seat_count`, `session_count`, and `healthy`. Read from each room's latest published snapshot and its session count; no lock on any tick. |
| `GET` + upgrade | `/api/v2/lobbies/<lobby_id>/session` | Join one room. Same subprotocol `blob-royale.session.v2`, same welcome, same snapshot stream, bound for the socket's life to that room's sink and publication. |
| `GET` + upgrade | `/api/v2/session` | Unchanged, and defined to be room 1. The existing client and the browser flows keep working without an edit. |

`lobby_id` is the one parametric path segment the router has. Its grammar is `[1-9][0-9]{0,2}`,
matched exactly after HTTP parsing as every other target is; a segment outside the grammar or
naming no room is `404 LOBBY.NOT_FOUND`. There is no client-chosen string anywhere in this
amendment: rooms are named by the server, and the only bytes a client chooses are still the closed
command payloads.

The welcome gains two members, `lobby_id` and `seat_count_maximum`, both required. The second is
the map's spawn-marker count, published so a seat-count control has a real ceiling and so the
refusal taxonomy's line holds: a client that could have known the bound is refused, a client racing
committed state is ignored (review finding 3).

`GET /api/v1/snapshots` and the v1 routes read room 1, unchanged. v1 is not extended.

**Why the welcome still names a match.** Under option A a socket is bound to one room for its
whole life, so `welcome.entity_id`, `controller_id`, `mode`, `map`, and the accepted kinds are
facts about that socket, exactly as v2 says. The brief's worry, that the welcome would have to stop
implying a match, is real only under option B, and it is the reason B costs a major version.

**Leaving.** A client leaves by closing the socket with `1000 normal`. On the server that is the
existing close path plus the `leave` command below. The client returns to the directory view and
re-fetches it. The thing that outlives a match is the browser tab, not a WebSocket, and this design
says so rather than pretending a re-targeted stream is free.

**Option B is not foreclosed.** A per-room `MatchSessionContext` and a `LobbyDirectory` keyed by
`lobby_id` are exactly what a v3 re-targetable session would bind to. If the product ever serves a
public matchmaking population, that is the point at which one socket per player earns its major
version. It does not earn it for four rooms on a tailnet.

### The tick-loop decision

**N runtimes, N threads, the class unchanged**, with two additions to `SimulationRuntime::run`:

1. **Bounded catch-up.** Today `next_tick += tick_duration` with no bound
   (`src/runtime/simulation_runtime.cpp:250`), so a stalled worker ticks back-to-back until it is
   caught up. Under the container's CFS quota a throttled 100 ms becomes 40 consecutive ticks, which
   is the burst that gets throttled next; with N rooms it is N synchronised bursts. The worker
   catches up at most `kMaximumCatchUpTicks = 4` (10 ms) and then re-bases `next_tick` to now,
   counting `clock_rebase_count` and the ticks it fell behind. Tick numbers are never skipped; the
   room runs in slow motion for a moment instead of sprinting. Determinism is untouched, because
   determinism is per tick sequence, not per wall clock.
2. **Overrun accounting.** Per room: `tick_overrun_count` (a step that ended after its deadline),
   `maximum_tick_duration`, and `maximum_lateness`, exposed beside the mailbox statistics and
   logged by the control loop when they rise, the same way `runtime.command_dropped` is logged
   today.

**The budget, and what happens when N worlds cannot be stepped inside it.** The quantum is 2.5 ms.
The acceptance number is **a mean step of at most 250 µs and a p99 of at most 1 ms per room at the
deployed roster** (eight seats, `wanderer:1`, the comet and boulder table, the 32-marker map),
measured on `cole-ubuntu-pc` with `./scripts/run-benchmarks-linux` extended by one royale case.
There is no such measurement in the tree today (`benchmarks/README.md` says the only authoritative
host has never produced a baseline), so this is the first thing the plan measures. At that budget
eight rooms are 8 × 10 % of one core's quantum, on two cores. If the measurement fails the budget,
`[lobbies] count` is lowered in configuration, and the design does not change. If a single room
cannot meet it, the entity or hazard bound is wrong, not the loop.

**Amended 2026-09-09 (plan Step 8, advisory measurement).** `royale_deployed_roster` exists in
`benchmarks/blob_simulation_benchmarks.cpp`: it reads the deployed `[simulation]`, `[royale]`, and
`[hazard.*]` sections and the 32-marker map from the repository through the production loaders,
widens the lobby to the eight seats above, spawns and joins eight controllers, presses Start,
carries the match through the deployed five-second countdown, and times every `step` of 6,400
running ticks -- sixteen seconds, across every spawn tick of both deployed hazard kinds. On the
maintainer's Mac -- `VirtualApple @ 2.50GHz` under Rosetta inside the pinned container, GCC 13.3
Release, **not** the native runner -- nine samples measured a per-tick step of **6.9 µs mean,
6.6 µs median, 10.2 µs p99, 22 µs maximum** (median across samples) and 0.3 µs per snapshot, with
every sample hash-matching the untimed reference; the deployed comet is lethal and the field thins
from eight blobs to four across the window, which the output records. That is 36× inside the mean
budget and 98× inside the p99 budget on a host slower than `cole-ubuntu-pc`, so nothing in this
section changed on that evidence alone.

**Amended 2026-09-09 (plan Step 8, native measurement).** `./scripts/run-benchmarks-linux` on
`cole-ubuntu-pc` at commit `380b5c9` -- `AMD Ryzen 5 5600G`, 12 logical CPUs, governor
`powersave`, kernel 6.17, GCC 13.3 Release, inside the pinned container -- measured the same case
at **5.2 µs mean, 4.8 µs median, 7.8 µs p99, 15 µs maximum** per tick (median across nine samples;
the worst sample's p99 was 8.0 µs) and 0.34 µs per snapshot, with the same final snapshot hash as
the Mac run. That is 48× inside the mean budget and 128× inside the p99 budget, so **the decision
stands: `[lobbies] count=4` deployed and 8 compiled.** Eight rooms at that cost are about 42 µs of
every 2.5 ms quantum across two cores, under 1 % of one core, which leaves the room-count knob a
matter of how many lobbies a wall should show rather than of what the host can step. The trip-wire
for option B is unchanged: `maximum_lateness` above one quantum on a room whose own step is under
budget.

Option B is the fallback if a native measurement ever shows the N-thread scheduler jitter itself
costing more than the steps: the trip-wire is `maximum_lateness` above one quantum on a room whose
own step time is under budget.

### The lobby lifecycle

* **Creation.** All rooms exist from startup, built by the composition root exactly as the one
  runtime is built today, and `application.running` is logged once every room has committed its
  first tick.
* **Naming.** The server names rooms `1..N`; the client renders "Room 1" (amended 2026-09-09: the
  directory is "Rooms" and a room is "Room N"; `lobby_id` on the wire is unchanged). No
  user-supplied string.
* **Reaping.** None. An empty room in `lobby` is one idle thread waking 400 times a second to copy
  a small world; that is the cost of four rooms on a wall and it is paid whether or not anyone is
  looking.
* **When the last human leaves a match in progress.** The application already knows which
  controllers are sessions and which are bots (the directory's `controller_kind`). When a room's
  session count reaches zero while its phase is `countdown` or `running`, the control loop closes
  that room's bot sessions. Their `leave` commands destroy their bodies, the alive count falls to
  zero, `outcome` reports `drawn`, and the machine walks `ended -> lobby`, where the bot
  reconciliation reseats every declared NPC. No new command kind, no new phase, and the tick never
  learns the word "human". A room with nobody in it therefore returns to `lobby` within
  `restart_delay_seconds` of the last person leaving, instead of holding a match between two idle
  bots that nobody can join.
* **Joining a room whose match is running.** Admitted, as today: the spawn policy defers the
  joiner until the next `lobby`, and the joiner watches. The directory says `phase: running` and
  `phase_started_tick` so the browser can show how long the match has been going. A refusal was
  considered and rejected: watching until the next lobby is the better answer and needs no new
  error code.

**Amended 2026-09-09 (plan Steps 12 and 13).** The bullet above is not what was built, and the
reason is the welcome. A welcome names the session's first body, and a session with no seat never
gets one, so a joiner who "watches" would be a socket that never receives a frame. What is built
instead: a person's `join` takes the lowest empty seat in any phase and displaces a declared bot's
seat only in `lobby` or `countdown`; a session whose join could change nothing -- every seat held
past `countdown`, or held by people -- is closed `1013 lobby_full` before any welcome, by the same
rule the tick seats with (`first_joinable_seat`). The directory predicts that case from its census
and disables the room's Join with the reason, so the close answers the last-seat race and not the
ordinary path. Spectating stays in § "What is deliberately not built"; the abandonment rule in the
bullet before this one was built as written and is observed end to end by plan Step 16's browser
flow.

### Seats, people, and bots

Two server-issued command kinds join `spawn` and `despawn` in the closed registry, with
`CommandWireKind` `nullopt` so neither is ever accepted from a client, ranks placed after every
existing kind so no fixture's canonical order moves, and both classified as entity-lifecycle
commands in the mailbox so neither is ever evicted.

* **`join {controller, seat}`.** Submitted for itself by the session that wants a seat, at the
  same presentation slot and cadence it asks for a missing body, and by the reconciliation for the
  bot it is creating; `CommandSink::open_session` stays the identity-issuing call it is and takes no
  seat request (amended 2026-09-09 at plan Step 6, where the symmetry with `spawn` turned out to be
  the whole implementation). `seat` is absent for a
  person and names the declared seat for a bot the reconciliation is creating. Phase 0 seats a
  person in the lowest empty seat, else displaces the lowest-indexed NPC seat, else leaves the
  roster unchanged; it fills a bot into its declared `NpcSeat` only if that seat still declares its
  kind and has no controller. All three outcomes are visible in the next snapshot. A person whose
  `join` changed nothing sees a full roster without their id, which is a total condition, and the
  session closes with `lobby_full` (a new close reason on `1013`) rather than sitting outside the
  field forever.
* **`leave {controller}`.** Enqueued by `CommandSink::close_session`, from the mailbox the sink
  already fronts, before the directory entry is retired. Phase 0 clears any seat that controller
  holds and destroys every entity whose `Controllable` names it, pending or seated. Because it is a
  command in the same batch order as everything else, a spawn drained in an earlier tick is
  destroyed by the leave, and a spawn and a leave in one batch net to nothing. The session stops
  tracking `current_controlled_entity_` altogether: review finding 1 becomes unreachable by
  construction, not by a wider observation window.
* **`can_start` requires a bot that exists.** An `NpcSeat` counts as filled only once its
  `controller` is present, which is the state the wire already renders as "joining". A match can
  no longer start with nobody in it (review finding 4); the cost is that Start waits one
  reconciliation pass, about 50 ms, after the last NPC is declared.
* **The roster carries its ceiling.** `SeatRoster` is seeded with the map's spawn-marker count and
  `try_set_seat_count` refuses above it as a no-op, exactly as it refuses a shrink past an occupant.
* **Reconciliation** is the plan's Step 6, per room: after each control poll the application reads
  the room's latest snapshot and opens a bot session plus a `join {seat}` for every `NpcSeat` with
  no controller, and closes the session of any bot whose controller holds no seat. It is
  idempotent because the seat carries the controller.
* **Displacement** is the plan's Step 7 and falls out of `join`: the person takes the NPC's seat in
  the tick, the bot's controller is then in no seat, and the next reconciliation closes it.

### Protocol consequence: 2.4

One minor, republished together:

* Targets `GET /api/v2/lobbies` and `GET /api/v2/lobbies/<lobby_id>/session`; `/api/v2/session`
  defined as room 1.
* A `lobby-directory.schema.json` for the directory response, bounded at `kMaximumLobbyCount`
  entries, carried in the v2 envelope (amended 2026-09-09: shipped as
  `lobby-directory-message.schema.json` over `lobby-directory-data.schema.json`, bounded by
  `kLobbyDirectoryLimit`, the constant's name in `protocol_v2_constants.hpp`).
* `welcome.lobby_id` and `welcome.seat_count_maximum`, both required.
* Error codes `LOBBY.NOT_FOUND` (`404`), `LOBBY.FULL` (`409`, when a room's session count already
  equals its seat count), `LOBBY.UNAVAILABLE` (`503`, a room whose runtime has failed). Amended
  2026-09-09: `409` and `503` name the room in `details.lobby_id`; `404` carries no id, so no
  client-chosen byte reaches a response body; `503` is also the answer for a room that has not yet
  committed its first tick.
* Close reason `lobby_full` on `1013`, for a join the tick refused after admission (amended
  2026-09-09: sent before any welcome, see § "The lobby lifecycle").
* The `controller` seat kind becomes reachable; its schema is unchanged.
* `join` and `leave` in the simulation registry and the replay CSV, not on the wire.
* The fixes from the review folded in: the lethal row phase-gated to `running`, the empty roster for
  a mode without a lobby, and one `info` log line per accepted lobby command.

The "one minor ahead" rejection cases move from 2.4 to 2.5 on both sides, following the 2.1 to 2.3
procedure exactly.

### Failure modes

| Failure | Detected how | Reported where | Recovered how |
|---|---|---|---|
| A room's tick overruns its budget | The worker times each step and compares the commit to its deadline | `runtime.tick_overrun` at warning with `lobby_id`, tick, step µs, lateness µs; `healthy: false` in the directory while the last minute holds an overrun | Bounded catch-up then re-base; the room runs slow for a moment; other rooms are on other threads |
| A client stops reading | The 5 s write deadline and the per-session single pending snapshot, unchanged | `session.closed` with `close_code 1013 slow_consumer` and `lobby_id` | The socket closes, `leave` frees the seat and body; the simulation never waited |
| A slow room starving its neighbours | Overrun counters on every room rising together while one room's step time is high | The same `runtime.tick_overrun` lines, distinguishable by `step` versus `lateness` | Catch-up is bounded, so a pathological room cannot sprint; the operator lowers `[lobbies] count` or fixes the room's entity table; the trip-wire for option B is named above |
| A match wedged in a phase | `countdown` and `ended` are bounded by configuration; `running` with no humans is caught by the abandonment rule | `match.phase_changed` lines carry `lobby_id`, `from`, `to`, tick; the directory shows `phase` and `phase_started_tick` | Abandonment closes the bots and the match ends. The one remaining wedge is a `running` match whose humans idle inside the final circle; a time limit is a mode rule and is listed as not built |
| The process dies with matches in progress | Docker `--restart unless-stopped`; `application.failed` is the last line | Container restart count; the final structured events | Every room restarts in its configured initial state; every client reconnects to the directory; nothing is persisted, by design |
| A player disconnects mid-lobby | The close path | `session.closed`, `lobby_id` | `leave` frees the seat; if the room was in `countdown`, `can_start` goes false and the machine returns to `lobby` and clears the start request |
| A player disconnects mid-match | The close path | `session.closed`, `lobby_id` | `leave` destroys the body and frees the seat; the alive count falls, which may end the match exactly as an elimination would |
| Two clients race the last seat | Both `join`s land in one batch, ascending controller id | The loser's `session.closed` with `lobby_full` | First wins by batch order; the loser is closed with a reason and re-reads the directory |
| A room's runtime throws | `SimulationRuntimeState::kFailed` on that room only | `runtime.failed` at error with `lobby_id` and the exception; `healthy: false`, `LOBBY.UNAVAILABLE` on join | The control loop closes that room's sessions with `service_not_ready`, keeps the other rooms serving, and does **not** stop the process. Readiness stays true while room 1 is healthy. A failed room stays failed until restart: there is no in-process resurrection, because a runtime that threw has a world nobody trusts |
| Join to a full, unknown, or failed room | Router and directory | `409`, `404`, `503` with the v2 error envelope naming the room | Amended 2026-09-09: a browser never sees the envelope, because the WebSocket API exposes no declined-upgrade response, so the client reads the directory once, derives the refusal from the listing -- full, not serving, not listed -- and shows that sentence on the directory without retrying; the envelope's message reaches `curl` and the logs. `1013 lobby_full` is the one refusal that arrives in the server's own words |
| The pinned mirror is down | Not a runtime failure; see § "Observability" | `deploy_tailnet.error_code` and a CI step name | Build the toolchain image once and reuse it by digest |

### Isolation

Today a throw in the tick kills the process, and with one match that is correct: there is nothing
else to serve. With N rooms it is not acceptable, and the boundary is the runtime. A room that
fails is closed and reported; the process keeps serving. What stays process-fatal: a server
listener failure, a control-loop failure, and any startup validation failure, because each of those
is a fact about the whole process rather than about one match. Processes per room, and any
supervisor beyond Docker's restart policy, are deliberately not built.

### Backpressure and admission

| Resource | Per room | Global | What a player sees at the bound |
|---|---:|---:|---|
| Sessions | the room's seat count | 32, and 8 per client | `409 LOBBY.FULL` naming the room in `details.lobby_id` (amended 2026-09-09; the count is the room's live `seat_count`); `429` with `Retry-After` as today |
| Seats | 1 to the map's spawn markers (32 shipped), protocol cap 64 | | A seat-count control that stops at `seat_count_maximum`; a stale value is a no-op |
| Published entities | 1,024, checked at startup per room with `kMaximumLobbySeatCount` bots counted | | Startup rejection naming the room's table |
| Mailbox | 2,048 distinct (kind, identity) | | Unreachable at this scale; counted and logged if reached |
| Egress | | 16 MiB active, 64 MiB/s, unchanged; expected 32 sessions × ~120 KB/s ≈ 3.8 MB/s | A slow client loses intermediate frames, as today |
| Tick CPU | ≤ 250 µs mean per room | 2 CPUs | Slow motion in the one room, logged |
| Memory | a few MB per room | 1 GiB | Not reachable at N ≤ 8 |
| Directory polling | | the HTTP bucket, 20 burst and 2/s per client | 1 Hz per tab while the browser is visible; `429` if a tab polls faster |

**Per-client accounting is a prerequisite, not an option.** Behind `tailscale serve` every player
is one loopback principal today (`docs/operations/tailnet.md` § "Limits behind the proxy"): 8
sessions, 4 upgrades per 20 s, and 20 requests per 10 s **shared by everyone**. A directory that
four people poll at 1 Hz, and joins that re-upgrade, do not fit inside that. The deployment sets
`trusted_proxy_addresses=127.0.0.1` with the local-forgery residual protocol v2 already accepts for
a single-operator host, and the bounds are not widened. That is one configuration line and one
sentence in the runbook.

### Observability

Every log line from a room's runtime, its sessions, or its reconciliation carries `lobby_id`; the
field is added to `StructuredLogEvent` once. The signals a person needs at 2 am, in the order they
would look:

1. `GET /api/v2/lobbies` from the tailnet: which rooms exist, their phase, how long they have been
   in it, who is in them, and `healthy`. It is the status page, and it costs nothing to keep true
   because it reads published snapshots.
2. `match.phase_changed` per room, emitted by the control loop when a room's committed phase
   differs from the last one it saw: the whole story of a match in four lines.
3. `session.opened` and `session.closed` with `lobby_id` and a close code, and one `info` line per
   accepted lobby command with the request id, kind, and payload.
4. `runtime.tick_overrun`, `runtime.clock_rebased`, `runtime.command_dropped`, and
   `runtime.failed`, each with `lobby_id` and counters that only rise.
5. `controllers.pass_degraded` per room, unchanged in content.

**What today's outage asked for.** A pinned Ubuntu snapshot mirror served errors for an hour, the
toolchain image build failed inside `verify-linux`, and both CI and `deploy-tailnet` failed
without a line that named the mirror. Three things would have surfaced it as one cause in seconds:

* `deploy-tailnet` and the CI workflow run the toolchain image build as their own named step with
  its own error code, `TOOLCHAIN.IMAGE_BUILD_FAILED`, and the failing URL in the message.
* `deploy-tailnet` pre-flights the mirror with one `curl --head` before starting the ninety-minute
  release profile, and fails with `DEPLOY.MIRROR_UNREACHABLE` if it is down.
* The toolchain image is built once per pinned input set, tagged by the digest of
  `ci/linux/Dockerfile`, and reused when that tag exists on the host or in CI's cache. After the
  first build, neither CI nor deployment depends on the mirror at all.

None of those is a runtime signal, and none is in scope for the rooms work; they are recorded here
because the brief asked, and because the plan that follows will run the release profile at least
twice.

### What is deliberately not built

* **Dynamic room creation, naming, and reaping.** Four rooms from configuration is the whole need.
* **Matchmaking, invitations, passwords, a host role, a ready check.** The owner chose "anyone in
  the lobby" for a private tailnet; a room browser does not change that.
* **Cross-match identity, accounts, persistence, leaderboards, resume tokens.** Nothing survives a
  socket today and nothing needs to; a reconnect is a new join, per room.
* **One socket per player with a re-targeted stream.** A major version bought for a live lobby
  list; a polled page delivers the same at 1 Hz.
* **A process per room, an orchestrator, a second host, a load balancer.** One process, N threads,
  Docker's restart policy, one host, exactly as ADR 0001 records.
* **A metrics endpoint, a message bus, a tracing system.** JSON lines with `lobby_id`, `docker
  logs`, and one status route are the observability of a four-room server.
* **Spectators, chat, per-room maps and modes, a match time limit.** Each is a real feature with
  its own decisions; the time limit is the one worth doing next, because it is the last way a room
  can wedge.
* **Consolidating the N workers into one loop.** Not until a native measurement shows the scheduler
  costing more than the steps; the trip-wire is stated above.
* **Widening any rate bound to hide the loopback accounting collapse.** Per-client accounting is
  enabled instead.

### If only one group ever plays

`[lobbies] count=1` is this design, and it is the current server plus the `join` and `leave`
commands, the phase-gated lethal row, and a directory with one entry. The restructure buys three
things beyond that: two groups can play at once, a room's failure no longer stops the process, and
leaving a match is a real action rather than a disconnect. If none of the three is wanted, option C
above, the client lobby view alone, is the cheaper build; the review's findings 1 through 5 still
need fixing under it.

## Consequences

* **Positive:** Every v2 identity invariant stays literally true per socket, so the client's
  validation layer and its own-body rule do not change; the browser is a page and a fetch.
* **Positive:** Determinism and the physics baseline are untouched: a room is the existing
  simulation with the existing runtime, and `join`/`leave` are batch commands with ranks after
  every existing kind, so no accepted fixture's order moves.
* **Positive:** The review's three lifecycle defects are closed by the mechanism the feature needs
  anyway rather than by patches beside it.
* **Positive:** Failure isolation falls out of "one runtime per room" without a new abstraction.
* **Negative:** N threads on two CPUs, unmeasured today.
* **Mitigation:** The budget is a number, the measurement is the plan's first step, and the
  fallback is a configuration value, not a redesign. (Measured 2026-09-09 at plan Step 8 on the
  native host: 5.2 µs mean and 7.8 µs p99 per tick at the deployed roster, 48× and 128× inside the
  budget; § "The tick-loop decision".)
* **Negative:** Joining re-upgrades and browsing polls, both of which are per-client bounds that
  the loopback proxy collapses today.
* **Mitigation:** `trusted_proxy_addresses` is set, with the residual the protocol already accepts.
* **Negative:** A parametric route in a router that has only ever compared paths exactly.
* **Mitigation:** One segment, one closed grammar, matched exactly on either side of it, with
  tests for every string that is not a room.
* **Negative:** An idle room costs a thread waking at 400 Hz forever.
* **Mitigation:** Measured, and bounded by `[lobbies] count`.
* **Operational:** `deploy/ubuntu-pc/blob-royale.cfg` gains `[lobbies] count=4`, moves
  `lobby_seat_count` under `[match]`, and sets `trusted_proxy_addresses=127.0.0.1`. The runbook's
  "Limits behind the proxy" section is rewritten for per-client accounting.
* **Reversibility:** `[lobbies] count=1` and the `/api/v2/session` alias are the single-match
  server; deleting the directory route and the parametric route returns v2 to 2.3's surface.

## Related

* [`0001-linux-runtime-contract.md`](0001-linux-runtime-contract.md) — the host, the container's
  `--cpus 2 --memory 1g`, and the tailnet trust boundary this design keeps.
* [`0002-simulation-architecture.md`](0002-simulation-architecture.md) — one writer per
  simulation, the write-only sink, and the composition root that now owns N runtimes.
* [`0003-deterministic-simulation-contract.md`](0003-deterministic-simulation-contract.md) — the
  fixed quantum the tick budget is measured against.
* [`0004-gameplay-architecture.md`](0004-gameplay-architecture.md) — the command registry the two
  server-issued kinds join.
* [`0005-royale-mode.md`](0005-royale-mode.md) — the lifecycle machine and the elimination rule the
  phase gate restores.
* [`../protocol/v2.md`](../protocol/v2.md) — the contract this amends at 2.4.
* [`../reviews/2026-09-08-lobby-and-hazard-review.md`](../reviews/2026-09-08-lobby-and-hazard-review.md)
  — the findings this design closes.
* [`../../.claude/prompts/2026-09-08-multi-lobby-server-architecture.md`](../../.claude/prompts/2026-09-08-multi-lobby-server-architecture.md)
  — the brief.

## Amendment — historical royale benchmark input, 2026-09-10

The `royale_deployed_roster` case retains its identity and measured algorithms, but its name now
denotes the historical royale workload rather than the current deployment selection. The
deployment switched to `king_of_the_hill`/`hills-960x640` in `a55dca4`; coupling this fixed royale
case to that live configuration made the required benchmark command fail before measuring it.

`benchmarks/fixtures/royale-roster.cfg` preserves
`a9e0104ca25724ac4660a4fc9031620b8a852d40:deploy/ubuntu-pc/blob-royale.cfg` byte for byte after its
provenance comment. This commit identifies the configuration only: the production map loader
still reads `arena-960x640` from the current repository's `maps/`. The runner supplies the explicit
`--royale-reference-config` input; the production loaders, drag 2, seed 1, royale tuning,
comet/boulder hazards, four-to-eight seat widening, 6,400 running ticks, timing boundaries,
correctness hashes, and advisory budget reporting remain the same. No deployment file is changed,
no case is dropped, and no fallback overrides the selected mode. JSON labels this provenance
`historical_reference`, not a measurement of today's deployed hill room.

The historical native evidence above is retained as historical evidence; this correction neither
regenerates it nor certifies current native performance. The new continuous-motion prototype
cases are separately labelled unwired/advisory, and neither their results nor a Mac-hosted
emulated run satisfies the dynamic-arena plan's Step 5 native-evidence/human-review gate.

### Historical input schema migration, 2026-09-10 (dynamic-arena plan Step 6)

The strict configuration migration replaces the historical fixture's unused race-width key
with its named `road` binding. The fixture is now a schema-only derivation of the source above,
not a byte-for-byte copy. Its measured simulation, royale, and hazard values remain unchanged;
the benchmark JSON and README identify the derivation. Historical review/baseline artifacts
are retained as evidence of their original inputs, not rewritten to imply the migrated file
was measured then. This does not certify native performance. ADR 0008's later owner
clarification defers capacity selection and removes native evidence from the present human
Step 5 design gate, while retaining it for Step 24 performance/release claims.
