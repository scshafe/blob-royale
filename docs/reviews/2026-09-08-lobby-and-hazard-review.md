<!-- canonical: lobby_and_hazard_review -- independent review of the 2026-09-07 and 2026-09-08 hazard and lobby commits -->

# Lobby and hazard review, 2026-09-08

Independent review of `d9ea038`, `042c5c6`, `f3628ba`, `3e8d732`, `d41e0c0`, `e2083dc`, and
`ecc8e95` against the owner's brief (`.claude/prompts/2026-09-08-multi-lobby-server-architecture.md`),
ADRs 0002 to 0005, `docs/protocol/v2.md`, and the two plans that carry the execution notes.

**Headline.** The seat roster is in the right place and the start request cannot fire twice, but
the lobby commit re-opened the leak it reports closing: a session that goes away while its spawn is
still in the mailbox leaves a body nobody owns. The hazard commits broke an invariant ADR 0005
states in as many words: eliminations now happen in every phase, and at the deployed table a comet
can kill the winner during the victory screen. Four more findings follow, then what the review
confirmed. Nothing here is a style preference.

Every finding names the defect, the concrete failure it produces, the file and line, and a
confidence. Three were reproduced with tests written for this review (appendix); they were run on
`linux-gcc-debug` and `linux-clang-asan-ubsan`, the session race seven times out of seven, and were then
removed from the tree so the working copy stays clean. Line numbers are as of `c05b770`.

## Findings that force rework if deferred

| # | Finding | Concrete failure | Where | Confidence |
|---|---|---|---|---|
| 1 | **A session that closes while its spawn is in flight orphans its entity.** The session learns what it owns only at a presentation slot, submits its spawn at one, and `leave_match` despawns only what it last observed. A close that lands between the slot that submitted the spawn and the next slot retires the controller while the spawn is still queued; the next tick creates the entity anyway and, in `lobby` or `countdown`, seats it. Nothing ever destroys it. Every close that happens before the welcome is in this window, which is up to one presentation period: 50 ms at the deployed 20 Hz. | A blob published as `controller_kind: "unknown"`, named `player-<entity_id>`, sitting on a ring point, counted alive, steerable by nobody. It leaves the world only by zone or hazard elimination or the next `ended -> lobby` wipe; in a two-player royale the survivor waits about a minute for the zone to reach it, plus the grace. This is the class of leak `ecc8e95` reports fixing; the fix moved it from "never seated" to "not yet observed". | `src/server/session_websocket_session.cpp:428-444` (ownership observed only at slots), `:446-461` (spawn submitted at a slot), `:731-757` (`leave_match` despawns only what was observed), `src/protocol/components/controllable_component_encoding.hpp:44-46` (what the orphan looks like on the wire) | High. Reproduced deterministically with a 1 Hz presentation cadence, appendix A. |
| 2 | **A lethal hazard eliminates in `ended`, `lobby`, and `countdown`.** The spawner runs only while `running`, but a hazard lives its whole `Lifetime`, the lethal row's predicates read no phase, and `placement_recorder` step 2 destroys and ranks every `EliminationEvent` in every phase. | At the deployed table a comet lives up to 4.6 s on a 6 s interval, so one is in flight at roughly half to three quarters of match ends. If it crosses the winner during the 8 s `ended`, the winner is destroyed and appended to the placement list at placement 1 while `outcome` still names it, and the victory overlay loses the blob it is highlighting. A lethal kind slower than about 100 wu/s outlives the restart delay and kills seated players in the next `lobby` and `countdown`, appending to the previous match's ranking. ADR 0005 § "Elimination and placement" now states two false things: "eliminations occur only during `running`" and "the winner receives no placement entry". | `src/gameplay/shared/lethal_hazard_contact_rule.cpp:15-21, 41-44`, `src/gameplay/royale/placement_recorder_system.cpp:101-127`, `src/gameplay/shared/hazard_spawn_system.cpp:33-35`, `src/gameplay/shared/lifetime_expiry_system.cpp:23-62`, `docs/architecture/0005-royale-mode.md:454, 476` | High. Reproduced in all three phases, appendix B. |
| 3 | **`set_seat_count` escapes the map bound that `validate_map` guards.** `RoyaleMode::validate_map` rejects a configured `lobby_seat_count` above the map's spawn markers because a full lobby could not all be seated. The command is bounded only by `kMaximumLobbySeatCount` (64) at the decoder, the sink, the batch, and the roster, so any client grows the shipped 32-marker lobby to 64 seats in one frame. By the tree's own line, "could the client have known?", this must be a tick no-op: the marker count is published nowhere. | The invariant the startup check states is violated at runtime: a 64-seat lobby on a 32-point map, a bound nothing publishes and no client can explain. With plan Step 6 that is 64 bot sessions competing for 32 ring points every tick, and a Start that fires with the losers still pending outside the arena. | `src/simulation/seat_roster.hpp:275-289`, `src/simulation/game_simulation.cpp:131-137`, `src/gameplay/royale/royale_mode.cpp:73`, `src/protocol/command_decoding.cpp:117-125`, `src/simulation/input_batch.cpp:77-99` | High. Reproduced, appendix C. |

