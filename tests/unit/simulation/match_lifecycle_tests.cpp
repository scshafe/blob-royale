#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "components/lifetime_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_lifecycle_system.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "physics_body.hpp"
#include "seat_roster.hpp"
#include "simulation_config.hpp"
#include "simulation_system.hpp"
#include "simulation_test_fixture.hpp"
#include "system_pipeline.hpp"
#include "team_id.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(
      500.0, 500.0, 10.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 10, 10);
}

[[nodiscard]] simulation::GameWorld::EntitySeed player(const simulation::EntityId::Value id,
                                                       const double x, const double y) {
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(id),
      simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                      simulation::Vector2::create(0.0, 0.0),
                                      simulation::Vector2::create(0.0, 0.0)));
}

// A match under the in-test mode over a map with no spawn points, so seating never interferes with
// a lifecycle assertion.
[[nodiscard]] simulation::GameSimulation
lifecycle_game(std::vector<simulation::GameWorld::EntitySeed> seeds,
               const std::size_t minimum_players,
               const simulation::MatchLifecycleDurations durations,
               simulation::SeatRoster lobby = simulation::SeatRoster{}) {
  testing::TestGameMode::Declaration declaration;
  declaration.name = "lifecycle_mode";
  declaration.minimum_players = minimum_players;
  declaration.durations = durations;
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  // The lobby is seeded onto the initial world exactly as production seeds it. The in-test mode's
  // objective never reads it, so it changes no transition here and only the engine's own clearing
  // rule can be observed through it.
  world.mutable_match().seats = std::move(lobby);
  return simulation::GameSimulation::create(
      configuration(), std::move(world),
      simulation::GameSimulationSetup::of_mode(
          simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(500.0, 500.0)),
          testing::TestGameMode::create(std::move(declaration))));
}

void advance(simulation::GameSimulation& game, const std::size_t tick_count) {
  for (std::size_t tick = 0; tick < tick_count; ++tick) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
}

void despawn(simulation::GameSimulation& game,
             const std::vector<simulation::EntityId::Value>& entities) {
  std::vector<simulation::Command> commands;
  commands.reserve(entities.size());
  for (const simulation::EntityId::Value entity : entities) {
    commands.push_back(
        simulation::Command{simulation::DespawnCommand{simulation::EntityId::create(entity)}});
  }
  game.step(simulation::FixedDelta::canonical(),
            simulation::InputBatch::create(std::move(commands), simulation::CommandKindMask::all(),
                                           simulation::EntityIdReservation::none()));
}

// Each accessor returns by value: the snapshot it reads is a temporary, so a reference into it
// would dangle, and the ref-qualified accessors on the publication say so at compile time.
[[nodiscard]] simulation::MatchPhase phase_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().phase();
}

[[nodiscard]] std::uint64_t phase_started_tick_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().phase_started_tick().value();
}

[[nodiscard]] std::uint64_t running_started_tick_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().running_started_tick().value();
}

[[nodiscard]] bool start_requested_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().seats().start_requested();
}

[[nodiscard]] simulation::SeatRoster seats_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().seats();
}

[[nodiscard]] simulation::MatchOutcome outcome_of(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().outcome();
}

} // namespace

TEST_CASE("MatchOutcome names its winner in the member the kind selects",
          "[unit][simulation][match_outcome]") {
  const simulation::MatchOutcome undecided = simulation::MatchOutcome::undecided();
  const simulation::MatchOutcome by_entity =
      simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(4));
  const simulation::MatchOutcome by_team =
      simulation::MatchOutcome::won_by_team(simulation::TeamId::create(2));
  const simulation::MatchOutcome drawn = simulation::MatchOutcome::drawn();

  CHECK_FALSE(undecided.is_decided());
  CHECK_FALSE(undecided.winning_entity().has_value());
  CHECK_FALSE(undecided.winning_team().has_value());

  CHECK(by_entity.is_decided());
  REQUIRE(by_entity.winning_entity().has_value());
  CHECK(by_entity.winning_entity()->value() == 4);
  CHECK_FALSE(by_entity.winning_team().has_value());

  CHECK(by_team.is_decided());
  REQUIRE(by_team.winning_team().has_value());
  CHECK(by_team.winning_team()->value() == 2);
  CHECK_FALSE(by_team.winning_entity().has_value());

  CHECK(drawn.is_decided());
  CHECK_FALSE(drawn.winning_entity().has_value());
  CHECK_FALSE(drawn.winning_team().has_value());

  CHECK(by_entity == simulation::MatchOutcome::won_by_entity(simulation::EntityId::create(4)));
  CHECK_FALSE(by_entity == drawn);
  CHECK(simulation::match_outcome_kind_name(by_team.kind()) == "won_by_team");
}

TEST_CASE("every MatchPhase has a declared name", "[unit][simulation][match_phase]") {
  CHECK(simulation::kMatchPhaseCount == 4);
  CHECK(simulation::match_phase_name(simulation::MatchPhase::kLobby) == "lobby");
  CHECK(simulation::match_phase_name(simulation::MatchPhase::kCountdown) == "countdown");
  CHECK(simulation::match_phase_name(simulation::MatchPhase::kRunning) == "running");
  CHECK(simulation::match_phase_name(simulation::MatchPhase::kEnded) == "ended");
}

