#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/spawn_command.hpp"
#include "component_store.hpp"
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
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "simulation_test_fixture.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_policy.hpp"
#include "spawn_system.hpp"
#include "system_pipeline.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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

[[nodiscard]] simulation::Command spawn_command(const simulation::ControllerId::Value controller) {
  return simulation::Command{
      simulation::SpawnCommand{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] simulation::InputBatch reserved_batch(std::vector<simulation::Command> commands,
                                                    const simulation::EntityId::Value first,
                                                    const std::uint64_t count) {
  return simulation::InputBatch::create(
      std::move(commands), simulation::CommandKindMask::all(),
      simulation::EntityIdReservation::create(simulation::EntityId::create(first), count));
}

[[nodiscard]] simulation::GameSimulation
seating_game(simulation::MapDefinition map, testing::TestGameMode::Declaration declaration,
             std::vector<simulation::GameWorld::EntitySeed> seeds = {}) {
  return simulation::GameSimulation::create(
      configuration(), simulation::GameWorld::create(std::move(seeds)),
      simulation::GameSimulationSetup::of_mode(
          std::move(map), testing::TestGameMode::create(std::move(declaration))));
}

[[nodiscard]] const simulation::PhysicsBody* body_of(const simulation::WorldSnapshot& snapshot,
                                                     const simulation::EntityId::Value id) {
  for (const simulation::ComponentStore<simulation::PhysicsBody>::Entry& entry :
       snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == simulation::EntityId::create(id)) {
      return &entry.value;
    }
  }
  return nullptr;
}

// canonical: misbehaving_spawn_policy_test_double -- a policy that returns an index it may not.
//
// The engine treats an out-of-range or already-taken index as a mode defect and fails the tick,
// because either would seat a body the world cannot represent or two entities in contact.
class FixedIndexSpawnPolicy final : public simulation::SpawnPolicy {
public:
  explicit FixedIndexSpawnPolicy(const std::size_t index) noexcept : index_(index) {}

  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld&, const simulation::TickContext&,
                     simulation::EntityId, std::size_t, std::span<const bool>) const override {
    return index_;
  }

private:
  std::size_t index_;
};

// A mode that declares a caller-supplied spawn policy and nothing else of interest, so a seating
// failure can be provoked without any other declaration changing.
class FixedIndexSpawnMode final : public simulation::GameMode {
public:
  explicit FixedIndexSpawnMode(const std::size_t index) noexcept : index_(index) {}

  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create(const std::size_t index) {
    return std::make_unique<const FixedIndexSpawnMode>(index);
  }

  [[nodiscard]] std::string_view name() const noexcept override { return "fixed_index_mode"; }
  [[nodiscard]] simulation::SystemPipeline systems() const override {
    return simulation::SystemPipeline::empty();
  }
  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::built_in();
  }
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::all();
  }
  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const FixedIndexSpawnPolicy>(index_);
  }
  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const testing::TestMatchObjective>(
        1'000, simulation::MatchLifecycleDurations{});
  }
  void validate_map(const simulation::MapDefinition&) const override {}

private:
  std::size_t index_;
};

} // namespace

TEST_CASE("the spawn system seats consecutive joiners at consecutive points",
          "[unit][simulation][spawn_system][game_mode]") {
  // The rotation counter is world state and advances to one past the index a seating used, so a
  // policy that probes forward from it spreads consecutive joiners around the map's points instead
  // of stacking them on the first free one.
  simulation::GameSimulation game =
      seating_game(testing::spawn_point_map(3), testing::TestGameMode::Declaration{});

  game.step(simulation::FixedDelta::canonical(),
            reserved_batch({spawn_command(7), spawn_command(8)}, 100, 4));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  REQUIRE(snapshot.entities().size() == 2);
  REQUIRE(body_of(snapshot, 100) != nullptr);
  REQUIRE(body_of(snapshot, 101) != nullptr);
  CHECK(body_of(snapshot, 100)->position().x() == Catch::Approx(50.0));
  CHECK(body_of(snapshot, 101)->position().x() == Catch::Approx(100.0));
}

