#include "royale/zone_elimination_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "physics_body.hpp"
#include "royale/royale_configuration.hpp"
#include "simulation_limits.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr double kZoneRadius = 100.0;
const simulation::Vector2 kZoneCenter = simulation::Vector2::create(480.0, 320.0);

[[nodiscard]] std::unique_ptr<const simulation::SimulationSystem>
elimination_system(const std::uint64_t grace_ticks) {
  gameplay::RoyaleConfiguration::Section section = gameplay::RoyaleConfiguration::default_section();
  // `elimination_grace_seconds` is authored in seconds and converted once, so a test that wants
  // `G` ticks names the duration that produces them rather than reaching past the conversion.
  section.elimination_grace_seconds =
      static_cast<double>(grace_ticks) / static_cast<double>(simulation::kSimulationTicksPerSecond);
  return gameplay::ZoneEliminationSystem::create(gameplay::RoyaleConfiguration::create(section));
}

// A running world holding one zone and one alive entity at `offset_y` from the zone centre.
[[nodiscard]] simulation::GameWorld running_world_with_player_at(const double offset_y) {
  simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(1),
          simulation::PhysicsBody::create(
              simulation::Vector2::create(kZoneCenter.x(), kZoneCenter.y() + offset_y),
              simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(0.0, 0.0)),
          simulation::ControllerId::create(1))});
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_store<simulation::Zone>().insert_or_assign(
      simulation::EntityId::create(9), simulation::Zone{kZoneCenter, kZoneRadius});
  return world;
}

[[nodiscard]] std::optional<std::uint64_t> exposure_of(const simulation::GameWorld& world,
                                                       const simulation::EntityId entity) {
  const simulation::ZoneExposure* exposure = world.store<simulation::ZoneExposure>().find(entity);
  if (exposure == nullptr) {
    return std::nullopt;
  }
  return exposure->outside_ticks;
}

[[nodiscard]] std::vector<simulation::EntityId> eliminated_in(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> eliminated;
  for (const simulation::WorldEvent& event : world.events()) {
    if (const auto* elimination = std::get_if<simulation::EliminationEvent>(&event);
        elimination != nullptr) {
      eliminated.push_back(elimination->entity);
    }
  }
  return eliminated;
}

} // namespace

TEST_CASE("a centre exactly on the boundary is inside", "[unit][gameplay][royale][elimination]") {
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  const std::unique_ptr<const simulation::SimulationSystem> system = elimination_system(1);
  const simulation::EntityId player = simulation::EntityId::create(1);

  // Inclusive, consistent with the baseline contact rule, and inclusive up to the `1e-9 wu`
  // position tolerance the whole contract measures with.
  simulation::GameWorld on_boundary = running_world_with_player_at(kZoneRadius);
  system->apply(on_boundary, harness.context());
  CHECK(exposure_of(on_boundary, player) == std::nullopt);
  CHECK(eliminated_in(on_boundary).empty());

  simulation::GameWorld within_tolerance =
      running_world_with_player_at(kZoneRadius + (simulation::kPositionTolerance / 2.0));
  system->apply(within_tolerance, harness.context());
  CHECK(exposure_of(within_tolerance, player) == std::nullopt);

  simulation::GameWorld outside = running_world_with_player_at(kZoneRadius + 1.0);
  system->apply(outside, harness.context());
  CHECK(exposure_of(outside, player) == 1);
}

TEST_CASE("the increment precedes the test, so a zero grace eliminates on the first outside tick "
          "and never a safe player",
          "[unit][gameplay][royale][elimination]") {
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  const std::unique_ptr<const simulation::SimulationSystem> system = elimination_system(0);
  const simulation::EntityId player = simulation::EntityId::create(1);

  simulation::GameWorld outside = running_world_with_player_at(kZoneRadius + 1.0);
  system->apply(outside, harness.context());
  CHECK(eliminated_in(outside) == std::vector<simulation::EntityId>{player});

  // An entity that is inside is never tested at all, which is why `G = 0` does not eliminate a safe
  // player.
  simulation::GameWorld inside = running_world_with_player_at(0.0);
  system->apply(inside, harness.context());
  CHECK(eliminated_in(inside).empty());
  CHECK(exposure_of(inside, player) == std::nullopt);
}