TEST_CASE("a default MatchState is the state every match begins in",
          "[unit][simulation][match_state]") {
  const simulation::MatchState state;

  CHECK(state.phase == simulation::MatchPhase::kLobby);
  CHECK(state.phase_started_tick == simulation::TickSequence::zero());
  CHECK(state.running_started_tick == simulation::TickSequence::zero());
  CHECK_FALSE(state.outcome.is_decided());
  CHECK(state.spawn_rotation_counter == 0);
  CHECK(simulation::mode_match_state_schema_id_of(state.mode_state) == "none");
  // No lobby has been declared, so there are no seats and no pending start. A world nobody seeded a
  // roster onto is a world no seat-reading objective will ever start.
  CHECK(state.seats.seat_count() == 0);
  CHECK_FALSE(state.seats.is_full());
  CHECK_FALSE(state.seats.start_requested());
}

TEST_CASE("every committed transition into lobby clears the pending start request",
          "[unit][simulation][match_lifecycle_system][seat_roster]") {
  // The engine's own rule, tested through the engine's own machine and deliberately not through a
  // mode: the clearing happens in `commit_transition` and applies whatever a mode's `can_start`
  // reads. The in-test mode's objective is the alive count, so the roster below drives nothing and
  // the only thing under test is when the flag is cleared.
  //
  // The flag survives `lobby -> countdown` and `countdown -> running`, which is the part that would
  // be wrong if it were cleared on the way *out* of `lobby`: a request cleared there would make the
  // next tick's "may it stay in `countdown`" check fail and no match could ever run.
  simulation::SeatRoster lobby = simulation::SeatRoster::of_size(2);
  lobby.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(1)}});
  lobby.assign_seat(
      1, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(2)}});
  lobby.request_start();

  simulation::GameSimulation game =
      lifecycle_game({player(1, 50.0, 50.0), player(2, 300.0, 300.0)}, 2,
                     simulation::MatchLifecycleDurations{2, 1}, std::move(lobby));
  REQUIRE(start_requested_of(game));

  advance(game, 1);
  REQUIRE(phase_of(game) == simulation::MatchPhase::kCountdown);
  CHECK(start_requested_of(game));

  advance(game, 2);
  REQUIRE(phase_of(game) == simulation::MatchPhase::kRunning);
  CHECK(start_requested_of(game));

  // The field empties, so the match ends and the restart delay returns it to `lobby` -- where the
  // request is gone and the next match waits for someone to ask for it.
  despawn(game, {1, 2});
  REQUIRE(phase_of(game) == simulation::MatchPhase::kEnded);
  CHECK(start_requested_of(game));

  advance(game, 1);
  REQUIRE(phase_of(game) == simulation::MatchPhase::kLobby);
  CHECK_FALSE(start_requested_of(game));
  // And the seats themselves are untouched: arriving in the lobby clears the request and nothing
  // else, because who is seated is not the machine's business.
  CHECK(seats_of(game).seat_count() == 2);
  CHECK(seats_of(game).is_full());
}

TEST_CASE("a countdown that returns to lobby also discards the start request",
          "[unit][simulation][match_lifecycle_system][seat_roster]") {
  // The other direction into `lobby`, and the reason the rule is "on arrival" rather than "after a
  // match": a field that broke up during the countdown is back in the lobby, and a lobby that still
  // held a request would restart itself the moment the seat refilled without anyone asking twice.
  simulation::SeatRoster lobby = simulation::SeatRoster::of_size(1);
  lobby.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(1)}});
  lobby.request_start();

  simulation::GameSimulation game =
      lifecycle_game({player(1, 50.0, 50.0), player(2, 300.0, 300.0)}, 2,
                     simulation::MatchLifecycleDurations{8, 0}, std::move(lobby));

  advance(game, 1);
  REQUIRE(phase_of(game) == simulation::MatchPhase::kCountdown);
  REQUIRE(start_requested_of(game));

  despawn(game, {2});
  REQUIRE(phase_of(game) == simulation::MatchPhase::kLobby);
  CHECK_FALSE(start_requested_of(game));
}

