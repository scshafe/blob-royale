# Prompt: review the lobby work, then design the connection and lobby architecture

Paste this whole file as your opening message. It is written to be self-contained: it names the
files and the facts you need so you can start from evidence rather than from a tour.

---

## Who you are and what I want

You are working in `/Users/coleshaffer/Projects/blob-royale`, a C++20 authoritative game server with
a React client, deployed to a single private host on a tailnet. Read `CLAUDE.md`, the ADRs in
`docs/architecture/` (0001–0005), and `docs/protocol/v2.md` before proposing anything.

I want three things, in this order, and I want you to **stop after the first two and show me your
work before you write feature code**.

1. **An adversarial review** of the lobby and hazard work landed on 2026-09-07 and 2026-09-08.
2. **A design** for connecting to the server, browsing and joining a lobby, and leaving it — with
   the connection and process architecture treated as the real problem, because it is.
3. **An implementation plan** in the format of `.claude/plans/`, which I will review before you
   execute it.

---

## Part 1 — Review what is already there

Recent work you are reviewing, newest first:

| Commit | What it claims |
|---|---|
| `ecc8e95` | Lobby as a seat roster; four client commands; protocol 2.3; two pre-existing bug fixes |
| `e2083dc` | Elimination grace published so the client counts down; protocol 2.2 |
| `d41e0c0` | Hazards in the deployed arena, with a self-proving fixture test |
| `3e8d732` | Per-body `drag_scale`, because hazards stalled under the deployed drag |
| `f3628ba` | Hazard component, lethal contact rule, spawner, lifetime expiry; protocol 2.1 |
| `042c5c6` | Kind-name grammar unified after existing three times |
| `d9ea038` | Per-body mass, restitution, and contact size |

Read `.claude/plans/2026-09-07-hazards-and-variable-physics.md` and
`.claude/plans/2026-09-08-operable-lobby-with-seats.md` — both carry execution notes recording what
was decided and why, including several places the plan was wrong and was amended.

**Be adversarial.** Every agent that worked on this feature found a real defect by being skeptical
of its brief, including two pre-existing bugs nobody was looking for. Assume there are more. In
particular, look hard at:

- **The seat roster's ownership.** It lives in engine-owned `MatchState` (`src/simulation/seat_roster.hpp`),
  argued on the grounds that mode state is for what the engine never reads. Is that right? It makes
  `sandbox` carry a roster it never uses and adds a `std::vector` copied into the working world every
  tick at 400 Hz — a new per-tick heap allocation on the deterministic path.
- **The start-request lifecycle.** It clears on *arrival into* `lobby`. Convince yourself that a
  match cannot be started twice, cannot start with nobody in it, and cannot be wedged.
- **The command authority model.** Anyone may resize the lobby, seat an NPC, and press Start. That
  was my explicit choice for a private tailnet. Tell me exactly what it costs the moment this is
  reachable by anyone I have not met.
- **The refusal taxonomy.** Vocabulary and bound violations close the connection; state-dependent
  refusals are silent tick-side no-ops. The line drawn was "could the client have known?" Is that
  line in the right place, and is it applied consistently?
- **`start_match` is classified non-lifecycle in the mailbox**, so a full mailbox can drop it. The
  argument was that a lost Start is visible and re-pressable. Check it.
- **Known-incomplete work**, so you do not report it as a defect: nothing yet creates a bot when a
  seat declares one (plan Step 6), a human does not yet displace a bot in a full lobby (Step 7), and
  there is no lobby UI (Step 8). `frontend-react/e2e/BlobRoyaleLobbyDriver.ts` is deliberate
  temporary scaffolding that Step 8 should delete.

Report findings as: the defect, the concrete failure it produces, the file and line, and your
confidence. Do not report style preferences as defects. If you think something is right, say so —
I would rather have a short honest list than a long padded one.

---

## Part 2 — The feature, and why it is not a UI change

I want a player to: **connect to the server, see what lobbies exist, join one, play, and leave it**
— returning to the lobby browser rather than being disconnected.

Here is the part that makes this architectural rather than cosmetic, verified in the tree today:

- **The server owns exactly one simulation.** `BlobRoyaleApplication` holds one
  `runtime::SimulationRuntime` (`src/application/blob_royale_application.hpp:87`), driven by one
  fixed-tick loop.
- **Every session is bound to that one match at upgrade time.** `MatchSessionContext`
  (`src/server/match_session_context.hpp`) carries a single write-only `CommandSink&` and a single
  read-only `SnapshotPublication&`, and `/api/v2/session` hands both to every admitted socket.
- **The welcome frame names the match.** It carries `entity_id`, `controller_id`, `mode`, `map` and
  `accepted_command_kinds` — facts that only make sense once a session already belongs to a match
  (`docs/protocol/schema/v2/welcome-data.schema.json`).
- **Concurrency is bounded at 32 sockets** (`server_limits.hpp`), entities at 4,096, and published
  entities at 1,024 — all currently *per server*, which is the same thing as *per match*.

So "choose a lobby" means: N concurrent simulations, a routing layer from session to simulation, a
session lifecycle that outlives any one match, and a protocol where the welcome no longer implies a
match. That is a restructure, and it deserves an ADR, not a patch.

