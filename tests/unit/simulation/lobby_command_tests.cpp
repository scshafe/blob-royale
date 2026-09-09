#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/join_command.hpp"
#include "commands/leave_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/start_match_command.hpp"
#include "components/controllable_component.hpp"
#include "contact_rule_table.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_mode.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "idle_match_objective.hpp"
#include "idle_spawn_policy.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "physics_body.hpp"
#include "seat_roster.hpp"
#include "simulation_config.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

// The map every lobby below runs on: `kSpawnPointCount` markers, which is the ceiling a lobby may
// grow to at run time, exactly as it is the ceiling `validate_map` holds a configured count to.
inline constexpr std::size_t kSpawnPointCount = 8;

[[nodiscard]] simulation::MapDefinition lobby_map() {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.reserve(kSpawnPointCount);
  for (std::size_t index = 0; index < kSpawnPointCount; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        simulation::Vector2::create(50.0 * static_cast<double>(index + 1), 250.0)));
  }
  return simulation::MapDefinition::create("lobby_map",
                                           simulation::ArenaBounds::create(500.0, 500.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// A world with a declared lobby and nothing else, stepped with no mode, so the only thing any of
// these ticks can change is the seat roster. There is no mode, so the engine's idle objective never
// starts a match and the phase stays `lobby` unless a test moves it deliberately -- which is what
// makes "the roster changed" the whole observable. The map is there because phase 0 holds a
// `set_seat_count` to the map's spawn-marker count, and a bare arena has none.
[[nodiscard]] simulation::GameSimulation lobby_simulation(const std::size_t seat_count) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(seat_count);
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(lobby_map()));
}

[[nodiscard]] simulation::InputBatch batch(std::vector<simulation::Command> commands) {
  return simulation::InputBatch::create(std::move(commands), simulation::CommandKindMask::all(),
                                        simulation::EntityIdReservation::none());
}

void step(simulation::GameSimulation& game, std::vector<simulation::Command> commands) {
  game.step(simulation::FixedDelta::canonical(), batch(std::move(commands)));
}

[[nodiscard]] simulation::Command seat_npc(const std::uint64_t controller,
                                           const std::uint64_t seat_index,
                                           const std::string_view kind) {
  return simulation::Command{
      simulation::SeatNpcCommand{simulation::ControllerId::create(controller), seat_index,
                                 simulation::SeatKindName::create(kind)}};
}

[[nodiscard]] simulation::Command clear_seat(const std::uint64_t controller,
                                             const std::uint64_t seat_index) {
  return simulation::Command{
      simulation::ClearSeatCommand{simulation::ControllerId::create(controller), seat_index}};
}

[[nodiscard]] simulation::Command set_seat_count(const std::uint64_t controller,
                                                 const std::uint64_t seat_count) {
  return simulation::Command{
      simulation::SetSeatCountCommand{simulation::ControllerId::create(controller), seat_count}};
}

[[nodiscard]] simulation::Command start_match(const std::uint64_t controller) {
  return simulation::Command{
      simulation::StartMatchCommand{simulation::ControllerId::create(controller)}};
}

// The committed roster, **by value**. `GameSimulation::snapshot()` returns a snapshot by value, so
// a reference into one taken inline would dangle at the semicolon; copying a handful of seats is
// what makes every assertion below read the state it names.
[[nodiscard]] simulation::SeatRoster roster_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().seats();
}

[[nodiscard]] simulation::MatchPhase phase_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().phase();
}

[[nodiscard]] std::optional<std::string> npc_kind_at(const simulation::SeatRoster& roster,
                                                     const std::size_t index) {
  const auto* const declared = std::get_if<simulation::NpcSeat>(&roster.seats()[index]);
  return declared == nullptr ? std::nullopt : std::optional<std::string>{declared->kind.value()};
}

} // namespace

TEST_CASE("A seated NPC is recorded as a declaration with no controller yet",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(4);
  step(game, {seat_npc(1, 2, "wanderer")});

  const simulation::SeatRoster roster = roster_of(game);
  REQUIRE(roster.seat_count() == 4);
  CHECK(npc_kind_at(roster, 2) == std::string{"wanderer"});
  // The simulation cannot construct a controller, so the seat is a declaration the runtime has yet
  // to reconcile. The absent controller is what a reconciliation reads to decide it has work to do.
  CHECK_FALSE(std::get<simulation::NpcSeat>(roster.seats()[2]).controller.has_value());
  CHECK(simulation::seat_is_filled(roster.seats()[2]));
  CHECK_FALSE(roster.is_full());
}

