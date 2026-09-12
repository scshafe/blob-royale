#include "component_publication.hpp"
#include "components/controllable_component.hpp"
#include "fixtures/tick_window_fixture.hpp"
#include "input_batch.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <variant>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tick_window_fixture;

TEST_CASE("thrust intake preserves absence and positive generations but rejects present zero",
          "[unit][simulation][input_batch][input_generation]") {
  for (const auto generation :
       {std::optional<simulation::TickSequence>{}, std::optional{fixture::tick()},
        std::optional{fixture::tick(simulation::TickSequence::kMaximumValue)}}) {
    const auto command = fixture::command(generation);
    const auto batch = simulation::InputBatch::create({command}, simulation::CommandKindMask::all(),
                                                      simulation::EntityIdReservation::none());
    REQUIRE(batch.commands().size() == fixture::kOneTick);
    CHECK(std::get<simulation::ThrustCommand>(batch.commands().front()) == command);
  }
  try {
    static_cast<void>(simulation::InputBatch::create(
        {fixture::command(simulation::TickSequence::zero())}, simulation::CommandKindMask::all(),
        simulation::EntityIdReservation::none()));
    FAIL("a present zero generation was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.code() == "SIMULATION.INPUT_BATCH_INPUT_GENERATION_ZERO");
  }
}

TEST_CASE("last submitted thrust still wins regardless of its generation",
          "[unit][simulation][input_batch][input_generation]") {
  const auto latest = fixture::command(fixture::tick());
  const auto stale = fixture::command();
  const auto batch = simulation::InputBatch::create(
      {latest, stale}, simulation::CommandKindMask::all(), simulation::EntityIdReservation::none());
  REQUIRE(batch.commands().size() == fixture::kOneTick);
  CHECK(std::get<simulation::ThrustCommand>(batch.commands().front()) == stale);
}

TEST_CASE("controllable publication preserves cancellation token and strips all private input",
          "[unit][simulation][input_generation][publication]") {
  const simulation::Controllable private_input{
      simulation::ControllerId::create(fixture::kController),
      {fixture::command(fixture::tick())},
      fixture::direction(),
      fixture::tick()};
  const auto published =
      simulation::ComponentPublication<simulation::Controllable>::published(private_input);
  CHECK(published.controller_id == private_input.controller_id);
  CHECK(published.input_generation == private_input.input_generation);
  CHECK(published.commands_this_tick.empty());
  CHECK_FALSE(published.normalized_thrust_intent.has_value());
  CHECK_FALSE(private_input.commands_this_tick.empty());
  CHECK(private_input.normalized_thrust_intent == fixture::direction());
  const simulation::Controllable never_invalidated{private_input.controller_id};
  CHECK_FALSE(
      simulation::ComponentPublication<simulation::Controllable>::published(never_invalidated)
          .input_generation.has_value());
  CHECK(never_invalidated != published);
}
