#include "movement_tuning.hpp"

#include "fixtures/movement_tuning_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::movement_tuning_fixture;

static_assert(!std::is_default_constructible_v<simulation::MovementTuning>);
static_assert(std::is_nothrow_copy_constructible_v<simulation::MovementTuning>);
static_assert(std::is_nothrow_move_constructible_v<simulation::MovementTuning>);
static_assert(std::is_nothrow_move_assignable_v<simulation::MovementTuning>);

TEST_CASE("movement tuning owns finite inclusive bounds and preserves valid pair bits",
          "[unit][simulation][movement_tuning]") {
  for (const auto& input : fixture::kValidPairs) {
    CAPTURE(input.acceleration, input.normal_top_speed);
    const auto tuning =
        simulation::MovementTuning::create(input.acceleration, input.normal_top_speed);
    const double canonical_acceleration = input.acceleration == 0.0 ? 0.0 : input.acceleration;
    CHECK(std::bit_cast<std::uint64_t>(tuning.acceleration()) ==
          std::bit_cast<std::uint64_t>(canonical_acceleration));
    CHECK(std::bit_cast<std::uint64_t>(tuning.normal_top_speed()) ==
          std::bit_cast<std::uint64_t>(input.normal_top_speed));
  }
  CHECK(simulation::MovementTuning::defaults() == simulation::MovementTuning::create(400.0, 600.0));
}

TEST_CASE("movement tuning refuses invalid fields with owned structured context",
          "[unit][simulation][movement_tuning][validation]") {
  for (const auto& input : fixture::rejected_pairs()) {
    DYNAMIC_SECTION(input.name) {
      try {
        static_cast<void>(
            simulation::MovementTuning::create(input.acceleration, input.normal_top_speed));
        FAIL("an invalid movement pair was accepted");
      } catch (const simulation::SimulationValidationError& error) {
        CHECK(error.validation_code() == input.code);
        CHECK(error.code() == simulation::simulation_validation_code_name(input.code));
        CHECK(error.context() == input.context);
      }
    }
  }
}

TEST_CASE("movement tuning is an independent value across copies moves and temporary reads",
          "[unit][simulation][movement_tuning]") {
  const auto original = simulation::MovementTuning::create(4'000.0, 10'000.0);
  auto copied = original;
  auto moved = std::move(copied);
  CHECK(moved == original);
  moved = simulation::MovementTuning::defaults();
  CHECK(original == simulation::MovementTuning::create(4'000.0, 10'000.0));
  CHECK(moved != original);
  CHECK(simulation::MovementTuning::create(0.0, 1.0).normal_top_speed() == 1.0);
}
