#ifndef BLOB_ROYALE_TESTING_HILL_ROAM_MOVEMENT_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_HILL_ROAM_MOVEMENT_FIXTURE_HPP

#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/score_component.hpp"
#include "gameplay_test_fixture.hpp"
#include "king_of_the_hill/hill_movement_system.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "terrain_definition.hpp"

namespace blob_royale::testing::hill_roam_movement_fixture {

using Configuration = gameplay::KingOfTheHillConfiguration;
inline constexpr std::uint64_t kSeed = 2026;
inline constexpr std::uint64_t kDifferentSeed = 2027;
inline constexpr std::uint64_t kFirstTick = 1;
inline constexpr std::uint64_t kRetargetTicks = 2;
inline constexpr std::uint64_t kContinuationTicks = 12;
inline constexpr std::uint64_t kBoundaryRetargetTick = 4;
inline constexpr std::uint64_t kFarRetargetTick = 1000;
inline constexpr std::uint64_t kCrossingTicks = 400;
inline constexpr std::uint64_t kScoringPlayer = 1;
inline constexpr std::uint64_t kLifecycleEndTick = 6;
inline constexpr std::size_t kLifecycleSeatCount = 2;
inline constexpr std::uint64_t kLifecyclePeer = 2;
inline constexpr double kSpeed = 20.0;
inline constexpr double kRadius = 5.0;

[[nodiscard]] inline simulation::EntityId hill_entity() { return simulation::EntityId::create(90); }
[[nodiscard]] inline simulation::Vector2 first_marker() {
  return simulation::Vector2::create(200.0, 200.0);
}
[[nodiscard]] inline simulation::Vector2 zero() { return simulation::Vector2::create(0.0, 0.0); }
[[nodiscard]] inline simulation::Vector2 far_corner() {
  return simulation::Vector2::create(959.9, 639.9);
}
[[nodiscard]] inline simulation::Vector2 outward_velocity() {
  return simulation::Vector2::create(80.0, 80.0);
}
[[nodiscard]] inline simulation::Vector2 crossing_velocity() {
  return simulation::Vector2::create(400.0, 0.0);
}
[[nodiscard]] inline simulation::Vector2 scoring_center() {
  return simulation::Vector2::create(206.0, 200.0);
}
[[nodiscard]] inline simulation::Vector2 scoring_velocity() {
  return simulation::Vector2::create(2400.0, 0.0);
}
[[nodiscard]] inline simulation::Vector2 far_marker() {
  return simulation::Vector2::create(600.0, 200.0);
}

[[nodiscard]] inline Configuration configuration() {
  auto section = Configuration::default_section();
  section.hill_motion = Configuration::HillMotionPolicy::kRandomRoam;
  section.hill_speed_minimum = kSpeed;
  section.hill_speed_maximum = kSpeed;
  section.hill_retarget_minimum_seconds = 0.005;
  section.hill_retarget_maximum_seconds = 0.005;
  section.hill_radius_world_units = kRadius;
  section.point_interval_seconds = 0.0;
  return Configuration::create(section);
}

[[nodiscard]] inline Configuration lifecycle_configuration() {
  auto section = Configuration::default_section();
  section.hill_motion = Configuration::HillMotionPolicy::kRandomRoam;
  section.countdown_seconds = 0.0;
  section.restart_delay_seconds = 0.0;
  section.time_limit_seconds = 0.01;
  return Configuration::create(section);
}

// Two locally safe islands with a wide unsupported gap and no connecting route from the spawn.
[[nodiscard]] inline simulation::MapDefinition map(const bool disconnected = false) {
  const auto bounds = simulation::ArenaBounds::create(960.0, 640.0);
  auto terrain = simulation::TerrainDefinition::solid(bounds);
  if (disconnected) {
    terrain = simulation::TerrainDefinition::create(
        bounds, simulation::TerrainGround::kCorridors,
        {simulation::TerrainCorridor::create("spawn_island", 40.0,
                                             {simulation::Vector2::create(100.0, 200.0),
                                              simulation::Vector2::create(250.0, 200.0)}),
         simulation::TerrainCorridor::create("remote_island", 40.0,
                                             {simulation::Vector2::create(550.0, 200.0),
                                              simulation::Vector2::create(650.0, 200.0)})},
        {});
  }
  return simulation::MapDefinition::create(
      "hill_roam_map", std::move(terrain), {},
      {simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(100.0, 200.0)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(150.0, 200.0)),
       simulation::MapDefinition::Marker::create("hill", first_marker(), std::nullopt,
                                                 simulation::MapMetadata::none())},
      simulation::MapMetadata::none());
}