## Findings to fix with the lobby work

| # | Finding | Concrete failure | Where | Confidence |
|---|---|---|---|---|
| 4 | **A match can start with nobody in it.** `can_start` is "every seat filled and a start requested"; a declared NPC counts as filled before its bot exists; and nothing in `src/` writes a `ControllerSeat` (zero writers, grepped). Plan Step 6 does not close this: a bot whose creation fails, because the host or the directory is full, still counts as seated. | Four `seat_npc` and one `start_match` from any client walk `lobby -> countdown -> running` with an alive count of zero, which `outcome` reports as `drawn`, then `ended -> lobby`: a recorded draw between nobody, every 13 s, for as long as someone presses. The owner's question "convince yourself a match cannot start with nobody in it" is answered no. | `src/gameplay/royale/royale_objective.hpp:60-84`, `src/simulation/seat_roster.hpp:152-158`, `src/simulation/game_simulation.cpp:152-166` | High, by construction. The migrated fixtures state the same fact in their own comments (`tests/fixtures/replays/royale-elimination-timing/commands.csv`). |
| 5 | **`sandbox` publishes a four-seat roster while advertising no lobby.** The composition root seeds `SeatRoster::of_size([royale] lobby_seat_count)` "whatever `[match] mode` names". Protocol v2 promises that a mode without a lobby publishes only `set_thrust` and that "a world that declared no lobby publishes an empty array". | A sandbox client validating 2.3 receives four `empty` seats it has no command to operate, and the decoder guidance in the protocol ("empty means no lobby was declared") is false for the shipped sandbox. A Step 8 seat grid keyed on `seats.length > 0` draws an inert lobby. | `src/application/blob_royale_application.cpp:287-301`, `docs/protocol/v2.md:285-287, 326-329` | High. Read, not run; the seeding is unconditional. |
| 6 | **Lobby commands leave no trace.** No log line records who resized, cleared, seated, or started. The tick logs nothing by contract and the session discards the submit result. | Under "anyone may operate the lobby" this is the whole cost the moment a stranger reaches the listener: a griefer who starts every match early or resizes every lobby cannot be identified even after the fact, and the per-principal caps do not bound a local process that forges its forwarded address. One `info` line per accepted lobby command at the session, carrying the request id, the kind, and the payload, is the entire fix. | `src/server/session_websocket_session.cpp:308-309`, `src/simulation/game_simulation.cpp:96-107` | High. |

## Smaller findings

