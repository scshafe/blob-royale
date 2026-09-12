#include "tick_window.hpp"

#include "fixtures/tick_window_fixture.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tick_window_fixture;

TEST_CASE("tick windows contain activation and exclude expiry without countdown state",
          "[unit][simulation][tick_window]") {
  const auto window = fixture::window();
  CHECK(window.activation_tick() == fixture::tick());
  CHECK(window.expiry_tick() == fixture::tick(fixture::kExpiry));
  CHECK_FALSE(window.contains(fixture::tick(fixture::kActivation - fixture::kOneTick)));
  CHECK(window.contains(fixture::tick()));
  CHECK(window.contains(fixture::tick(fixture::kExpiry - fixture::kOneTick)));
  CHECK_FALSE(window.expired(fixture::tick(fixture::kExpiry - fixture::kOneTick)));
  CHECK_FALSE(window.contains(fixture::tick(fixture::kExpiry)));
  CHECK(window.expired(fixture::tick(fixture::kExpiry)));
  CHECK(window.expired(fixture::tick(fixture::kExpiry + fixture::kOneTick)));
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::TickWindow>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::TickWindow>);
  CHECK(window == fixture::window());
}

TEST_CASE("empty and zero-origin mathematical tick windows are valid",
          "[unit][simulation][tick_window]") {
  const auto empty = simulation::TickWindow::create(fixture::tick(), fixture::kEmptyDuration);
  CHECK(empty.activation_tick() == empty.expiry_tick());
  CHECK_FALSE(empty.contains(fixture::tick()));
  CHECK(empty.expired(fixture::tick()));
  const auto origin =
      simulation::TickWindow::create(simulation::TickSequence::zero(), fixture::kOneTick);
  CHECK(origin.contains(simulation::TickSequence::zero()));
  CHECK(origin.expired(fixture::tick(fixture::kOneTick)));
}

TEST_CASE("tick windows accept exact maximum expiry and reject overflow before addition",
          "[unit][simulation][tick_window][validation]") {
  for (const auto& input : fixture::kExactMaximumCases) {
    CAPTURE(input.activation, input.duration);
    const auto window =
        simulation::TickWindow::create(fixture::tick(input.activation), input.duration);
    CHECK(window.expiry_tick().value() == simulation::TickSequence::kMaximumValue);
  }
  for (const auto& input : fixture::kOverflowCases) {
    CAPTURE(input.activation, input.duration);
    try {
      static_cast<void>(
          simulation::TickWindow::create(fixture::tick(input.activation), input.duration));
      FAIL("overflowing window was accepted");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.code() == "SIMULATION.TICK_WINDOW_EXPIRY_OVERFLOW");
      CHECK(error.context() == "tick_window.expiry_tick");
    }
  }
}