TEST_CASE(
    "a seated body carries the configured player radius rather than the undeclared placeholder",
    "[unit][simulation][spawn_system][physics_body]") {
  // Every accepted phase measures with `SimulationConfig::player_radius()`, so that is the only
  // radius a seated body can truthfully publish, and `physics-body-component.schema.json` requires
  // a positive one: seating `PhysicsBody::kUndeclaredRadius` made every live match unencodable.
  simulation::GameSimulation game =
      seating_game(testing::spawn_point_map(3), testing::TestGameMode::Declaration{});

  game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(7)}, 100, 2));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  REQUIRE(body_of(snapshot, 100) != nullptr);
  CHECK(body_of(snapshot, 100)->radius() == Catch::Approx(configuration().player_radius()));
  CHECK(body_of(snapshot, 100)->ground_attachment() == simulation::GroundAttachment::kGroundBound);
}

TEST_CASE("seating refreshes nearby marker occupancy after each simultaneous joiner",
          "[unit][simulation][spawn_system]") {
  auto map = simulation::MapDefinition::create(
      "nearby_seats", simulation::ArenaBounds::create(500, 500), {},
      {simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(50, 50)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(65, 50)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(100, 50))},
      simulation::MapMetadata::none());
  auto game = seating_game(std::move(map), testing::TestGameMode::Declaration{});
  game.step(simulation::FixedDelta::canonical(),
            reserved_batch({spawn_command(7), spawn_command(8), spawn_command(9)}, 100, 4));
  const auto snapshot = game.snapshot();
  REQUIRE(body_of(snapshot, 100) != nullptr);
  REQUIRE(body_of(snapshot, 101) != nullptr);
  CHECK(body_of(snapshot, 100)->position() == simulation::Vector2::create(50, 50));
  CHECK(body_of(snapshot, 101)->position() == simulation::Vector2::create(100, 50));
  CHECK(body_of(snapshot, 102) == nullptr);
}

TEST_CASE("the spawn rotation counter survives the tick that advanced it",
          "[unit][simulation][spawn_system][match_state]") {
  simulation::GameSimulation game =
      seating_game(testing::spawn_point_map(3), testing::TestGameMode::Declaration{});

  game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(7)}, 100, 2));
  game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(8)}, 200, 2));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  REQUIRE(body_of(snapshot, 200) != nullptr);
  // Point 0 is taken and the counter already names point 1, so the second joiner lands there.
  CHECK(body_of(snapshot, 200)->position().x() == Catch::Approx(100.0));
}

TEST_CASE("a spawn point occupied by a live body is not offered as free",
          "[unit][simulation][spawn_system]") {
  // A point is occupied when a live body's centre lies within contact range of it, which is the
  // baseline pair predicate, so two entities are never seated in contact.
  simulation::GameSimulation game =
      seating_game(testing::spawn_point_map(2), testing::TestGameMode::Declaration{},
                   {simulation::GameWorld::EntitySeed::create(
                       simulation::EntityId::create(1),
                       simulation::PhysicsBody::create(simulation::Vector2::create(50.0, 50.0),
                                                       simulation::Vector2::create(0.0, 0.0),
                                                       simulation::Vector2::create(0.0, 0.0)))});

  game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(7)}, 100, 2));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  REQUIRE(body_of(snapshot, 100) != nullptr);
  CHECK(body_of(snapshot, 100)->position().x() == Catch::Approx(100.0));
}

