#include "components/score_component.hpp"
#include "fixed_delta.hpp"
#include "fixtures/deterministic_random_frozen_reference.hpp"
#include "fixtures/random_stream_world_fixture.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "random_stream_registry.hpp"
#include "simulation_validation_error.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::random_stream_world_fixture;
namespace frozen = blob_royale::testing::deterministic_random_reference;

namespace {

using Kind = simulation::RandomStreamKind;

template <typename Value>
concept HasUnnamedRandom = requires(Value& value) { value.random(); };
template <typename Value>
concept HasLegacyCount = requires(const Value& value) { value.random_draw_count(); };
template <typename Value>
concept HasTemporaryRandom = requires(Value value) { std::move(value).random(Kind::kHazards); };

static_assert(!HasUnnamedRandom<simulation::GameWorld>);
static_assert(!HasTemporaryRandom<simulation::GameWorld>);
static_assert(!HasTemporaryRandom<const simulation::GameWorld>);
static_assert(!HasLegacyCount<simulation::WorldSnapshot>);
static_assert(std::is_nothrow_move_constructible_v<simulation::GameWorld>);
static_assert(std::is_nothrow_move_assignable_v<simulation::GameWorld>);
static_assert(
    std::same_as<decltype(std::declval<const simulation::WorldSnapshot&>().random_draw_counts()),
                 simulation::RandomDrawCounts>);
static_assert(
    std::same_as<decltype(std::declval<simulation::WorldSnapshot&&>().random_draw_counts()),
                 simulation::RandomDrawCounts>);
static_assert(noexcept(std::declval<const simulation::WorldSnapshot&>().random_draw_counts()));
static_assert(noexcept(std::declval<simulation::WorldSnapshot&&>().random_draw_counts()));

[[nodiscard]] std::uint64_t observed_bits(const simulation::WorldSnapshot& snapshot,
                                          const simulation::EntityId::Value entity) {
  for (const auto& entry : snapshot.components<simulation::Score>()) {
    if (entry.entity.value() == entity) {
      return std::bit_cast<std::uint64_t>(entry.value.points);
    }
  }
  FAIL("named random-stream score observation is absent");
  return 0;
}

void check_counts(const simulation::WorldSnapshot& snapshot, const std::uint64_t hazards,
                  const std::uint64_t hill) {
  const simulation::RandomDrawCounts counts = snapshot.random_draw_counts();
  CHECK(counts[simulation::random_stream_index(Kind::kHazards)] == hazards);
  CHECK(counts[simulation::random_stream_index(Kind::kHill)] == hill);
}

} // namespace

TEST_CASE("GameWorld initializes all named streams and owns their copied state",
          "[unit][simulation][game_world][random_streams]") {
  const simulation::GameWorld seeded = fixture::world();
  CHECK(seeded.random(Kind::kHazards).seed() == fixture::kMatchSeed);
  CHECK(seeded.random(Kind::kHill).seed() == fixture::kHillGolden.hill_seed);
  CHECK(seeded.random_draw_counts() == simulation::RandomDrawCounts{});

  const simulation::GameWorld zero_seeded = simulation::GameWorld::create({});
  CHECK(zero_seeded.random(Kind::kHazards).seed() == 0);
  CHECK(zero_seeded.random(Kind::kHill).seed() ==
        blob_royale::testing::random_stream_fixture::kHillGoldens[0].hill_seed);
  CHECK(zero_seeded.random_draw_counts() == simulation::RandomDrawCounts{});

  for (const auto& invalid : blob_royale::testing::random_stream_fixture::kInvalidKinds) {
    simulation::GameWorld copy = seeded;
    CHECK_THROWS_AS(copy.random(static_cast<Kind>(invalid.ordinal)),
                    simulation::SimulationValidationError);
    CHECK_THROWS_AS(seeded.random(static_cast<Kind>(invalid.ordinal)),
                    simulation::SimulationValidationError);
    CHECK(copy == seeded);
  }

  for (const simulation::RandomStreamDefinition stream : simulation::kRandomStreamRegistry) {
    INFO(stream.name);
    simulation::GameWorld copy = seeded;
    CHECK(copy == seeded);
    static_cast<void>(copy.random(stream.kind).next_bits());
    CHECK_FALSE(copy == seeded);
    CHECK(seeded.random(stream.kind).draw_count() == 0);
    const simulation::GameWorld moved = std::move(copy);
    CHECK(moved.random(stream.kind).draw_count() == 1);
    CHECK(seeded.random_draw_counts() == simulation::RandomDrawCounts{});
  }
}