| # | Finding | Where |
|---|---|---|
| 7 | The startup entity budget counts 32 sessions plus the configured bot roster, not the up-to-64 bots plan Step 6 will create from declared seats. On the shipped map the worst case is about 102 of 1,024 published entities, so nothing breaks; the arithmetic should include `kMaximumLobbySeatCount` when Step 6 lands so the bound stays a proof. | `src/application/match_startup_validation.cpp:55-68` |
| 8 | A slider that emits `set_seat_count` per pixel exhausts the 30-token command burst and closes the socket with `command_rate_exceeded`. Not a server defect; Step 8 must debounce, and the note belongs in that step. | `src/server/server_limits.hpp:46-47` |
| 9 | `countdown -> lobby` is unreachable today: lobby commands are refused outside `lobby` and no runtime path removes a `ControllerSeat`. Not a defect; it becomes reachable with Step 7, and `match_lifecycle_tests.cpp:306` already covers the transition. Recorded so nobody reads the dead branch as a bug. | `src/simulation/game_simulation.cpp:127-129` |

## Pre-existing behaviour found while reading, relevant to the multi-lobby design

| # | Observation | Where |
|---|---|---|
| P1 | **Tick catch-up is unbounded.** After a stall the worker advances `next_tick` by one quantum and ticks back-to-back until it has caught up. Under the deployed CFS quota (`--cpus 2`) a throttled 100 ms becomes 40 consecutive ticks, which is the burst shape that gets throttled again. With one runtime it is a hiccup; with N runtimes it is N synchronized bursts. | `src/runtime/simulation_runtime.cpp:250-253` |
| P2 | **The tailnet deployment accounts every player as one loopback principal**: 8 sessions, 4 upgrades per 20 s, and 20 HTTP requests per 10 s are shared by everyone. Any lobby browser that polls, and any join that re-upgrades, needs `trusted_proxy_addresses` set first, with the local-forgery residual the protocol already accepts. | `docs/operations/tailnet.md:141-146`, `docs/protocol/v2.md:966-969` |
| P3 | **A runtime failure stops the process.** The control loop treats `SimulationRuntimeState::kFailed` as a terminal wake reason. Correct with one match; with N it is the isolation boundary the design has to move. | `src/application/blob_royale_application.cpp:231-233` |

## Release hazards that are not defects

- **HEAD is not deployable as a playable game.** No writer of `ControllerSeat` exists and the client has no lobby view, so `scripts/deploy-tailnet` at `c05b770` would replace the auto-starting `2b7ecd2` arena with a lobby that no browser can fill or start. The plan orders Step 10 last for this reason; it is recorded here because `ecc8e95` already changed `deploy/ubuntu-pc/blob-royale.cfg` to `lobby_seat_count=4`, so the hazard is armed the moment anyone runs the script.
- `frontend-react/e2e/BlobRoyaleLobbyDriver.ts` is the only thing in the tree that can start a match. It stays load-bearing until Step 8 deletes it.

## What the review confirmed, question by question

**The seat roster's ownership is right.** The engine writes it in two places: phase 0 applies the four lobby commands (`src/simulation/game_simulation.cpp:118-172`) and the lifecycle system clears the start request on every arrival in `lobby` (`src/simulation/match_lifecycle_system.cpp:42-44`). A roster in royale's mode-state arm would need the engine's lifecycle to reach into a mode arm, which ADR 0004 § "Game modes and the match lifecycle" forbids, and would make the second competitive mode rebuild the roster and its wire form. The per-tick cost is real but not new in kind: `GameSimulation::step` copies the whole world every tick (`src/simulation/game_simulation.cpp:648`), including every component store and royale's placements vector, and four seats add roughly 350 bytes to a copy that already moves kilobytes. The `sandbox` roster is finding 5, a composition-root bug, not an ownership problem. One thing to change with the lobby work: `lobby_seat_count` is a fact about the match, not a royale balance number, and reads as a `[match]` key.