TEST_CASE("Two clients seating one seat in one tick resolve by the batch's order, and the second "
          "press is a no-op",
          "[unit][simulation][lobby][command]") {
  // The stated rule, and the reason first-wins rather than last-wins: a press must never evict
  // whoever is already sitting there. Submission order is deliberately the reverse of controller
  // order so the assertion cannot pass by accident of arrival.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(9, 0, "chaser"), seat_npc(4, 0, "wanderer")});

  CHECK(npc_kind_at(roster_of(game), 0) == std::string{"wanderer"});

  // And on a later tick it is still a no-op, so "first wins" is a property of the seat rather than
  // of one batch.
  step(game, {seat_npc(9, 0, "chaser")});
  CHECK(npc_kind_at(roster_of(game), 0) == std::string{"wanderer"});
}

TEST_CASE("One controller may clear a seat and refill it in a single tick",
          "[unit][simulation][lobby][command]") {
  // This is the only way to replace an occupant, and it works because `clear_seat` ranks ahead of
  // `seat_npc` in phase 0's application order. Reversing the two ranks would make replacing a seat
  // impossible without a wasted tick.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 0, "wanderer")});
  REQUIRE(npc_kind_at(roster_of(game), 0) == std::string{"wanderer"});

  step(game, {seat_npc(1, 0, "chaser"), clear_seat(1, 0)});
  CHECK(npc_kind_at(roster_of(game), 0) == std::string{"chaser"});
}

TEST_CASE("Clearing empties a declared NPC seat and leaves a live controller's seat alone",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 0, "wanderer")});
  const simulation::SeatRoster seated = roster_of(game);
  REQUIRE(simulation::seat_is_filled(seated.seats()[0]));

  step(game, {clear_seat(1, 0)});
  const simulation::SeatRoster emptied = roster_of(game);
  CHECK_FALSE(simulation::seat_is_filled(emptied.seats()[0]));

  // A seat a live session holds is the runtime's to give and take. A tick that emptied one would be
  // contradicted by the next reconciliation, which seats that session again because it is still
  // connected, so the command declines rather than lying. The seat is written into the world
  // directly, because no command can produce a controller seat before plan Step 7.
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(2);
  world.mutable_match().seats.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(3)}});
  simulation::GameSimulation occupied = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world));

  step(occupied, {clear_seat(9, 0)});
  const simulation::SeatRoster after_clear = roster_of(occupied);
  REQUIRE(std::holds_alternative<simulation::ControllerSeat>(after_clear.seats()[0]));
  CHECK(std::get<simulation::ControllerSeat>(after_clear.seats()[0]).controller ==
        simulation::ControllerId::create(3));

  // Seating over it is refused too, by the ordinary first-wins rule, so the two commands agree
  // about who owns a person's seat rather than disagreeing at the edges.
  step(occupied, {seat_npc(9, 0, "wanderer")});
  const simulation::SeatRoster after_seat = roster_of(occupied);
  CHECK(std::holds_alternative<simulation::ControllerSeat>(after_seat.seats()[0]));
}

TEST_CASE("A lobby grows and shrinks, and never shrinks past somebody sitting down",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {set_seat_count(1, 5)});
  CHECK(roster_of(game).seat_count() == 5);

  step(game, {seat_npc(1, 3, "wanderer")});
  step(game, {set_seat_count(1, 2)});
  // Ignored, not rejected: a client's view of the roster lags the world by a frame, and racing it
  // must not fail a tick everybody shares.
  const simulation::SeatRoster refused = roster_of(game);
  CHECK(refused.seat_count() == 5);
  CHECK(npc_kind_at(refused, 3) == std::string{"wanderer"});

  step(game, {set_seat_count(1, 4)});
  CHECK(roster_of(game).seat_count() == 4);

  // Growing and seating the new seat in one tick works, because `set_seat_count` ranks ahead of
  // `seat_npc`.
  step(game, {seat_npc(1, 5, "chaser"), set_seat_count(1, 6)});
  const simulation::SeatRoster grown = roster_of(game);
  CHECK(grown.seat_count() == 6);
  CHECK(npc_kind_at(grown, 5) == std::string{"chaser"});
}

