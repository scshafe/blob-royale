#include "royale/placement_recorder_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/zone_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/despawn_event.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics_body.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kRecordedTick = 42;

[[nodiscard]] simulation::GameWorld
world_with_players(const std::uint64_t player_count, const simulation::MatchPhase phase,
                   const simulation::MatchPhase previous_phase) {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (std::uint64_t index = 0; index < player_count; ++index) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        simulation::EntityId::create(index + 1),
        simulation::PhysicsBody::create(
            simulation::Vector2::create(60.0 * static_cast<double>(index + 1), 60.0),
            simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(0.0, 0.0)),
        simulation::ControllerId::create(index + 1)));
  }
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  world.mutable_match().phase = phase;
  // The engine field is what steps 1 and 3 read. Royale's block starts at its default so a recorder
  // that still read the block's mirror would see `lobby` there and neither clear nor wipe.
  world.mutable_match().previous_phase = previous_phase;
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{};
  // The zone entity owns neither a body nor a controller, so it is never wiped and never ranked.
  world.mutable_store<simulation::Zone>().insert_or_assign(
      simulation::EntityId::create(90),
      simulation::Zone{simulation::Vector2::create(480.0, 320.0), 500.0});
  return world;
}

[[nodiscard]] simulation::RoyalePlacementsModeState
mode_state_of(const simulation::GameWorld& world) {
  const auto* held = std::get_if<simulation::RoyalePlacementsModeState>(&world.match().mode_state);
  REQUIRE(held != nullptr);
  return *held;
}

[[nodiscard]] std::vector<simulation::EntityId> despawned_in(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> despawned;
  for (const simulation::WorldEvent& event : world.events()) {
    if (const auto* despawn = std::get_if<simulation::DespawnEvent>(&event); despawn != nullptr) {
      despawned.push_back(despawn->entity);
    }
  }
  return despawned;
}

} // namespace

TEST_CASE("one tick's eliminations share one placement computed after they leave the roster",
          "[unit][gameplay][royale][placement]") {
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();
  simulation::GameWorld world =
      world_with_players(4, simulation::MatchPhase::kRunning, simulation::MatchPhase::kRunning);

  world.emit(simulation::EliminationEvent{simulation::EntityId::create(2)});
  world.emit(simulation::EliminationEvent{simulation::EntityId::create(4)});
  recorder->apply(world, harness.context());

  // `alive_after` is 2, so both share placement 3: placement is computed once over the whole set.
  const std::vector<simulation::RoyalePlacement> ranking = mode_state_of(world).placements;
  REQUIRE(ranking.size() == 2);
  CHECK(ranking[0].entity == simulation::EntityId::create(2));
  CHECK(ranking[1].entity == simulation::EntityId::create(4));
  CHECK(ranking[0].placement == 3);
  CHECK(ranking[1].placement == 3);
  CHECK(ranking[0].elimination_tick.value() == kRecordedTick);
  CHECK(ranking[1].elimination_tick.value() == kRecordedTick);
  // The controller the entity was driving is recorded at the instant it is eliminated, because
  // this same step destroys the entity and no later reader can recover the link.
  CHECK(ranking[0].controller == simulation::ControllerId::create(2));
  CHECK(ranking[1].controller == simulation::ControllerId::create(4));

  // The entities are destroyed here rather than at the commit, so the engine's lifecycle system
  // observes the alive count after this tick's eliminations have left the roster. A DespawnEvent is
  // emitted for each so every removal is announced at one place.
  CHECK_FALSE(world.contains(simulation::EntityId::create(2)));
  CHECK_FALSE(world.contains(simulation::EntityId::create(4)));
  CHECK(despawned_in(world) == std::vector<simulation::EntityId>{simulation::EntityId::create(2),
                                                                 simulation::EntityId::create(4)});
}

TEST_CASE("a mutual finish gives every final entity the shared placement one",
          "[unit][gameplay][royale][placement]") {
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();
  simulation::GameWorld world =
      world_with_players(2, simulation::MatchPhase::kRunning, simulation::MatchPhase::kRunning);

  world.emit(simulation::EliminationEvent{simulation::EntityId::create(1)});
  world.emit(simulation::EliminationEvent{simulation::EntityId::create(2)});
  recorder->apply(world, harness.context());

  const std::vector<simulation::RoyalePlacement> ranking = mode_state_of(world).placements;
  REQUIRE(ranking.size() == 2);
  CHECK(ranking[0].placement == 1);
  CHECK(ranking[1].placement == 1);
}