TEST_CASE("GameSimulation rolls back both streams and retries the same full-bit continuation",
          "[unit][simulation][game_simulation][random_streams][rollback]") {
  simulation::GameSimulation retried = fixture::drawing_simulation();
  simulation::GameSimulation control = fixture::drawing_simulation();
  frozen::DeterministicRandom hazard_reference =
      frozen::DeterministicRandom::create(fixture::kMatchSeed);
  frozen::DeterministicRandom hill_reference =
      frozen::DeterministicRandom::create(fixture::kHillGolden.hill_seed);

  for (std::uint64_t tick = 1; tick <= fixture::kContinuationTicks; ++tick) {
    INFO("tick " << tick);
    const simulation::WorldSnapshot before = retried.snapshot();
    if (tick == 2) {
      try {
        retried.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
        FAIL("draw-then-reserve tick accepted an absent reservation");
      } catch (const simulation::SimulationValidationError& error) {
        CHECK(error.validation_code() ==
              simulation::SimulationValidationCode::kEntityIdReservationExhausted);
        CHECK(error.context() == "entity_id_reservation.draw_next");
      }
      CHECK(retried.snapshot() == before);
      check_counts(retried.snapshot(), tick - 1, tick - 1);
    }

    const simulation::InputBatch input = fixture::input_for_tick(tick);
    retried.step(simulation::FixedDelta::canonical(), input);
    control.step(simulation::FixedDelta::canonical(), input);
    const simulation::WorldSnapshot actual = retried.snapshot();
    CHECK(actual == control.snapshot());
    CHECK(observed_bits(actual, fixture::kHazardScoreEntity) == hazard_reference.next_bits());
    CHECK(observed_bits(actual, fixture::kHillScoreEntity) == hill_reference.next_bits());
    check_counts(actual, tick, tick);
    check_counts(before, tick - 1, tick - 1);
  }
}

TEST_CASE("WorldSnapshot retains owned named counts across later ticks and simulation destruction",
          "[unit][simulation][snapshot][random_streams]") {
  const simulation::WorldSnapshot retained = [] {
    simulation::GameSimulation game = fixture::drawing_simulation();
    check_counts(game.snapshot(), 0, 0);
    game.step(simulation::FixedDelta::canonical(), fixture::input_for_tick(1));
    const simulation::WorldSnapshot first = game.snapshot();
    for (std::uint64_t tick = 2; tick <= fixture::kContinuationTicks; ++tick) {
      game.step(simulation::FixedDelta::canonical(), fixture::input_for_tick(tick));
    }
    check_counts(first, 1, 1);
    check_counts(game.snapshot(), fixture::kContinuationTicks, fixture::kContinuationTicks);
    return first;
  }();
  check_counts(retained, 1, 1);
  simulation::RandomDrawCounts local = retained.random_draw_counts();
  local[simulation::random_stream_index(Kind::kHill)] = std::numeric_limits<std::uint64_t>::max();
  CHECK(local[simulation::random_stream_index(Kind::kHill)] ==
        std::numeric_limits<std::uint64_t>::max());
  check_counts(retained, 1, 1);
  CHECK(fixture::drawing_simulation().snapshot().random_draw_counts() ==
        simulation::RandomDrawCounts{});
}

TEST_CASE(
    "WorldSnapshot equality observes each named stream count without publishing hidden RNG state",
    "[unit][simulation][snapshot][random_streams]") {
  const simulation::WorldSnapshot original =
      fixture::simulation_from_world(fixture::world()).snapshot();
  CHECK(original == fixture::simulation_from_world(
                        fixture::world(blob_royale::testing::random_stream_fixture::kDifferentSeed))
                        .snapshot());
  for (const simulation::RandomStreamDefinition stream : simulation::kRandomStreamRegistry) {
    INFO(stream.name);
    simulation::GameWorld drawn = fixture::world();
    static_cast<void>(drawn.random(stream.kind).next_bits());
    const simulation::WorldSnapshot changed =
        fixture::simulation_from_world(std::move(drawn)).snapshot();
    CHECK(changed.tick_sequence() == original.tick_sequence());
    CHECK(changed.match() == original.match());
    CHECK_FALSE(changed == original);
  }
}