TEST_CASE("grace accumulates across ticks and is lost the moment a centre is inside again",
          "[unit][gameplay][royale][elimination]") {
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  const std::unique_ptr<const simulation::SimulationSystem> system = elimination_system(3);
  const simulation::EntityId player = simulation::EntityId::create(1);
  simulation::GameWorld world = running_world_with_player_at(kZoneRadius + 1.0);

  system->apply(world, harness.context());
  CHECK(exposure_of(world, player) == 1);
  CHECK(eliminated_in(world).empty());
  system->apply(world, harness.context());
  CHECK(exposure_of(world, player) == 2);
  CHECK(eliminated_in(world).empty());

  // Re-entering resets the counter, and any partial grace is lost rather than banked. The entry is
  // erased rather than set to zero, because an absent `ZoneExposure` already reads as zero and one
  // world state should have one spelling.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      player, world.store<simulation::PhysicsBody>().find(player)->with_position(kZoneCenter));
  system->apply(world, harness.context());
  CHECK(exposure_of(world, player) == std::nullopt);

  // Leaving again starts from one, so the third consecutive outside tick is what eliminates.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      player,
      world.store<simulation::PhysicsBody>().find(player)->with_position(
          simulation::Vector2::create(kZoneCenter.x(), kZoneCenter.y() + kZoneRadius + 1.0)));
  system->apply(world, harness.context());
  CHECK(eliminated_in(world).empty());
  system->apply(world, harness.context());
  CHECK(eliminated_in(world).empty());
  system->apply(world, harness.context());
  CHECK(exposure_of(world, player) == 3);
  CHECK(eliminated_in(world) == std::vector<simulation::EntityId>{player});
}

TEST_CASE("elimination is evaluated only while the committed phase is running",
          "[unit][gameplay][royale][elimination]") {
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  const std::unique_ptr<const simulation::SimulationSystem> system = elimination_system(0);
  const simulation::EntityId player = simulation::EntityId::create(1);

  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kEnded}) {
    INFO("phase " << simulation::match_phase_name(phase));
    simulation::GameWorld world = running_world_with_player_at(kZoneRadius + 1.0);
    world.mutable_match().phase = phase;
    system->apply(world, harness.context());
    CHECK(eliminated_in(world).empty());
    CHECK(exposure_of(world, player) == std::nullopt);
  }
}

TEST_CASE("a running world with no zone is a rejection naming the missing declaration",
          "[unit][gameplay][royale][elimination][validation]") {
  // Reachable only by declaring `zone_elimination` without `zone_shrink` ahead of it. Eliminating
  // nobody because there is nothing to test against would be a match silently played by different
  // rules, so it fails the tick with a named cause.
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  const std::unique_ptr<const simulation::SimulationSystem> system = elimination_system(0);
  simulation::GameWorld world = running_world_with_player_at(kZoneRadius + 1.0);
  world.mutable_store<simulation::Zone>().erase(simulation::EntityId::create(9));

  try {
    system->apply(world, harness.context());
    FAIL("a running world with no zone eliminated nobody instead of failing");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() == gameplay::GameplayValidationCode::kRoyaleZoneAbsent);
    CHECK(error.code() == std::string_view{"GAMEPLAY.ROYALE_ZONE_ABSENT"});
    CHECK(error.context() == "zone_elimination.zone");
  }
}

TEST_CASE("only entities that are alive are evaluated", "[unit][gameplay][royale][elimination]") {
  // A pending joiner owns only the controller link and a static body owns no controller, so neither
  // accumulates exposure however far outside the zone it sits.
  const testing::TickHarness harness{simulation::TickSequence::zero()};
  const std::unique_ptr<const simulation::SimulationSystem> system = elimination_system(0);
  simulation::GameWorld world = running_world_with_player_at(0.0);

  world.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(20),
      simulation::Controllable{simulation::ControllerId::create(20)});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(30),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(20.0, 20.0)));

  system->apply(world, harness.context());
  CHECK(eliminated_in(world).empty());
  CHECK(world.store<simulation::ZoneExposure>().empty());
}