TEST_CASE("the placement list is cleared on the first running tick and never before",
          "[unit][gameplay][royale][placement]") {
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();

  // The list survives the whole `ended` phase and the `lobby` and `countdown` that follow, so a
  // client has the finished ranking on screen for the entire restart delay.
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kEnded, simulation::MatchPhase::kLobby,
        simulation::MatchPhase::kCountdown}) {
    INFO("phase " << simulation::match_phase_name(phase));
    simulation::GameWorld world = world_with_players(2, phase, simulation::MatchPhase::kEnded);
    world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{
        {simulation::RoyalePlacement{simulation::EntityId::create(7),
                                     simulation::ControllerId::create(7), 2,
                                     simulation::TickSequence::create(9)}},
        phase == simulation::MatchPhase::kLobby ? simulation::MatchPhase::kCountdown
                                                : simulation::MatchPhase::kRunning};
    recorder->apply(world, harness.context());
    CHECK(mode_state_of(world).placements.size() == 1);
  }

  // And it is cleared on the tick a match starts, which is also the first tick that can append.
  simulation::GameWorld starting =
      world_with_players(2, simulation::MatchPhase::kRunning, simulation::MatchPhase::kCountdown);
  starting.mutable_match().mode_state = simulation::RoyalePlacementsModeState{
      {simulation::RoyalePlacement{simulation::EntityId::create(7),
                                   simulation::ControllerId::create(7), 2,
                                   simulation::TickSequence::create(9)}},
      simulation::MatchPhase::kCountdown};
  recorder->apply(starting, harness.context());
  CHECK(mode_state_of(starting).placements.empty());
}

TEST_CASE("the previous match's ranking is cleared before this match's first placements are "
          "appended",
          "[unit][gameplay][royale][placement]") {
  // The one case where two steps coincide: under a zero shrink and a zero grace, a match's first
  // running tick both starts the match and eliminates. Step 1 running first is what guarantees the
  // previous ranking is gone before this one is appended rather than after.
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();
  simulation::GameWorld world =
      world_with_players(3, simulation::MatchPhase::kRunning, simulation::MatchPhase::kCountdown);
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{
      {simulation::RoyalePlacement{simulation::EntityId::create(7),
                                   simulation::ControllerId::create(7), 2,
                                   simulation::TickSequence::create(9)}},
      simulation::MatchPhase::kCountdown};

  world.emit(simulation::EliminationEvent{simulation::EntityId::create(1)});
  recorder->apply(world, harness.context());

  const std::vector<simulation::RoyalePlacement> ranking = mode_state_of(world).placements;
  REQUIRE(ranking.size() == 1);
  CHECK(ranking[0].entity == simulation::EntityId::create(1));
  CHECK(ranking[0].placement == 3);
}

TEST_CASE("the restart wipe clears the arena on the one lobby tick after a match ended",
          "[unit][gameplay][royale][placement]") {
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();
  simulation::GameWorld world =
      world_with_players(2, simulation::MatchPhase::kLobby, simulation::MatchPhase::kEnded);

  recorder->apply(world, harness.context());

  // Without the wipe the winner would carry its position, velocity, and stored acceleration into
  // the next match and would never be re-seated on the ring.
  CHECK_FALSE(world.contains(simulation::EntityId::create(1)));
  CHECK_FALSE(world.contains(simulation::EntityId::create(2)));
  // The zone entity owns neither a body nor a controller, so it survives every match.
  CHECK(world.contains(simulation::EntityId::create(90)));
}

TEST_CASE("only an ended-to-lobby transition wipes, and every other lobby tick leaves the roster "
          "alone",
          "[unit][gameplay][royale][placement]") {
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();

  // `previous_phase` is what distinguishes `ended -> lobby` from `countdown -> lobby`, which
  // changes nothing but the phase and its start tick.
  for (const simulation::MatchPhase previous :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kRunning}) {
    INFO("previous phase " << simulation::match_phase_name(previous));
    simulation::GameWorld world = world_with_players(2, simulation::MatchPhase::kLobby, previous);
    recorder->apply(world, harness.context());
    CHECK(world.contains(simulation::EntityId::create(1)));
    CHECK(world.contains(simulation::EntityId::create(2)));
  }
}

TEST_CASE("the phase this system observed is what it records as previous_phase",
          "[unit][gameplay][royale][placement][mode_state]") {
  // Step 4 records the phase `MatchState` holds while this system runs, which is the phase the
  // previous tick committed; the engine's transition for this tick has not run yet.
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();

  for (const simulation::MatchPhase phase : simulation::kMatchPhases) {
    INFO("phase " << simulation::match_phase_name(phase));
    simulation::GameWorld world = world_with_players(2, phase, simulation::MatchPhase::kLobby);
    recorder->apply(world, harness.context());
    CHECK(mode_state_of(world).previous_phase == phase);
  }
}

TEST_CASE("a world carrying another mode's state reads as a match that has never run",
          "[unit][gameplay][royale][placement][mode_state]") {
  const testing::TickHarness harness{simulation::TickSequence::create(kRecordedTick)};
  const std::unique_ptr<const simulation::SimulationSystem> recorder =
      gameplay::PlacementRecorderSystem::create();
  simulation::GameWorld world =
      world_with_players(2, simulation::MatchPhase::kLobby, simulation::MatchPhase::kLobby);
  world.mutable_match().mode_state = simulation::NoModeState{};

  recorder->apply(world, harness.context());

  CHECK(mode_state_of(world).placements.empty());
  CHECK(mode_state_of(world).previous_phase == simulation::MatchPhase::kLobby);
  CHECK(world.contains(simulation::EntityId::create(1)));
}