**Decide and justify, do not assume:**

- Does a session join a lobby by opening a *new* socket per lobby, or does one socket carry a
  `join_lobby` / `leave_lobby` command and re-target its snapshot stream? The second keeps one
  connection per player and makes the browser cheap; it also means the snapshot stream changes
  identity mid-connection, which every client assumption about `entity_id` and sequence numbers has
  to survive.
- Is the lobby browser itself a simulation, a plain query endpoint, or a subscription? A polling
  `GET /api/v2/lobbies` is the cheapest honest answer and may be the right one.
- **One tick loop stepping N worlds, or N loops?** This is the decision the rest hangs off. ADR 0002
  fixes single-writer ownership and ADR 0003 forbids any parallelism that changes a committed
  result. A thread per match is simple to reason about and multiplies the scheduler's jitter
  surface; one loop stepping N worlds keeps a single clock and makes one slow match everyone's
  problem. Say which, and say what happens when N worlds cannot be stepped inside one tick budget.
- What is a lobby's lifecycle? Who creates one, who names it, when is an empty one reaped, and what
  happens to a match in progress when the last human leaves and only bots remain?

---

## Part 3 — "Enterprise grade", defined concretely or not at all

I asked for something professional, scalable, and robust. I do not want those words back. I want
them turned into decisions with numbers and failure modes attached. Specifically:

- **State the target.** How many concurrent lobbies, how many players each, on what hardware? The
  deployment is one host: an AMD Ryzen 5 5600G, 12 CPUs, 125 GiB RAM, containerised with
  `--memory 1g --cpus 2` (ADR 0001). If the answer is "8 lobbies of 8 on 2 CPUs", say that and design
  for it. Do not design for a scale nobody asked for — over-provisioned architecture is its own
  failure.
- **Name the failure modes and what happens in each.** A lobby whose tick overruns its budget. A
  client that stops reading and lets its socket buffer grow. A slow lobby starving its neighbours.
  A match wedged in a phase. The process dying with matches in progress. A player disconnecting
  mid-match versus mid-lobby. Two clients racing the same seat. For each: detected how, reported
  where, recovered how.
- **Isolation.** Does one lobby's failure take the others down? Today a throw in the tick kills the
  process. Is that still acceptable at N lobbies, and if not, what is the boundary?
- **Backpressure and admission.** 32 sockets and 1,024 published entities are per-server today. What
  are they per-lobby, what is the global ceiling, and what does a player see when it is reached?
  A refusal with a reason beats a queue nobody told them about.
- **Observability.** The tree already emits structured JSON logs with `trace_id`
  (`src/observability/`). What are the lobby-level signals a person needs at 2am — and what would
  you have wanted during today's outage, when a pinned Ubuntu mirror took out CI and deployment
  simultaneously and nothing surfaced that as one cause?
- **What you would deliberately NOT build.** This runs on a private tailnet for a handful of
  friends. Tell me what enterprise machinery is genuinely wrong here — I would rather be talked out
  of something than sold it.

Where the honest answer is "the current design is fine and here is the one thing to change", say
that. A restructure that buys nothing is worse than the single-match server we have.

---

## Constraints that bind, regardless of design

These are load-bearing in this codebase; violating them is a defect, not a trade-off.

- **Determinism is the product.** Same map, mode config, seed and command log must reproduce a match
  exactly. Every draw comes from `GameWorld::random()`; ascending `EntityId` iteration;
  `-ffp-contract=off`. Any concurrency must commit the same result as the serial reference.
- **The accepted physics baseline does not move.** `AcceptedBaselineTick`,
  `kMigratedPairFixtureHorizon` and `bodies_are_bit_identical` keep their exact values, and
  `git diff --stat -- maps/` stays empty.
- **Fail closed.** Every configuration value is validated at startup and a bad one stops the
  process. No silent fallbacks, no degraded successes.
- **One capability, one implementation.** Duplication has bitten this tree three times in two days
  (the kind-name grammar existed three times, an entity-budget constant four). Unify on the second
  instance, not the third.
- **A new component kind, command kind, or mode-state member is a protocol minor**, and the schema
  set republishes with the version const bumped. The wire is at **2.3**.
- **Verify on both lanes.** `./scripts/verify-focused '<filter>'` on `linux-gcc-debug` *and*
  `linux-clang-asan-ubsan`; GCC-only verification has turned CI red five times running here.
- **The release profile takes about ninety minutes** and refuses to publish anything it has not
  certified, so a failed deploy leaves the previous arena serving.

---

## What I want back, and in what order

**First**, the review from Part 1. Findings ranked by severity, each with its concrete failure.
Then stop.

**Second**, a design note for Parts 2 and 3 — prose, not code. It should name the routing model, the
tick-loop decision, the lobby lifecycle, the protocol consequence and its version, the numbers from
Part 3, and an explicit list of what you chose not to build. Where a decision could reasonably go
two ways, give both and recommend one. Then stop.

**Third**, once I have agreed the design, a plan file in `.claude/plans/` following the format of the
two already there: atomic steps, each with a `Verify:` command that proves it, and notes recording
*why* rather than *what*.

Do not start implementing until I have agreed the design. The most expensive thing you can do here
is build the right feature on the wrong architecture.