TEST_CASE("A lobby never grows past the map's spawn markers",
          "[unit][simulation][lobby][command]") {
  // The wire admits any count up to 64 and `validate_map` only ever saw the configured count, so
  // the run-time command is the one place a lobby could outgrow the arena it plays on. A count
  // above the marker count is a disagreement with committed state -- the map -- that the client
  // could not have known, so it is ignored rather than refused
  // (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 3).
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {set_seat_count(1, kSpawnPointCount + 1)});
  CHECK(roster_of(game).seat_count() == 2);
  step(game, {set_seat_count(1, simulation::kMaximumLobbySeatCount)});
  CHECK(roster_of(game).seat_count() == 2);

  // Exactly the marker count is a lobby the field can seat in full.
  step(game, {set_seat_count(1, kSpawnPointCount)});
  CHECK(roster_of(game).seat_count() == kSpawnPointCount);
  step(game, {set_seat_count(1, kSpawnPointCount + 1)});
  CHECK(roster_of(game).seat_count() == kSpawnPointCount);
}

TEST_CASE("A seat index inside the wire bound that names no seat is ignored",
          "[unit][simulation][lobby][command]") {
  // The boundary bounds the *value* at `kMaximumLobbySeatCount`; only the tick knows how many seats
  // this lobby actually has, and an index past the end is a stale view rather than an attack.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 40, "wanderer"), clear_seat(1, 40)});

  const simulation::SeatRoster roster = roster_of(game);
  CHECK(roster.seat_count() == 2);
  for (const simulation::Seat& seat : roster.seats()) {
    CHECK_FALSE(simulation::seat_is_filled(seat));
  }
}

TEST_CASE("Pressing Start records a request and commits no transition",
          "[unit][simulation][lobby][command]") {
  simulation::GameSimulation game = lobby_simulation(1);
  step(game, {start_match(1)});

  // The flag is set even though the lobby is empty: the press is a fact about a person, and the
  // objective decides separately whether the field is complete.
  const simulation::SeatRoster pressed = roster_of(game);
  CHECK(pressed.start_requested());
  CHECK_FALSE(pressed.is_full());
  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);

  // Pressing twice is one request, and the request survives until the seats fill or the match ends.
  step(game, {start_match(1), start_match(2)});
  CHECK(roster_of(game).start_requested());

  step(game, {seat_npc(1, 0, "wanderer")});
  const simulation::SeatRoster complete = roster_of(game);
  CHECK(complete.is_full());
  CHECK(complete.start_requested());
}

TEST_CASE("Every lobby command is refused outside the lobby phase",
          "[unit][simulation][lobby][command]") {
  // A seat roster is only meaningful before a match. Resizing the field mid-match, or seating a bot
  // into a running game, are changes the arena has no way to represent, so the guard is one line
  // for all four rather than four separate opinions.
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(2);
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  simulation::GameSimulation game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world));

  step(game, {set_seat_count(1, 6), seat_npc(1, 0, "wanderer"), clear_seat(1, 1), start_match(1)});

  const simulation::WorldSnapshot snapshot = game.snapshot();
  const simulation::SeatRoster roster = snapshot.match().seats();
  CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(roster.seat_count() == 2);
  CHECK_FALSE(roster.start_requested());
  for (const simulation::Seat& seat : roster.seats()) {
    CHECK_FALSE(simulation::seat_is_filled(seat));
  }
}

TEST_CASE("A lobby command is applied at phase 0, so a replayed log reproduces the roster exactly",
          "[unit][simulation][lobby][command][determinism]") {
  // Determinism is inherited rather than reinvented: the commands are ordinary batch members in the
  // canonical order, so two fresh runs of one log commit the same roster on the same tick.
  const auto run = [] {
    simulation::GameSimulation game = lobby_simulation(3);
    step(game, {seat_npc(5, 1, "chaser"), seat_npc(2, 0, "wanderer"), set_seat_count(9, 4)});
    step(game, {seat_npc(2, 2, "wanderer"), clear_seat(5, 1), start_match(7)});
    return game.snapshot();
  };

  const simulation::WorldSnapshot first = run();
  const simulation::WorldSnapshot second = run();
  CHECK(first == second);
  const simulation::SeatRoster roster = first.match().seats();
  CHECK(roster.seat_count() == 4);
  CHECK(npc_kind_at(roster, 0) == std::string{"wanderer"});
  CHECK_FALSE(simulation::seat_is_filled(roster.seats()[1]));
  CHECK(npc_kind_at(roster, 2) == std::string{"wanderer"});
  CHECK(roster.start_requested());
}

