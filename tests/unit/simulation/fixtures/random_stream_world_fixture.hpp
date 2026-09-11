#ifndef BLOB_ROYALE_TESTING_RANDOM_STREAM_WORLD_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_RANDOM_STREAM_WORLD_FIXTURE_HPP

#include "../simulation_test_fixture.hpp"
#include "random_stream_fixture.hpp"

#include "command_kind_mask.hpp"
#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "random_stream_registry.hpp"
#include "simulation_config.hpp"
#include "simulation_system.hpp"
#include "system_pipeline.hpp"
#include "tick_context.hpp"

#include <bit>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::testing::random_stream_world_fixture {

inline constexpr std::uint64_t kMatchSeed = random_stream_fixture::kOwnedSeed;
inline constexpr auto kHillGolden = random_stream_fixture::kHillGoldens[2];
static_assert(kHillGolden.match_seed == kMatchSeed);
inline constexpr simulation::EntityId::Value kHazardScoreEntity = 1;
inline constexpr simulation::EntityId::Value kHillScoreEntity = 2;
inline constexpr simulation::EntityId::Value kFirstReceiptEntity = 3;
inline constexpr std::uint64_t kContinuationTicks = 5;

[[nodiscard]] inline simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(
      500.0, 500.0, 10.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 10, 10);
}

[[nodiscard]] inline simulation::MapDefinition map() { return spawn_point_map(0); }

[[nodiscard]] inline simulation::GameWorld world(const std::uint64_t seed = kMatchSeed) {
  simulation::GameWorld result = simulation::GameWorld::create(configuration(), map(), seed);
  result.mutable_store<simulation::Score>().insert_or_assign(
      simulation::EntityId::create(kHazardScoreEntity), simulation::Score{});
  result.mutable_store<simulation::Score>().insert_or_assign(
      simulation::EntityId::create(kHillScoreEntity), simulation::Score{});
  return result;
}

// Records complete random words in existing signed Score cells by bit_cast, not narrowing. The
// later real reservation draw fails when input supplied no id. All observations and failure
// conditions are world/input owned: no mutable fault flag or out-of-band observation buffer.
class DrawThenReserveSystem final : public simulation::SimulationSystem {
public:
  [[nodiscard]] std::string_view name() const noexcept override { return "draw_then_reserve"; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    world.mutable_store<simulation::Score>().insert_or_assign(
        simulation::EntityId::create(kHazardScoreEntity),
        simulation::Score{std::bit_cast<std::int64_t>(
            world.random(simulation::RandomStreamKind::kHazards).next_bits())});
    world.mutable_store<simulation::Score>().insert_or_assign(
        simulation::EntityId::create(kHillScoreEntity),
        simulation::Score{std::bit_cast<std::int64_t>(
            world.random(simulation::RandomStreamKind::kHill).next_bits())});
    const simulation::EntityId receipt = world.create_entity();
    world.mutable_store<simulation::Score>().insert_or_assign(receipt, simulation::Score{});
  }
};

[[nodiscard]] inline simulation::GameSimulation simulation_from_world(simulation::GameWorld world) {
  return simulation::GameSimulation::create(
      configuration(), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(map()));
}

[[nodiscard]] inline simulation::GameSimulation drawing_simulation() {
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back(staged(simulation::SystemStage::kPostKernel,
                           std::make_unique<const DrawThenReserveSystem>()));
  return simulation::GameSimulation::create(
      configuration(), world(),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(map())
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
}

[[nodiscard]] inline simulation::InputBatch input_for_tick(const std::uint64_t tick) {
  return simulation::InputBatch::create(
      {}, simulation::CommandKindMask::all(),
      simulation::EntityIdReservation::create(
          simulation::EntityId::create(kFirstReceiptEntity + tick - 1), 1));
}

} // namespace blob_royale::testing::random_stream_world_fixture

#endif
