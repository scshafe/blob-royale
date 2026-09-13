#include "shared/hazard_spawn_system.hpp"

#include "fixtures/hazard_stream_frozen_reference.hpp"
#include "gameplay_test_fixture.hpp"

#include "components/crossing_hazard_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "entity_id_reservation.hpp"
#include "game_simulation_setup.hpp"
#include "input_batch.hpp"
#include "random_stream_registry.hpp"
#include "shared/hazard_archetype.hpp"
#include "simulation_system.hpp"
#include "system_pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace frozen = blob_royale::testing::hazard_stream_reference;
namespace frozen_random = blob_royale::testing::deterministic_random_reference;

namespace {

constexpr std::uint64_t kHillNoiseDrawsPerTick = 7;
constexpr double kLethalRatePerSecond = 5.0;
constexpr double kNonlethalRatePerSecond = 5.0;
const std::vector<std::uint64_t> kExpectedBirthTicks{118, 125, 158, 195, 217, 240};
const std::vector<std::uint64_t> kExpectedBirthTicksWithReservationGap{118, 125, 159,
                                                                       196, 218, 241};
const std::vector<std::uint64_t> kExpectedLethalBirthTicks{125, 158, 240};
const std::vector<std::uint64_t> kExpectedLethalBirthTicksWithReservationGap{125, 159, 241};

// Test-only consumption through the real tick pipeline, before the lifecycle hazard reader. It
// changes only world-owned hill randomness, not any feature or the frozen hazard inputs.
class HillNoiseSystem final : public simulation::SimulationSystem {
public:
  explicit HillNoiseSystem(const std::uint64_t draws_per_tick) : draws_per_tick_(draws_per_tick) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "hazard_proof_hill_noise";
  }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (std::uint64_t draw = 0; draw < draws_per_tick_; ++draw) {
      static_cast<void>(world.random(simulation::RandomStreamKind::kHill).next_bits());
    }
  }

private:
  const std::uint64_t draws_per_tick_;
};

[[nodiscard]] simulation::GameSimulation hazard_game(const std::uint64_t hill_draws_per_tick) {
  const auto configuration = testing::gameplay_configuration();
  auto map = testing::gameplay_map(4);
  auto world = simulation::GameWorld::create(configuration, map, frozen::kSeed);
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  const auto defaults = simulation::MovementTuning::defaults();
  world.mutable_match().movement.current = simulation::MovementTuning::create(
      defaults.acceleration(), defaults.normal_top_speed(), defaults.charge_speed_fraction(),
      kLethalRatePerSecond, kNonlethalRatePerSecond);
  std::vector<gameplay::HazardArchetype> table{
      gameplay::HazardArchetype::create({"plaid_meteorite", frozen::kRadius, frozen::kMass,
                                         frozen::kRestitution, frozen::kSpeed,
                                         frozen::kFirstIntervalSeconds, true}),
      gameplay::HazardArchetype::create({"velvet_boulder", frozen::kRadius, frozen::kMass,
                                         frozen::kRestitution, frozen::kSpeed,
                                         frozen::kSecondIntervalSeconds, false})};
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  if (hill_draws_per_tick != 0) {
    systems.push_back({simulation::SystemStage::kPreKernel,
                       std::make_unique<const HillNoiseSystem>(hill_draws_per_tick)});
  }
  systems.push_back(
      {simulation::SystemStage::kLifecycle, gameplay::HazardSpawnSystem::create(std::move(table))});
  return simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(std::move(map))
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
}

void check_bits(const double actual, const double expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual) == std::bit_cast<std::uint64_t>(expected));
}

// The named stream still uses the independent frozen generator and crossing geometry. The
// deliberate random-scheduling cutover adds one trial draw per eligible tick, then kind and speed.
[[nodiscard]] std::uint64_t hazard_draw_count(const simulation::WorldSnapshot& snapshot) {
  return snapshot.random_draw_counts()[simulation::random_stream_index(
      simulation::RandomStreamKind::kHazards)];
}

void check_newborn(const simulation::PhysicsBody& body, const frozen::Crossing& expected,
                   const simulation::Lifetime& lifetime) {
  check_bits(body.position().x(), expected.position.x);
  check_bits(body.position().y(), expected.position.y);
  check_bits(body.velocity().x(), expected.velocity.x);
  check_bits(body.velocity().y(), expected.velocity.y);
  check_bits(body.acceleration().x(), 0.0);
  check_bits(body.acceleration().y(), 0.0);
  check_bits(body.radius(), frozen::kRadius);
  check_bits(body.mass(), frozen::kMass);
  check_bits(body.restitution(), frozen::kRestitution);
  check_bits(body.drag_scale(), 0.0);
  CHECK(body.collision_layer() == 1);
  CHECK(body.collision_mask() == 1);
  CHECK_FALSE(body.is_static());
  CHECK(body.crosses_bounds());
  CHECK(lifetime.ticks_remaining == expected.lifetime_ticks);
}