namespace {

[[nodiscard]] simulation::Command leave(const std::uint64_t controller) {
  return simulation::Command{
      simulation::LeaveCommand{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] simulation::Command spawn(const std::uint64_t controller) {
  return simulation::Command{
      simulation::SpawnCommand{simulation::ControllerId::create(controller)}};
}

// A person's join names no seat; a bot's names the one that declared it.
[[nodiscard]] simulation::Command
join(const std::uint64_t controller, const std::optional<std::uint64_t> seat_index = std::nullopt) {
  return simulation::Command{
      simulation::JoinCommand{simulation::ControllerId::create(controller), seat_index}};
}

[[nodiscard]] simulation::Seat person_at(const std::uint64_t controller) {
  return simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] simulation::Seat bot_at(const std::string_view kind,
                                      const std::optional<std::uint64_t> controller) {
  return simulation::Seat{simulation::NpcSeat{
      simulation::SeatKindName::create(kind),
      controller.has_value() ? std::optional{simulation::ControllerId::create(*controller)}
                             : std::nullopt}};
}

// A tick that may create: the plain `batch` above carries no reservation, which is right for a
// roster-only tick and a hard failure for a spawn.
void step_creating(simulation::GameSimulation& game, std::vector<simulation::Command> commands,
                   const std::uint64_t first_created_entity_id) {
  game.step(simulation::FixedDelta::canonical(),
            simulation::InputBatch::create(
                std::move(commands), simulation::CommandKindMask::all(),
                simulation::EntityIdReservation::create(
                    simulation::EntityId::create(first_created_entity_id), 8)));
}

[[nodiscard]] bool snapshot_holds_controller(const simulation::WorldSnapshot& snapshot,
                                             const std::uint64_t controller) {
  for (const auto& entry : snapshot.components<simulation::Controllable>()) {
    if (entry.value.controller_id == simulation::ControllerId::create(controller)) {
      return true;
    }
  }
  return false;
}

// A world with one seated body driven by controller 5 in seat 0, and an NPC seat whose bot is
// controller 6 in seat 1, on the same marker map every lobby here runs on, in the phase named.
[[nodiscard]] simulation::GameSimulation
seated_lobby_simulation(const simulation::MatchPhase phase = simulation::MatchPhase::kLobby) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(3),
          simulation::PhysicsBody::create(simulation::Vector2::create(250.0, 400.0), zero, zero),
          simulation::ControllerId::create(5))});
  simulation::SeatRoster roster = simulation::SeatRoster::of_size(2);
  roster.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(5)}});
  roster.assign_seat(
      1, simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create("wanderer"),
                                              simulation::ControllerId::create(6)}});
  world.mutable_match().seats = std::move(roster);
  world.mutable_match().phase = phase;
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(lobby_map()));
}

// A mode that accepts exactly the kinds it is given, which is how a test states the mistake the
// startup check exists to refuse: a mode that lets a session in and refuses what the server issues
// on its behalf.
class RefusingMode final : public simulation::GameMode {
public:
  explicit RefusingMode(const simulation::CommandKindMask accepted) noexcept
      : accepted_(accepted) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "refusing"; }
  [[nodiscard]] simulation::SystemPipeline systems() const override {
    return simulation::SystemPipeline::empty();
  }
  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::built_in();
  }
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return accepted_;
  }
  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const simulation::IdleSpawnPolicy>();
  }
  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const simulation::IdleMatchObjective>();
  }
  void validate_map(const simulation::MapDefinition&) const override {}

private:
  simulation::CommandKindMask accepted_;
};

// Whether `GameSimulation::create` refuses a mode with this mask, and with which code.
[[nodiscard]] std::optional<simulation::SimulationValidationCode>
startup_refusal_of(const simulation::CommandKindMask accepted) {
  try {
    static_cast<void>(simulation::GameSimulation::create(
        simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8),
        simulation::GameWorld::create({}),
        simulation::GameSimulationSetup::of_mode(lobby_map(),
                                                 std::make_unique<const RefusingMode>(accepted))));
    return std::nullopt;
  } catch (const simulation::SimulationValidationError& error) {
    return error.validation_code();
  }
}

} // namespace

