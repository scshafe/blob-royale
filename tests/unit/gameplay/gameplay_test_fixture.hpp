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
#include "simulation_limits.hpp"
#include "spatial_grid.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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

// canonical: gameplay_tick_harness -- an owning (map, index, context) for a system tested directly.
//
// A `TickContext` holds the map and the spatial index by reference, so a helper that built one from
// temporaries would hand back a dangling value. This owns all three, which is what lets a test call
// one system's `apply` against a hand-built world -- the way to reach a rule that needs a world
// state the engine would never reach on its own, such as `running` with no `Zone`.
class TickHarness final {
public:
  explicit TickHarness(const simulation::TickSequence tick_sequence,
                       simulation::MapDefinition map = gameplay_map(4))
      : configuration_(gameplay_configuration()), map_(std::move(map)),
        indexed_world_(simulation::GameWorld::create({})),
        grid_(simulation::SpatialGrid::create(configuration_, map_.bounds(), indexed_world_)),
        context_(simulation::TickContext::create(tick_sequence, kGameplayFixedDelta, configuration_,
                                                 map_, grid_)) {}

  TickHarness(const TickHarness&) = delete;
  TickHarness(TickHarness&&) = delete;
  TickHarness& operator=(const TickHarness&) = delete;
  TickHarness& operator=(TickHarness&&) = delete;
  ~TickHarness() = default;

  [[nodiscard]] const simulation::TickContext& context() const& noexcept { return context_; }
  [[nodiscard]] const simulation::TickContext& context() const&& = delete;
  [[nodiscard]] const simulation::MapDefinition& map() const& noexcept { return map_; }
  [[nodiscard]] const simulation::MapDefinition& map() const&& = delete;

private:
  simulation::SimulationConfig configuration_;
  simulation::MapDefinition map_;
  simulation::GameWorld indexed_world_;
  simulation::SpatialGrid grid_;
  simulation::TickContext context_;
};

// canonical: gameplay_stepped_game -- drives a declared mode through ticks with a live reservation.
//
// A mode whose systems create entities -- royale's `zone_shrink` creates the zone entity on the
// first tick it observes none -- cannot be driven with `InputBatch::empty()`, because the no-input
// tick carries no reservation and a tick that was handed nothing may create nothing
// (`entity_id_reservation.hpp`). This owns the cursor so a test writes commands and never
// arithmetic.
//
// **The reservation policy is the same one `tests/fixtures/replay_fixture.hpp` states and is
// canonical there**: every tick receives a contiguous block of `spawn_count + 1` ids from a
// monotonic cursor that advances by the same width, so an entity id is a deterministic function of
// the command sequence alone and every tick has room for the one entity a system may create. A
// hand-built test and a replay fixture therefore number entities identically.
class SteppedGame final {
public:
  static constexpr std::uint64_t kSystemCreatedEntityHeadroom = 1;

  explicit SteppedGame(simulation::GameSimulation game) : game_(std::move(game)) {}

  SteppedGame(const SteppedGame&) = delete;
  SteppedGame(SteppedGame&&) noexcept = default;
  SteppedGame& operator=(const SteppedGame&) = delete;
  SteppedGame& operator=(SteppedGame&&) = delete;
  ~SteppedGame() = default;

  [[nodiscard]] const simulation::GameSimulation& game() const& noexcept { return game_; }
  [[nodiscard]] const simulation::GameSimulation& game() const&& = delete;

  // The id the next entity brought into existence will take, which is the lowest id of the next
  // tick's block.
  [[nodiscard]] simulation::EntityId next_entity_id() const {
    return simulation::EntityId::create(cursor_);
  }

  // Steps one tick with these commands and returns the committed snapshot.
  simulation::WorldSnapshot step(std::vector<simulation::Command> commands) {
    std::uint64_t spawn_count = 0;
    for (const simulation::Command& command : commands) {
      if (std::holds_alternative<simulation::SpawnCommand>(command)) {
        ++spawn_count;
      }
    }
    const std::uint64_t width = spawn_count + kSystemCreatedEntityHeadroom;
    const simulation::InputBatch batch = simulation::InputBatch::create(
        std::move(commands), game_.accepted_command_kinds(),
        simulation::EntityIdReservation::create(simulation::EntityId::create(cursor_), width));
    cursor_ += width;
    game_.step(kGameplayFixedDelta, batch);
    return game_.snapshot();
  }

  simulation::WorldSnapshot step() { return step({}); }

  // Steps `tick_count` command-free ticks and returns the last committed snapshot.
  simulation::WorldSnapshot advance(const std::size_t tick_count) {
    simulation::WorldSnapshot snapshot = game_.snapshot();
    for (std::size_t tick = 0; tick < tick_count; ++tick) {
      snapshot = step();
    }
    return snapshot;
  }

private:
  simulation::GameSimulation game_;
  std::uint64_t cursor_{simulation::kMinimumEntityId};
};

} // namespace blob_royale::testing

#endif