**The start-request lifecycle.** A match cannot start twice: the request clears on every committed transition into `lobby` and `start_match` is refused outside it, so a stale press cannot arm the next match from inside this one. It cannot wedge in `countdown` or `ended`, both of which are bounded by configured tick counts. It can start with nobody in it: finding 4. Clearing on arrival rather than departure is correct for the reason the code gives; clearing on departure would bounce every countdown.

**The command authority model.** Among invited friends the accepted cost is a mis-click. The moment a stranger can reach the listener the cost is: every lobby resizable to 1 or 64 and every bot clearable at 20 commands per second per session, 8 sessions per principal, unlimited principals if the forwarded address is forged from the host; with Step 6, 64 bot sessions per lobby on demand; and no attribution at all, because nothing logs a lobby command (finding 6). The control the code names, an authority attached to the sender's `ControllerId` and refused at the sink, is the right one and nothing in the tick is load-bearing for it. The missing piece today is the log line.

**The refusal taxonomy.** "Could the client have known?" is the right line and it is drawn consistently for everything the welcome publishes: an unregistered kind, an unpublished `npc_kind`, a seat index or count outside the schema bound all close the connection, and every disagreement with committed roster state is a silent tick no-op. It is misapplied once, in finding 3, where a value the client could not have known is accepted rather than ignored. The one rough edge that is not a misapplication is finding 8.

**`start_match` in the mailbox.** The classification is correct and the argument holds: a lost despawn is unrecoverable by anyone, a lost Start is visible because `start_requested` is published on every frame and pressing again fixes it. It is also moot at this scale. The mailbox holds 2,048 distinct (kind, identity) slots; 32 sessions times five client kinds plus a bot roster is about 200, and the eviction path needs more than 2,048 distinct commanded identities inside one 2.5 ms drain. The caveat is that the session discards the submit result, so a drop reaches a person only through the snapshot and the composition root's counter log.

**The hazard work.** `require_match_fits_snapshot_bound` reaches the standing-hazard count through the one implementation in `hazard_crossing.hpp`, `drag_scale` is bounded below at zero so a negative scale cannot grow velocities without bound (`src/simulation/physics_body.cpp:137-141`), the `<= 1` expiry guard is right, and the lethal row's precedence above the impulse rows is pinned by test. The fixture migration's "no horizon moved" argument is sound: the threshold was crossed during a tick's spawn and observed at the same tick's `kLifecycle`, and a Start applied at phase 0 of the same tick is observed at the same place. The `royale-spawn-order` derivation is correct. I did not re-derive the horizons numerically; the suite passes on both lanes.

## Documentation corrections owed

- `docs/architecture/0005-royale-mode.md:454` and `:476` become true again only with the fix for finding 2, and the amendment should say the lethal row is phase-gated.
- `docs/protocol/v2.md:326-329` is false for the shipped `sandbox` until finding 5 is fixed.
- `docs/operations/tailnet.md:65` and `scripts/reconfigure-tailnet:10` still say "lobby minimum".
- `frontend-react/src/features/simulation/sessionSelectors.ts:306` still says a match starts once enough blobs have joined; the client README already admits it and assigns it to Step 8.

## Appendix: reproduction tests

The tests below were appended to `tests/unit/gameplay/royale/royale_mode_tests.cpp` (A and C) and
`tests/unit/server/session_websocket_session_tests.cpp` (B), run with
`./scripts/verify-focused 'REVIEW'` on `linux-gcc-debug` and on `linux-clang-asan-ubsan`, and
reverted. They pass, which is the defect. Each is written so the fix turns it into a regression
test by inverting the final assertions.

### A. The session orphan (finding 1)

Appended to `tests/unit/server/session_websocket_session_tests.cpp`, after the existing harness.