[[nodiscard]] inline simulation::GameWorld world(const std::uint64_t seed = kSeed) {
  auto result = simulation::GameWorld::create(gameplay_configuration(), map(), seed);
  result.mutable_match().phase = simulation::MatchPhase::kRunning;
  result.mutable_store<simulation::Hill>().insert_or_assign(
      hill_entity(), simulation::Hill{first_marker(), kRadius});
  result.mutable_store<simulation::HillMotion>().insert_or_assign(hill_entity(),
                                                                  simulation::HillMotion{zero()});
  return result;
}

[[nodiscard]] inline simulation::GameWorld scoring_world() {
  auto result = world();
  const auto entity = simulation::EntityId::create(kScoringPlayer);
  result.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity, simulation::PhysicsBody::create(scoring_center(), zero(), zero()));
  result.mutable_store<simulation::Controllable>().insert_or_assign(
      entity, simulation::Controllable{simulation::ControllerId::create(kScoringPlayer)});
  return result;
}

inline void schedule(simulation::GameWorld& world, const simulation::Vector2& velocity,
                     const std::uint64_t deadline = kFarRetargetTick) {
  world.mutable_store<simulation::HillMotion>().insert_or_assign(
      hill_entity(),
      simulation::HillMotion{
          velocity, simulation::HillMotionSchedule{simulation::TickSequence::create(deadline),
                                                   simulation::RandomStreamKind::kHill}});
}

inline void move(simulation::GameWorld& world, const std::uint64_t tick,
                 const simulation::MapDefinition& arena = map()) {
  const TickHarness harness{simulation::TickSequence::create(tick), arena};
  gameplay::HillMovementSystem::create(configuration())->apply(world, harness.context());
}

[[nodiscard]] inline simulation::Hill hill(const simulation::GameWorld& world) {
  return *world.store<simulation::Hill>().find(hill_entity());
}
[[nodiscard]] inline simulation::HillMotion motion(const simulation::GameWorld& world) {
  return *world.store<simulation::HillMotion>().find(hill_entity());
}

// A genuine downstream reservation failure, after hill selection/motion have already run. A
// successful transaction records the private deadline in a test-owned Score observation before
// creating a receipt. No mutable fault switches or out-of-band buffers influence the tick.
class ObserveThenReserve final : public simulation::SimulationSystem {
public:
  [[nodiscard]] std::string_view name() const noexcept override {
    return "hill_observe_then_reserve";
  }
  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    const auto current = motion(world);
    world.mutable_store<simulation::Score>().insert_or_assign(
        hill_entity(), simulation::Score{static_cast<std::int64_t>(
                           current.schedule.value().next_retarget_tick.value())});
    const auto receipt = world.create_entity();
    world.mutable_store<simulation::Score>().insert_or_assign(receipt, simulation::Score{});
  }
};

[[nodiscard]] inline simulation::GameSimulation rollback_simulation() {
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back({simulation::SystemStage::kPostKernel,
                     gameplay::HillMovementSystem::create(configuration())});
  systems.push_back(
      {simulation::SystemStage::kPostKernel, std::make_unique<const ObserveThenReserve>()});
  return simulation::GameSimulation::create(
      gameplay_configuration(), world(),
      simulation::GameSimulationSetup::engine_defaults().with_map(map()).with_systems(
          simulation::SystemPipeline::create(std::move(systems))));
}

[[nodiscard]] inline simulation::InputBatch reserved_input(const std::uint64_t tick) {
  return simulation::InputBatch::create(
      {}, simulation::CommandKindMask::all(),
      simulation::EntityIdReservation::create(simulation::EntityId::create(1000 + tick), 1));
}

} // namespace blob_royale::testing::hill_roam_movement_fixture

#endif
