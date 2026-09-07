#ifndef BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_CONTROLLERS_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_CONTROLLERS_TEST_FIXTURE_HPP

#include "../gameplay/gameplay_test_fixture.hpp"

#include "command_mailbox.hpp"
#include "command_registry.hpp"
#include "command_sink.hpp"
#include "controller_directory.hpp"
#include "controller_host.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_allocator.hpp"
#include "game_simulation.hpp"
#include "map_definition.hpp"
#include "observation.hpp"
#include "sandbox/sandbox_mode.hpp"
#include "simulation_limits.hpp"
#include "snapshot_publication.hpp"
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

namespace controllers = blob_royale::controllers;
namespace gameplay = blob_royale::gameplay;
namespace runtime = blob_royale::runtime;

// canonical: controllers_test_fixture -- the published world and the two capabilities a controller
// test hands a command source.
//
// A controllers test drives **real** values end to end: a real `SandboxMode` in a real
// `GameSimulation` for the world, and the real `CommandMailbox`, `ControllerDirectory`,
// `EntityIdAllocator`, and `CommandSink` for the write path. The world half reuses
// `tests/unit/gameplay/gameplay_test_fixture.hpp` -- its `SteppedGame` owns the one reservation
// policy `tests/fixtures/replay_fixture.hpp` states, and a second stepper here would be a second
// copy of it.
//
// What this file adds is only what is new: a map whose spawn markers sit exactly where a test wants
// them, and the bundle of runtime capabilities a `ControllerHost` is constructed from.

// A map with a spawn marker at each requested position, in that order. Sandbox seats the entity
// awaiting a body with the lowest `EntityId` at the first free marker and advances the rotation
// counter, so with one spawn per controller in one tick, marker `i` seats the `i`-th lowest
// `ControllerId` (`src/gameplay/sandbox/next_free_spawn_point_policy.hpp`;
// `src/simulation/spawn_system.hpp`).
[[nodiscard]] inline simulation::MapDefinition
controllers_map(const std::vector<simulation::Vector2>& spawn_positions,
                const std::string& name = "controllers_map") {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.reserve(spawn_positions.size());
  for (const simulation::Vector2& position : spawn_positions) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(position));
  }
  return simulation::MapDefinition::create(name, simulation::ArenaBounds::create(960.0, 640.0), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// Four widely spaced markers, so every one of them is simultaneously seatable at the fixture
// radius and a seating test observes the policy rather than the engine's occupancy test.
[[nodiscard]] inline simulation::MapDefinition controllers_map_of(const std::size_t point_count) {
  std::vector<simulation::Vector2> positions;
  positions.reserve(point_count);
  for (std::size_t index = 0; index < point_count; ++index) {
    positions.push_back(simulation::Vector2::create(100.0 * static_cast<double>(index + 1), 320.0));
  }
  return controllers_map(positions);
}

// A sandbox game on this map with `controller_count` controllers seated, drawn from the ascending
// `ControllerId` block `CommandSink::open_session` issues from.
[[nodiscard]] inline SteppedGame seated_sandbox(simulation::MapDefinition map,
                                                const std::size_t controller_count) {
  SteppedGame stepped(gameplay_simulation(gameplay::SandboxMode::create(), std::move(map)));
  std::vector<simulation::Command> spawns;
  spawns.reserve(controller_count);
  for (std::size_t index = 0; index < controller_count; ++index) {
    spawns.push_back(spawn_command(simulation::kMinimumControllerId + index));
  }
  static_cast<void>(stepped.step(std::move(spawns)));
  return stepped;
}

// canonical: controllers_fixture -- one published world plus the exact capability pair a command
// source holds.
//
// The publication is **static**: `SnapshotPublication::publish` is private to `SimulationRuntime`,
// so a test that needs the world to advance mid-pass builds a real runtime instead
// (`controller_host_tests.cpp`). Everything that does not need a moving world reads one committed
// snapshot, which is also what a network session reads between two pushed frames.
class ControllersFixture final {
public:
  // Above every entity id these fixtures use, so the sink's despawn check against the allocator
  // cursor never refuses a test's command for a reason the test did not intend.
  static constexpr std::uint64_t kIssuedEntityIdCeiling = 1'024;

  explicit ControllersFixture(simulation::MapDefinition map, const std::size_t seated_count)
      : stepped_(seated_sandbox(std::move(map), seated_count)),
        publication_(stepped_.game().snapshot()),
        mailbox_(stepped_.game().accepted_command_kinds()),
        allocator_(runtime::EntityIdAllocator::create(
            simulation::EntityId::create(kIssuedEntityIdCeiling))),
        sink_(mailbox_, directory_, allocator_), host_(publication_, sink_) {
    // One open session per seated controller, so the ids the sink issues are the ids the world
    // already links its bodies to.
    for (std::size_t index = 0; index < seated_count; ++index) {
      static_cast<void>(sink_.open_session("session", "fixture"));
    }
  }

  ControllersFixture(const ControllersFixture&) = delete;
  ControllersFixture(ControllersFixture&&) = delete;
  ControllersFixture& operator=(const ControllersFixture&) = delete;
  ControllersFixture& operator=(ControllersFixture&&) = delete;
  ~ControllersFixture() = default;

  [[nodiscard]] const simulation::GameSimulation& game() const noexcept { return stepped_.game(); }
  [[nodiscard]] const runtime::SnapshotPublication& publication() const noexcept {
    return publication_;
  }
  [[nodiscard]] runtime::CommandMailbox& mailbox() noexcept { return mailbox_; }
  [[nodiscard]] runtime::ControllerDirectory& directory() noexcept { return directory_; }
  [[nodiscard]] runtime::CommandSink& sink() noexcept { return sink_; }
  [[nodiscard]] controllers::ControllerHost& host() noexcept { return host_; }

  [[nodiscard]] std::shared_ptr<const simulation::WorldSnapshot> snapshot() const {
    return publication_.latest();
  }

  // Commits one tick from these commands, exactly as the runtime worker does from a mailbox drain.
  // The publication does not follow -- only `SimulationRuntime` may publish -- so a test that
  // commits reads the new world through `game().snapshot()` or `observation_at_committed`.
  simulation::WorldSnapshot commit(std::vector<simulation::Command> commands) {
    return stepped_.step(std::move(commands));
  }

  simulation::WorldSnapshot commit() { return commit({}); }

  // One controller's view of the **committed** world, which `commit` advances and the fixture's
  // static publication does not.
  [[nodiscard]] controllers::Observation
  observation_at_committed(const std::uint64_t controller) const {
    return controllers::Observation::create(
        std::make_shared<const simulation::WorldSnapshot>(stepped_.game().snapshot()),
        simulation::ControllerId::create(controller));
  }

  // One controller's view of the committed world, built exactly as the host builds it.
  [[nodiscard]] controllers::Observation observation_for(const std::uint64_t controller) const {
    return controllers::Observation::create(publication_.latest(),
                                            simulation::ControllerId::create(controller));
  }

  // The body this controller drives in the committed world, or nullopt when it drives none.
  [[nodiscard]] std::optional<simulation::EntityId>
  entity_of(const std::uint64_t controller) const {
    return observation_for(controller).entity();
  }

private:
  SteppedGame stepped_;
  runtime::SnapshotPublication publication_;
  runtime::CommandMailbox mailbox_;
  runtime::ControllerDirectory directory_;
  runtime::EntityIdAllocator allocator_;
  runtime::CommandSink sink_;
  controllers::ControllerHost host_;
};

} // namespace blob_royale::testing

#endif
