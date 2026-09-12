#include "commands/charge_command.hpp"
#include "commands/shield_command.hpp"
#include "component_publication.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "fixtures/tick_window_fixture.hpp"
#include "input_batch.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <variant>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tick_window_fixture;

namespace {

// The shield twin of `fixture::command`, kept local because the shared tick-window fixture is a
// thrust fixture and a pulse carries no direction to give it.
[[nodiscard]] simulation::ShieldCommand
shield_pulse(const std::optional<simulation::TickSequence> generation = {}) {
  return {simulation::EntityId::create(fixture::kEntity), generation};
}

// The charge twin. It reuses the fixture's direction, which is what makes the comparison below a
// comparison of tokens alone: the only thing that differs between these charges is the generation.
[[nodiscard]] simulation::ChargeCommand
charge_command(const std::optional<simulation::TickSequence> generation = {}) {
  return {simulation::EntityId::create(fixture::kEntity), fixture::direction(), generation};
}

} // namespace

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

TEST_CASE("shield intake preserves absence and positive generations but rejects present zero",
          "[unit][simulation][input_batch][input_generation][shield]") {
  // The pulse carries the identical token the thrust does, so the same three shapes are legal and
  // the same one is not: absence is the never-invalidated entity, a positive tick echoes the
  // generation the world published, and a present zero is a token no entity was ever given.
  for (const auto generation :
       {std::optional<simulation::TickSequence>{}, std::optional{fixture::tick()},
        std::optional{fixture::tick(simulation::TickSequence::kMaximumValue)}}) {
    const auto command = shield_pulse(generation);
    const auto batch = simulation::InputBatch::create({command}, simulation::CommandKindMask::all(),
                                                      simulation::EntityIdReservation::none());
    REQUIRE(batch.commands().size() == fixture::kOneTick);
    CHECK(std::get<simulation::ShieldCommand>(batch.commands().front()) == command);
  }
  try {
    static_cast<void>(simulation::InputBatch::create(
        {shield_pulse(simulation::TickSequence::zero())}, simulation::CommandKindMask::all(),
        simulation::EntityIdReservation::none()));
    FAIL("a present zero generation was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.code() == "SIMULATION.INPUT_BATCH_INPUT_GENERATION_ZERO");
    CHECK(error.context() == "input_batch.commands.shield.input_generation");
  }
}

TEST_CASE("last submitted shield still wins regardless of its generation",
          "[unit][simulation][input_batch][input_generation][shield]") {
  // Intake does not judge a generation against the world -- it has none -- so a stale pulse
  // supersedes a current one exactly as a stale thrust does. Refusing to activate on a stale token
  // is the ability system's job at kPreKernel, and it is a silent no-op there rather than an error.
  const auto latest = shield_pulse(fixture::tick());
  const auto stale = shield_pulse();
  const auto batch = simulation::InputBatch::create(
      {latest, stale}, simulation::CommandKindMask::all(), simulation::EntityIdReservation::none());
  REQUIRE(batch.commands().size() == fixture::kOneTick);
  CHECK(std::get<simulation::ShieldCommand>(batch.commands().front()) == stale);
}

TEST_CASE("charge intake preserves absence and positive generations but rejects present zero",
          "[unit][simulation][input_batch][input_generation][charge]") {
  // A third kind carrying the identical token, and the identical three legal shapes: absence is the
  // never-invalidated entity, a positive tick echoes the generation the world published, and a
  // present zero is a token no entity was ever given. The direction is beside the point here --
  // intake judges the token the same way whether or not the command also carries a heading.
  for (const auto generation :
       {std::optional<simulation::TickSequence>{}, std::optional{fixture::tick()},
        std::optional{fixture::tick(simulation::TickSequence::kMaximumValue)}}) {
    const auto command = charge_command(generation);
    const auto batch = simulation::InputBatch::create({command}, simulation::CommandKindMask::all(),
                                                      simulation::EntityIdReservation::none());
    REQUIRE(batch.commands().size() == fixture::kOneTick);
    CHECK(std::get<simulation::ChargeCommand>(batch.commands().front()) == command);
  }
  try {
    static_cast<void>(simulation::InputBatch::create(
        {charge_command(simulation::TickSequence::zero())}, simulation::CommandKindMask::all(),
        simulation::EntityIdReservation::none()));
    FAIL("a present zero generation was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.code() == "SIMULATION.INPUT_BATCH_INPUT_GENERATION_ZERO");
    CHECK(error.context() == "input_batch.commands.charge.input_generation");
  }
}

TEST_CASE("last submitted charge still wins regardless of its generation",
          "[unit][simulation][input_batch][input_generation][charge]") {
  // Intake does not judge a generation against the world -- it has none -- so a stale charge
  // supersedes a current one exactly as a stale thrust or shield does. Refusing to activate on a
  // stale token is the ability system's job at kPreKernel, and it is a silent no-op there.
  const auto latest = charge_command(fixture::tick());
  const auto stale = charge_command();
  const auto batch = simulation::InputBatch::create(
      {latest, stale}, simulation::CommandKindMask::all(), simulation::EntityIdReservation::none());
  REQUIRE(batch.commands().size() == fixture::kOneTick);
  CHECK(std::get<simulation::ChargeCommand>(batch.commands().front()) == stale);
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