void check_production_births(const bool omit_reservation,
                             const std::uint64_t hill_draws_per_tick = 0) {
  auto game = hazard_game(hill_draws_per_tick);
  auto random = frozen_random::DeterministicRandom::create(frozen::kSeed);
  std::vector<std::uint64_t> expected_entities;
  std::vector<std::uint64_t> expected_lethal_entities;
  const auto initial = game.snapshot();
  REQUIRE(initial.entities().empty());
  REQUIRE(initial.terrain().bounds().width() == frozen::kWidth);
  REQUIRE(initial.terrain().bounds().height() == frozen::kHeight);
  REQUIRE(testing::kGameplayFixedDelta.seconds() == frozen::kSecondsPerTick);
  REQUIRE(hazard_draw_count(initial) == 0);
  REQUIRE(initial.random_draw_counts()[simulation::random_stream_index(
              simulation::RandomStreamKind::kHill)] == 0);

  // Real committed ticks execute the production HazardSpawnSystem at its lifecycle stage.
  // No royale outcome/zone, player, or lifetime-expiry system obscures the newborn observation.
  // Existing bodies still run through real physics; their later paths are not reimplemented here.
  for (std::uint64_t tick = 1; tick <= frozen::kTickCount; ++tick) {
    CAPTURE(tick, omit_reservation, hill_draws_per_tick);
    auto expected_birth = frozen::Birth::kNone;
    if (!(omit_reservation && tick == frozen::kSkippedReservationTick)) {
      const double sample = random.next_unit_interval();
      if (sample < kLethalRatePerSecond * frozen::kSecondsPerTick) {
        expected_birth = frozen::Birth::kLethal;
      } else if (sample <
                 (kLethalRatePerSecond + kNonlethalRatePerSecond) * frozen::kSecondsPerTick) {
        expected_birth = frozen::Birth::kHarmless;
      }
    }
    std::optional<frozen::Crossing> expected_crossing;
    if (expected_birth != frozen::Birth::kNone) {
      static_cast<void>(random.next_unit_interval()); // Explicit kind draw with one kind per class.
      static_cast<void>(random.next_unit_interval()); // Explicit speed draw at zero variation.
      expected_crossing = frozen::draw_crossing(random);
      expected_entities.push_back(tick);
      if (expected_birth == frozen::Birth::kLethal) {
        expected_lethal_entities.push_back(tick);
      }
    }

    // Literal zero-command reservation policy: tick N owns id N, even when that slot is unused.
    // Omitting tick140 never compacts or recycles the subsequent allocator sequence.
    const auto reservation =
        omit_reservation && tick == frozen::kSkippedReservationTick
            ? simulation::EntityIdReservation::none()
            : simulation::EntityIdReservation::create(simulation::EntityId::create(tick), 1);
    game.step(testing::kGameplayFixedDelta,
              simulation::InputBatch::create({}, game.accepted_command_kinds(), reservation));
    const auto snapshot = game.snapshot();
    REQUIRE(snapshot.tick_sequence().value() == tick);
    REQUIRE(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    // next_below(4) rejects no words: every eligible trial costs one raw draw, and a birth costs
    // five further draws. Expected decisions come from the independent frozen RNG, not production.
    CHECK(random.draw_count() ==
          tick - (omit_reservation && tick >= frozen::kSkippedReservationTick ? 1 : 0) +
              expected_entities.size() * 5);
    CHECK(hazard_draw_count(snapshot) == random.draw_count());
    CHECK(snapshot.random_draw_counts()[simulation::random_stream_index(
              simulation::RandomStreamKind::kHill)] == tick * hill_draws_per_tick);

    const auto bodies = snapshot.components<simulation::PhysicsBody>();
    const auto lifetimes = snapshot.components<simulation::Lifetime>();
    const auto lethal = snapshot.components<simulation::LethalOnContact>();
    REQUIRE(snapshot.entities().size() == expected_entities.size());
    REQUIRE(bodies.size() == expected_entities.size());
    REQUIRE(lifetimes.size() == expected_entities.size());
    REQUIRE(snapshot.components<simulation::CrossingHazard>().size() == expected_entities.size());
    REQUIRE(lethal.size() == expected_lethal_entities.size());
    for (std::size_t index = 0; index < expected_entities.size(); ++index) {
      CHECK(bodies[index].entity.value() == expected_entities[index]);
      CHECK(lifetimes[index].entity.value() == expected_entities[index]);
    }
    for (std::size_t index = 0; index < expected_lethal_entities.size(); ++index) {
      CHECK(lethal[index].entity.value() == expected_lethal_entities[index]);
    }
    if (expected_crossing) {
      REQUIRE(bodies.back().entity.value() == tick);
      check_newborn(bodies.back().value, *expected_crossing, lifetimes.back().value);
    }
  }
  CHECK(expected_entities ==
        (omit_reservation ? kExpectedBirthTicksWithReservationGap : kExpectedBirthTicks));
  CHECK(expected_lethal_entities == (omit_reservation ? kExpectedLethalBirthTicksWithReservationGap
                                                      : kExpectedLethalBirthTicks));
  CHECK(random.draw_count() == (omit_reservation ? std::uint64_t{309} : std::uint64_t{310}));
  REQUIRE(expected_entities.size() >= 3);
  CHECK(expected_entities[1] - expected_entities[0] != expected_entities[2] - expected_entities[1]);
}

} // namespace

TEST_CASE("random hazard births retain a pinned independent seed2026 sequence and unequal spacing",
          "[unit][gameplay][hazard_spawn][random_streams][promotion]") {
  check_production_births(false);
}

TEST_CASE(
    "random hazard births skip tick140 without consuming a trial or recycling its reserved id",
    "[unit][gameplay][hazard_spawn][random_streams][promotion]") {
  check_production_births(true);
}

TEST_CASE(
    "hill draws each tick preserve every random hazard and its independently expected draw count",
    "[unit][gameplay][hazard_spawn][random_streams][promotion]") {
  SECTION("all independently scheduled births remain unchanged") {
    check_production_births(false, kHillNoiseDrawsPerTick);
  }
  SECTION("the reservation gap still consumes no hazard draws") {
    check_production_births(true, kHillNoiseDrawsPerTick);
  }
}