TEST_CASE("A leave destroys what its controller drove and vacates the seat it held",
          "[unit][simulation][lobby][leave]") {
  simulation::GameSimulation game = seated_lobby_simulation();
  REQUIRE(snapshot_holds_controller(game.snapshot(), 5));

  // A person leaves: the body goes, and the seat is empty again.
  step(game, {leave(5)});
  const simulation::WorldSnapshot after_person = game.snapshot();
  CHECK_FALSE(snapshot_holds_controller(after_person, 5));
  CHECK(after_person.entities().empty());
  const simulation::SeatRoster roster_after_person = after_person.match().seats();
  CHECK(roster_after_person.seats()[0] == simulation::Seat{simulation::EmptySeat{}});
  CHECK(roster_after_person.seats()[1] ==
        simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create("wanderer"),
                                             simulation::ControllerId::create(6)}});

  // A bot leaves: the seat keeps its declaration and loses its bot, so the reconciliation that
  // created the bot can create another.
  step(game, {leave(6)});
  const simulation::WorldSnapshot after_bot = game.snapshot();
  const simulation::SeatRoster roster_after_bot = after_bot.match().seats();
  CHECK(roster_after_bot.seats()[1] ==
        simulation::Seat{
            simulation::NpcSeat{simulation::SeatKindName::create("wanderer"), std::nullopt}});
}

TEST_CASE("A spawn and a leave for one controller in one batch net to nothing",
          "[unit][simulation][lobby][leave]") {
  // The window review finding 1 describes: the spawn was queued when the socket closed, so both
  // arrive in one drain. `leave` ranks after `spawn`, so the entity is created and then destroyed
  // inside the same phase 0 pass, and nothing is committed.
  simulation::GameSimulation game = lobby_simulation(2);
  step_creating(game, {spawn(9), leave(9)}, 100);
  const simulation::WorldSnapshot after = game.snapshot();
  CHECK_FALSE(snapshot_holds_controller(after, 9));
  CHECK(after.entities().empty());
}

TEST_CASE("A leave on a later tick destroys the pending entity an earlier spawn created",
          "[unit][simulation][lobby][leave]") {
  simulation::GameSimulation game = lobby_simulation(2);
  step_creating(game, {spawn(9)}, 100);
  // The engine's idle policy never seats, so this is the pending case: a `Controllable` and no
  // body.
  REQUIRE(snapshot_holds_controller(game.snapshot(), 9));

  step(game, {leave(9)});
  const simulation::WorldSnapshot after_leave = game.snapshot();
  CHECK_FALSE(snapshot_holds_controller(after_leave, 9));
  CHECK(after_leave.entities().empty());
}

TEST_CASE("A leave for a controller that drives nothing and sits nowhere changes nothing",
          "[unit][simulation][lobby][leave]") {
  simulation::GameSimulation game = seated_lobby_simulation();
  const simulation::WorldSnapshot before = game.snapshot();
  step(game, {leave(42)});
  const simulation::WorldSnapshot after = game.snapshot();
  CHECK(after.entities().size() == before.entities().size());
  CHECK(after.match().seats() == before.match().seats());
}

TEST_CASE("A declared mode that refuses a server-issued kind is refused at startup",
          "[unit][simulation][lobby][leave][join]") {
  // Refusing `leave` would have the mailbox drop it on every disconnect and leave a body nobody
  // owns in the arena; the engine refuses the mode before a session can exist.
  CHECK(
      startup_refusal_of(simulation::CommandKindMask::create({simulation::CommandKind::kThrust})) ==
      simulation::SimulationValidationCode::kGameSimulationModeRefusesServerIssuedKind);

  // A mode with a lobby -- it accepts `start_match` -- must accept the join that fills it, or no
  // session and no bot could ever take a seat and the lobby would wait forever.
  CHECK(startup_refusal_of(simulation::CommandKindMask::create(
            {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
             simulation::CommandKind::kLeave, simulation::CommandKind::kThrust,
             simulation::CommandKind::kStartMatch})) ==
        simulation::SimulationValidationCode::kGameSimulationModeRefusesServerIssuedKind);

  // A mode without a lobby owes no join, and one with a lobby that accepts it is accepted.
  CHECK_FALSE(
      startup_refusal_of(simulation::CommandKindMask::create(
                             {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
                              simulation::CommandKind::kLeave, simulation::CommandKind::kThrust}))
          .has_value());
  CHECK_FALSE(startup_refusal_of(simulation::CommandKindMask::all()).has_value());
}