```cpp
#include "command_kind_mask.hpp"
#include "command_mailbox.hpp"
#include "components/controllable_component.hpp"
#include "match_session_context.hpp"
#include "server_config.hpp"
#include "simulation_runtime.hpp"
#include "world_snapshot.hpp"

#include <thread>

namespace {

// The same harness shape as `SessionHarness`, except that the publication the server reads is a
// **live** runtime's and that runtime is ticking, so a spawn the session submits is actually
// applied. Presentation runs at one slot per second, which turns the window between "spawn
// submitted" and "body observed" from ~33 ms into ~1 s so the test can close inside it reliably.
class LiveRuntimeHarness final {
public:
  LiveRuntimeHarness()
      : simulation_runtime_(fixture::game_simulation()),
        acceptor_(server_io_context_, {boost::asio::ip::address_v4::loopback(), 0}),
        server_context_(std::make_shared<server::ServerExecutionContext>(
            server_io_context_, server_config(acceptor_.local_endpoint().port()),
            simulation_runtime_.snapshot_publication(), match_context(), log_capture_.logger)),
        client_socket_(client_io_context_), server_socket_(server_io_context_) {
    simulation_runtime_.start();
    client_socket_.connect(acceptor_.local_endpoint());
    acceptor_.accept(server_socket_);
  }
  ~LiveRuntimeHarness() { simulation_runtime_.stop(); }
  // make_session and request are copies of SessionHarness's; omitted here.

  [[nodiscard]] boost::asio::io_context& server_io_context() noexcept { return server_io_context_; }
  [[nodiscard]] Tcp::socket& client_socket() noexcept { return client_socket_; }
  [[nodiscard]] runtime::SimulationRuntime& simulation_runtime() noexcept {
    return simulation_runtime_;
  }
  [[nodiscard]] const fixture::LogCapture& log_capture() const noexcept { return log_capture_; }

private:
  [[nodiscard]] server::MatchSessionContext match_context() {
    return server::MatchSessionContext::create(
        simulation_runtime_.command_sink(), simulation_runtime_.controller_directory(),
        std::string{fixture::kFixtureMapName}, simulation::CommandKindMask::all(),
        std::vector<std::string>{"wanderer"});
  }
  [[nodiscard]] static server::ServerConfig server_config(const std::uint16_t port) {
    return server::ServerConfig::create(
        "127.0.0.1", port, 1, fixture::kWorldWidth, fixture::kWorldHeight, fixture::kPlayerRadius,
        {std::string{"127.0.0.1:"}.append(std::to_string(port))}, {}, {});
  }

  boost::asio::io_context server_io_context_{1};
  boost::asio::io_context client_io_context_{1};
  runtime::SimulationRuntime simulation_runtime_;
  fixture::LogCapture log_capture_;
  Tcp::acceptor acceptor_;
  std::shared_ptr<server::ServerExecutionContext> server_context_;
  Tcp::socket client_socket_;
  Tcp::socket server_socket_;
};

template <typename Predicate>
void run_until_within(boost::asio::io_context& io_context,
                      const std::chrono::steady_clock::duration budget, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  while (std::chrono::steady_clock::now() < deadline) {
    io_context.restart();
    static_cast<void>(io_context.poll());
    if (predicate()) {
      return;
    }
    std::this_thread::sleep_for(1ms);
  }
}

[[nodiscard]] bool world_holds_controller(const simulation::WorldSnapshot& snapshot,
                                          const simulation::ControllerId controller) {
  for (const auto& entry : snapshot.components<simulation::Controllable>()) {
    if (entry.value.controller_id == controller) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("REVIEW: a session closed while its spawn is in flight leaves its entity in the world",
          "[review][server][v2][session][ownership]") {
  constexpr std::string_view kRequestId = "review.session.spawn-in-flight";
  LiveRuntimeHarness harness;
  const simulation::ControllerId issued = simulation::ControllerId::create(
      harness.simulation_runtime().command_sink().next_controller_id());
  const std::shared_ptr<server::SessionWebSocketSession> session =
      harness.make_session(kRequestId, direct_identity());
  SessionClient client{std::move(harness.client_socket())};

  session->run(harness.request(kRequestId));
  run_until_within(harness.server_io_context(), 2s, [&harness] {
    return harness.simulation_runtime().controller_directory().size() == 1;
  });
  REQUIRE(harness.simulation_runtime().controller_directory().contains(issued));

  // The first presentation slot (one second after the open) observes no body and submits the
  // spawn. Wait for exactly that submission.
  run_until_within(harness.server_io_context(), 3s, [&harness] {
    return harness.simulation_runtime().command_mailbox_statistics().submitted_command_count >= 1;
  });
  REQUIRE(harness.simulation_runtime().command_mailbox_statistics().submitted_command_count == 1);

  // The client goes away before the next slot (a second later) could observe the body the spawn
  // is about to create.
  client.stream().next_layer().close();
  run_until_within(harness.server_io_context(), 2s,
                   [&harness] { return count_events(harness, "session.closed") == 1; });
  REQUIRE(count_events(harness, "session.closed") == 1);
  REQUIRE_FALSE(harness.simulation_runtime().controller_directory().contains(issued));

  // Let the runtime commit a few more ticks, then look at what the world holds.
  std::this_thread::sleep_for(100ms);
  const std::shared_ptr<const simulation::WorldSnapshot> latest =
      harness.simulation_runtime().snapshot_publication().latest();
  const runtime::CommandMailbox::Statistics statistics =
      harness.simulation_runtime().command_mailbox_statistics();

  // The defect: the retired controller's entity is committed in the world and nothing ever
  // submitted a despawn for it -- the only command this session ever submitted was the spawn.
  CHECK(world_holds_controller(*latest, issued));
  CHECK(statistics.submitted_command_count == 1);
}
```

