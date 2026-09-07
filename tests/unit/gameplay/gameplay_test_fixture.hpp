#ifndef BLOB_ROYALE_TESTS_UNIT_GAMEPLAY_GAMEPLAY_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_GAMEPLAY_GAMEPLAY_TEST_FIXTURE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_mode.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::testing {

namespace simulation = blob_royale::simulation;

// canonical: gameplay_test_fixture -- the world every gameplay test drives a mode in.
//
// A gameplay test constructs a real `GameSimulation` from a real map and the mode under test and
// then drives it with real `InputBatch` values, because a mode's whole contract is what the engine
// does with its seven declarations; a test that called the declarations directly would prove the
// mode compiles and nothing else.

// The accepted geometry of every simulation fixture in the tree, so a gameplay horizon is
// comparable with a kernel one.
[[nodiscard]] inline simulation::SimulationConfig gameplay_configuration() {
  return simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 12, 8);
}

// A map with `point_count` spawn markers spaced far enough apart that every one of them is
// simultaneously seatable at the fixture radius, which is what makes a seating test observe the
// policy's rotation rather than the engine's occupancy test.
[[nodiscard]] inline simulation::MapDefinition
gameplay_map(const std::size_t point_count, const std::string& name = "gameplay_map") {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.reserve(point_count);
  for (std::size_t index = 0; index < point_count; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        simulation::Vector2::create(100.0 * static_cast<double>(index + 1), 320.0)));
  }
  return simulation::MapDefinition::create(name, simulation::ArenaBounds::create(960.0, 640.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// A map with no `spawn` marker at all, which is the one map a free-play mode has to reject.
[[nodiscard]] inline simulation::MapDefinition gameplay_map_without_spawn_points() {
  return gameplay_map(0, "gameplay_map_without_spawn_points");
}

[[nodiscard]] inline simulation::GameSimulation
gameplay_simulation(std::unique_ptr<const simulation::GameMode> mode,
                    simulation::MapDefinition map) {
  const simulation::SimulationConfig configuration = gameplay_configuration();
  simulation::GameWorld world = simulation::GameWorld::create(configuration, map, 0);
  return simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::of_mode(std::move(map), std::move(mode)));
}

[[nodiscard]] inline simulation::Command spawn_command(const std::uint64_t controller) {
  return simulation::Command{
      simulation::SpawnCommand{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] inline simulation::Command thrust_command(const std::uint64_t entity, const double x,
                                                        const double y) {
  return simulation::Command{simulation::ThrustCommand{simulation::EntityId::create(entity),
                                                       simulation::Vector2::create(x, y)}};
}

// One tick's input for a mode under test: the mode's own accepted set, and a reservation wide
// enough for every spawn in the batch. The reservation opens above the ids a map's static bodies
// took, which `GameWorld::create(configuration, map, seed)` numbers from kMinimumEntityId.
[[nodiscard]] inline simulation::InputBatch
gameplay_batch(const simulation::GameSimulation& simulation,
               std::vector<simulation::Command> commands,
               const std::uint64_t first_created_entity_id = 1, const std::uint64_t reserved = 8) {
  return simulation::InputBatch::create(
      std::move(commands), simulation.accepted_command_kinds(),
      simulation::EntityIdReservation::create(simulation::EntityId::create(first_created_entity_id),
                                              reserved));
}

// The publication counts, bound the same way. A snapshot's spans are deleted on an rvalue for the
// same reason its match section is.
[[nodiscard]] inline std::size_t published_player_count(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.players().size();
}

[[nodiscard]] inline std::size_t published_entity_count(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.entities().size();
}

// The match section reads through accessors that are deleted on an rvalue snapshot -- a snapshot is
// an owning value and a reference into a temporary would dangle -- so these bind one and return the
// scalar a test asserts on.
[[nodiscard]] inline simulation::MatchPhase
committed_phase(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().phase();
}

[[nodiscard]] inline simulation::MatchOutcome
committed_outcome(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().outcome();
}

[[nodiscard]] inline simulation::TickSequence
committed_running_started_tick(const simulation::GameSimulation& game) {
  const simulation::WorldSnapshot snapshot = game.snapshot();
  return snapshot.match().running_started_tick();
}

// The body one snapshot published for one entity, or nullopt when it published none.
[[nodiscard]] inline std::optional<simulation::PhysicsBody>
published_body(const simulation::WorldSnapshot& snapshot, const std::uint64_t entity) {
  for (const simulation::ComponentStore<simulation::PhysicsBody>::Entry& entry :
       snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity.value() == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

inline constexpr simulation::FixedDelta kGameplayFixedDelta = simulation::FixedDelta::canonical();

} // namespace blob_royale::testing

#endif