TEST_CASE("A person's join takes the lowest empty seat and a second join leaves them there",
          "[unit][simulation][lobby][join]") {
  simulation::GameSimulation game = lobby_simulation(3);
  step(game, {join(7)});
  const simulation::SeatRoster after_first = roster_of(game);
  CHECK(after_first.seats()[0] == person_at(7));
  CHECK(after_first.seats()[1] == simulation::Seat{simulation::EmptySeat{}});

  // The session asks again every tenth of a second until it observes its seat; asking while seated
  // moves nobody.
  step(game, {join(7)});
  CHECK(roster_of(game) == after_first);

  // Two people in one tick take the next two seats in the batch's order, which is controller order.
  step(game, {join(9), join(8)});
  const simulation::SeatRoster after_both = roster_of(game);
  CHECK(after_both.seats()[1] == person_at(8));
  CHECK(after_both.seats()[2] == person_at(9));
  CHECK(after_both.is_full());
}

TEST_CASE("A bot's join fills exactly the seat that declared its kind and nothing else",
          "[unit][simulation][lobby][join]") {
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {seat_npc(1, 1, "wanderer")});
  const simulation::SeatRoster declared = roster_of(game);
  REQUIRE(declared.seats()[1] == bot_at("wanderer", std::nullopt));

  step(game, {join(6, 1)});
  const simulation::SeatRoster seated = roster_of(game);
  CHECK(seated.seats()[1] == bot_at("wanderer", 6));
  CHECK(seated.seats()[0] == simulation::Seat{simulation::EmptySeat{}});

  // A named seat that is empty, one that already holds a bot, and one this roster does not have:
  // the bot seats nowhere, and the reconciliation that created it will retire it. An index inside
  // the wire bound but outside the roster is the same no-op every lobby command makes of it.
  step(game, {join(7, 0)});
  step(game, {join(8, 1)});
  step(game, {join(9, kSpawnPointCount - 1)});
  CHECK(roster_of(game) == seated);

  // The bot that holds the seat and asks again is left where it is.
  step(game, {join(6, 1)});
  CHECK(roster_of(game) == seated);
}

TEST_CASE("A person displaces the lowest NPC seat before a match starts, and nobody after",
          "[unit][simulation][lobby][join]") {
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown}) {
    simulation::GameSimulation game = seated_lobby_simulation(phase);
    step(game, {join(7)});
    const simulation::SeatRoster roster = roster_of(game);
    // The person's seat was the bot's; the person already seated is untouched.
    CHECK(roster.seats()[0] == person_at(5));
    CHECK(roster.seats()[1] == person_at(7));
    // The displaced bot asking for its declared seat back finds a person in it.
    step(game, {join(6, 1)});
    CHECK(roster_of(game) == roster);
  }

  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded}) {
    simulation::GameSimulation game = seated_lobby_simulation(phase);
    const simulation::SeatRoster before = roster_of(game);
    step(game, {join(7)});
    CHECK(roster_of(game) == before);
  }
}

TEST_CASE("A join into a full lobby with no NPC to displace changes nothing",
          "[unit][simulation][lobby][join]") {
  simulation::GameSimulation game = lobby_simulation(1);
  step(game, {join(5)});
  const simulation::SeatRoster full = roster_of(game);
  REQUIRE(full.seats()[0] == person_at(5));
  step(game, {join(7)});
  CHECK(roster_of(game) == full);
}

TEST_CASE("A join and a leave for one controller in one batch net to no seat",
          "[unit][simulation][lobby][join][leave]") {
  // The session's first presentation slot submits the join and the socket closes before the next
  // drain: `leave` ranks after `join`, so the seat is taken and vacated inside one phase 0 pass.
  simulation::GameSimulation game = lobby_simulation(2);
  step(game, {join(7), leave(7)});
  CHECK(roster_of(game) == simulation::SeatRoster::of_size(2));
}