### B. Eliminations outside `running` (finding 2)

Appended to `tests/unit/gameplay/royale/royale_mode_tests.cpp`.

```cpp
// A royale world frozen in `phase`, holding one seated player and one lethal hazard that is
// already overlapping it and moving toward it, so the very next kernel contact phase fires the
// lethal row whatever the phase is.
[[nodiscard]] simulation::GameSimulation
review_world_with_lethal_hazard(const simulation::MatchPhase phase,
                                const simulation::MatchPhase previous_phase) {
  const simulation::SimulationConfig configuration = testing::gameplay_configuration();
  const simulation::MapDefinition map = testing::gameplay_map(4);
  simulation::GameWorld world = simulation::GameWorld::create(configuration, map, 0);

  const simulation::Vector2 zero = review_point(0.0, 0.0);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      review_entity(10), simulation::PhysicsBody::create(review_point(400.0, 320.0), zero, zero));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      review_entity(10), simulation::Controllable{simulation::ControllerId::create(10)});

  const simulation::PhysicsBody hazard =
      simulation::PhysicsBody::create(review_point(430.0, 320.0), review_point(-40.0, 0.0), zero,
                                      26.0, 40.0, simulation::PhysicsBody::kDefaultCollisionLayer,
                                      simulation::PhysicsBody::kDefaultCollisionMask, false)
          .with_restitution(0.35)
          .with_drag_scale(simulation::PhysicsBody::kMinimumDragScale)
          .with_bounds_behavior(simulation::BoundsBehavior::kCross);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(review_entity(11), hazard);
  world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
      review_entity(11), simulation::LethalOnContact{});

  simulation::MatchState& match = world.mutable_match();
  match.phase = phase;
  match.phase_started_tick = simulation::TickSequence::create(50);
  match.running_started_tick = simulation::TickSequence::create(10);
  match.outcome = phase == simulation::MatchPhase::kEnded
                      ? simulation::MatchOutcome::won_by_entity(review_entity(10))
                      : simulation::MatchOutcome::undecided();
  match.seats = simulation::SeatRoster::of_size(1);
  if (phase == simulation::MatchPhase::kCountdown) {
    match.seats.assign_seat(
        0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(10)}});
    match.seats.request_start();
  }
  gameplay::royale_mode_state_in(world).previous_phase = previous_phase;

  return simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::of_mode(
          map, gameplay::RoyaleMode::create(gameplay::RoyaleConfiguration::defaults())));
}

TEST_CASE("REVIEW: a lethal hazard eliminates and records a placement outside running",
          "[review][gameplay][royale][hazard]") {
  const auto run_one_tick = [](simulation::GameSimulation& game) {
    game.step(testing::kGameplayFixedDelta, testing::gameplay_batch(game, {}, 100, 8));
    return game.snapshot();
  };

  SECTION("ended: the committed winner is destroyed and given placement 1") {
    simulation::GameSimulation game = review_world_with_lethal_hazard(
        simulation::MatchPhase::kEnded, simulation::MatchPhase::kEnded);
    const simulation::WorldSnapshot after = run_one_tick(game);
    CHECK(after.match().phase() == simulation::MatchPhase::kEnded);
    CHECK(after.match().outcome() == simulation::MatchOutcome::won_by_entity(review_entity(10)));
    CHECK_FALSE(review_snapshot_holds(after, review_entity(10)));
    const std::vector<simulation::RoyalePlacement> placements = review_placements(after);
    REQUIRE(placements.size() == 1);
    CHECK(placements[0].entity == review_entity(10));
    CHECK(placements[0].placement == 1);
  }

  SECTION("lobby: a seated player is destroyed and a placement is appended to no match") {
    simulation::GameSimulation game = review_world_with_lethal_hazard(
        simulation::MatchPhase::kLobby, simulation::MatchPhase::kLobby);
    const simulation::WorldSnapshot after = run_one_tick(game);
    CHECK(after.match().phase() == simulation::MatchPhase::kLobby);
    CHECK_FALSE(review_snapshot_holds(after, review_entity(10)));
    CHECK(review_placements(after).size() == 1);
  }

  SECTION("countdown: a seated player is destroyed while the countdown keeps running") {
    simulation::GameSimulation game = review_world_with_lethal_hazard(
        simulation::MatchPhase::kCountdown, simulation::MatchPhase::kCountdown);
    const simulation::WorldSnapshot after = run_one_tick(game);
    CHECK(after.match().phase() == simulation::MatchPhase::kCountdown);
    CHECK_FALSE(review_snapshot_holds(after, review_entity(10)));
    CHECK(review_placements(after).size() == 1);
  }
}
```