TEST_CASE("a full ring defers the entity, which is offered again on a later tick",
          "[unit][simulation][spawn_system]") {
  // A deferral is a first-class answer, not a failure: the entity exists carrying its controller
  // link and waits. Nothing frees a point while positions are frozen, so it loses nothing.
  simulation::GameSimulation game =
      seating_game(testing::spawn_point_map(1), testing::TestGameMode::Declaration{});

  game.step(simulation::FixedDelta::canonical(),
            reserved_batch({spawn_command(7), spawn_command(8)}, 100, 4));

  const simulation::WorldSnapshot after_first = game.snapshot();
  REQUIRE(after_first.entities().size() == 2);
  CHECK(after_first.components<simulation::PhysicsBody>().size() == 1);
  CHECK(body_of(after_first, 100) != nullptr);
  CHECK(body_of(after_first, 101) == nullptr);

  game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());

  // Still deferred: the only point is still taken, and the entity is still pending.
  const simulation::WorldSnapshot after_second = game.snapshot();
  CHECK(after_second.components<simulation::PhysicsBody>().size() == 1);
  CHECK(after_second.components<simulation::Controllable>().size() == 2);
}

TEST_CASE("a policy that defers outside its declared phases holds joiners pending",
          "[unit][simulation][spawn_system][match_lifecycle_system]") {
  // This is royale's closed-field rule expressed through the seam rather than in the kernel: a
  // joiner who arrives mid-match waits for the next lobby.
  testing::TestGameMode::Declaration declaration;
  declaration.minimum_players = 1;
  declaration.seating_phases = {simulation::MatchPhase::kLobby};
  simulation::GameSimulation game =
      seating_game(testing::spawn_point_map(3), std::move(declaration));

  // Tick 1 commits lobby -> countdown, and the policy is consulted at phase 0 while the committed
  // phase is still lobby, so this joiner is seated.
  game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(7)}, 100, 2));
  const simulation::WorldSnapshot seated = game.snapshot();
  REQUIRE(body_of(seated, 100) != nullptr);
  REQUIRE(seated.match().phase() == simulation::MatchPhase::kCountdown);

  game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(8)}, 200, 2));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  CHECK(body_of(snapshot, 200) == nullptr);
  CHECK(snapshot.components<simulation::Controllable>().size() == 2);
}

TEST_CASE("a policy that chooses an out-of-range spawn point fails the tick",
          "[unit][simulation][spawn_system][validation]") {
  simulation::GameSimulation game = simulation::GameSimulation::create(
      configuration(), simulation::GameWorld::create({}),
      simulation::GameSimulationSetup::of_mode(testing::spawn_point_map(2),
                                               FixedIndexSpawnMode::create(9)));
  const simulation::WorldSnapshot before = game.snapshot();

  CHECK_THROWS_AS(
      game.step(simulation::FixedDelta::canonical(), reserved_batch({spawn_command(7)}, 100, 2)),
      simulation::SimulationValidationError);

  CHECK(game.snapshot() == before);
  CHECK(game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("a policy that chooses an occupied spawn point fails the tick",
          "[unit][simulation][spawn_system][validation]") {
  simulation::GameSimulation game = simulation::GameSimulation::create(
      configuration(), simulation::GameWorld::create({}),
      simulation::GameSimulationSetup::of_mode(testing::spawn_point_map(2),
                                               FixedIndexSpawnMode::create(0)));

  CHECK_THROWS_AS(game.step(simulation::FixedDelta::canonical(),
                            reserved_batch({spawn_command(7), spawn_command(8)}, 100, 4)),
                  simulation::SimulationValidationError);

  CHECK(game.tick_sequence() == simulation::TickSequence::zero());
}

TEST_CASE("require_spawn_points_are_seatable rejects a point the configured disc cannot occupy",
          "[unit][simulation][spawn_system][map_definition][validation]") {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.push_back(
      simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(2.0, 50.0)));
  const simulation::MapDefinition edge_map = simulation::MapDefinition::create(
      "edge_spawn_map", simulation::ArenaBounds::create(100.0, 100.0), {}, std::move(markers),
      simulation::MapMetadata::none());

  CHECK_THROWS_AS(
      simulation::require_spawn_points_are_seatable(
          simulation::SimulationConfig::create(
              100.0, 100.0, 10.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 4, 4),
          edge_map),
      simulation::SimulationValidationError);
  CHECK_NOTHROW(simulation::require_spawn_points_are_seatable(
      simulation::SimulationConfig::create(
          100.0, 100.0, 1.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 4, 4),
      edge_map));
}