TEST_CASE("the lifecycle machine walks lobby, countdown, running, ended and back",
          "[unit][simulation][match_lifecycle_system][game_mode]") {
  // Two players and a two-tick countdown with a three-tick restart delay. Every transition below
  // is committed on exactly the tick the machine's rule names.
  simulation::GameSimulation game = lifecycle_game({player(1, 50.0, 50.0), player(2, 300.0, 300.0)},
                                                   2, simulation::MatchLifecycleDurations{2, 3});

  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);

  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kCountdown);
  CHECK(phase_started_tick_of(game) == 1);

  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kCountdown);

  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kRunning);
  CHECK(phase_started_tick_of(game) == 3);
  CHECK(running_started_tick_of(game) == 3);

  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kRunning);
  CHECK_FALSE(outcome_of(game).is_decided());

  // One entity leaves and the match is decided on the same tick the removal commits.
  despawn(game, {2});
  CHECK(phase_of(game) == simulation::MatchPhase::kEnded);
  CHECK(phase_started_tick_of(game) == 5);
  REQUIRE(outcome_of(game).winning_entity().has_value());
  CHECK(outcome_of(game).winning_entity()->value() == 1);

  advance(game, 2);
  CHECK(phase_of(game) == simulation::MatchPhase::kEnded);

  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);
  CHECK(phase_started_tick_of(game) == 8);
  // The committed outcome survives the restart, because a client reads it for the whole delay.
  CHECK(outcome_of(game).winning_entity()->value() == 1);
  // `running_started_tick` still names this match's start until the next one begins.
  CHECK(running_started_tick_of(game) == 3);
}

TEST_CASE("countdown returns to lobby when the objective can no longer start",
          "[unit][simulation][match_lifecycle_system]") {
  // Losing the ability to start takes precedence over the countdown elapsing, so a field that
  // empties during the countdown returns to the lobby rather than starting a match it cannot hold.
  simulation::GameSimulation game = lifecycle_game({player(1, 50.0, 50.0), player(2, 300.0, 300.0)},
                                                   2, simulation::MatchLifecycleDurations{4, 0});

  advance(game, 1);
  REQUIRE(phase_of(game) == simulation::MatchPhase::kCountdown);

  despawn(game, {2});

  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);
  CHECK(phase_started_tick_of(game) == 2);
  CHECK_FALSE(outcome_of(game).is_decided());
}

TEST_CASE("the machine commits at most one phase transition per tick",
          "[unit][simulation][match_lifecycle_system][determinism]") {
  // With every duration at zero the machine would chain lobby -> countdown -> running -> ended ->
  // lobby inside one tick and never terminate. The one-transition bound is what makes an all-zero
  // configuration total: each phase occupies exactly one tick.
  simulation::GameSimulation game =
      lifecycle_game({player(1, 50.0, 50.0)}, 1, simulation::MatchLifecycleDurations{0, 0});

  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);
  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kCountdown);
  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kRunning);
  advance(game, 1);
  // One alive entity decides the in-test objective, so this is the ended transition.
  CHECK(phase_of(game) == simulation::MatchPhase::kEnded);
  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kLobby);
  advance(game, 1);
  CHECK(phase_of(game) == simulation::MatchPhase::kCountdown);
}

TEST_CASE("an empty field ends a running match as a draw",
          "[unit][simulation][match_lifecycle_system]") {
  simulation::GameSimulation game = lifecycle_game({player(1, 50.0, 50.0), player(2, 300.0, 300.0)},
                                                   2, simulation::MatchLifecycleDurations{0, 0});

  advance(game, 2);
  REQUIRE(phase_of(game) == simulation::MatchPhase::kRunning);

  // Both leave on the same tick, so the objective sees an empty field rather than a survivor.
  despawn(game, {1, 2});

  CHECK(phase_of(game) == simulation::MatchPhase::kEnded);
  CHECK(outcome_of(game).kind() == simulation::MatchOutcomeKind::kDrawn);
  CHECK_FALSE(outcome_of(game).winning_entity().has_value());
}

TEST_CASE("a mode's own lifecycle system runs before the engine's transition",
          "[unit][simulation][match_lifecycle_system][game_mode][order]") {
  // The engine appends its own system last at kLifecycle, so a mode's lifecycle system always
  // observes the phase the previous tick committed. The probe records the phase it saw into every
  // entity's Lifetime cell.
  class PhaseProbeSystem final : public simulation::SimulationSystem {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "phase_probe"; }
    void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
      const auto observed = static_cast<std::uint64_t>(world.match().phase);
      for (const simulation::EntityId entity : world.entities()) {
        world.mutable_store<simulation::Lifetime>().insert_or_assign(
            entity, simulation::Lifetime{observed + 1});
      }
    }
  };

  testing::TestGameMode::Declaration declaration;
  declaration.minimum_players = 1;
  declaration.durations = simulation::MatchLifecycleDurations{0, 0};
  declaration.systems.push_back(testing::staged(simulation::SystemStage::kLifecycle,
                                                std::make_unique<const PhaseProbeSystem>()));
  simulation::GameSimulation game = simulation::GameSimulation::create(
      configuration(), simulation::GameWorld::create({player(1, 50.0, 50.0)}),
      simulation::GameSimulationSetup::of_mode(
          simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(500.0, 500.0)),
          testing::TestGameMode::create(std::move(declaration))));

  game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  // The engine committed lobby -> countdown on this tick; the mode's system saw `lobby` first.
  CHECK(phase_of(game) == simulation::MatchPhase::kCountdown);
  const simulation::WorldSnapshot snapshot = game.snapshot();
  REQUIRE(snapshot.components<simulation::Lifetime>().size() == 1);
  CHECK(snapshot.components<simulation::Lifetime>()[0].value.ticks_remaining ==
        static_cast<std::uint64_t>(simulation::MatchPhase::kLobby) + 1);
}