`review_entity`, `review_point`, `review_placements` (reads the `RoyalePlacementsModeState` arm out
of a snapshot) and `review_snapshot_holds` (linear scan of `snapshot.entities()`) are the obvious
helpers and are omitted.

### C. The seat count and the map (finding 3)

Appended to `tests/unit/gameplay/royale/royale_mode_tests.cpp`.

```cpp
TEST_CASE("REVIEW: set_seat_count grows a lobby past the map's spawn markers",
          "[review][gameplay][royale][lobby]") {
  gameplay::RoyaleConfiguration::Section section =
      gameplay::RoyaleConfiguration::default_section();
  section.lobby_seat_count = 2;
  const simulation::MapDefinition map = testing::gameplay_map(4);
  REQUIRE(map.spawn_points().size() == 4);

  simulation::GameSimulation game = testing::gameplay_simulation(
      gameplay::RoyaleMode::create(gameplay::RoyaleConfiguration::create(section)), map, 0,
      simulation::SeatRoster::of_size(2));

  const simulation::InputBatch batch = testing::gameplay_batch(
      game,
      {simulation::Command{
          simulation::SetSeatCountCommand{simulation::ControllerId::create(1), 64}}},
      100, 8);
  game.step(testing::kGameplayFixedDelta, batch);

  const simulation::WorldSnapshot after = game.snapshot();
  // validate_map refused a *configured* count above the marker count; the command is not held to
  // the same bound.
  CHECK(after.match().seats().seat_count() == 64);
  CHECK(after.match().seats().seat_count() > map.spawn_points().size());
}
```
