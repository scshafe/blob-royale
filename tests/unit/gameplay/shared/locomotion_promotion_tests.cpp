#include "shared/locomotion.hpp"

#include "fixtures/locomotion_frozen_reference.hpp"
#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "game_simulation_setup.hpp"
#include "physics.hpp"
#include "shared/thrust_steering_system.hpp"
#include "simulation_validation_error.hpp"
#include "system_pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace frozen = blob_royale::testing::locomotion_reference;

namespace {

void check_bits(const simulation::Vector2& actual, const frozen::Point expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual.x()) == std::bit_cast<std::uint64_t>(expected.x));
  CHECK(std::bit_cast<std::uint64_t>(actual.y()) == std::bit_cast<std::uint64_t>(expected.y));
}

[[nodiscard]] simulation::Vector2 vector(const frozen::Point value) {
  return simulation::Vector2::create(value.x, value.y);
}

[[nodiscard]] simulation::GameWorld sequence_world(const simulation::MapDefinition& map) {
  auto world = simulation::GameWorld::create(testing::gameplay_configuration(), map, 0);
  const auto tuning =
      simulation::MovementTuning::create(frozen::kSequenceAcceleration, frozen::kInactiveCeiling);
  world.mutable_match().movement = simulation::MovementTuningState{tuning, tuning};
  const auto entity = simulation::EntityId::create(1);
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity, simulation::PhysicsBody::create(vector(frozen::kInitialPosition),
                                              vector(frozen::kInitialVelocity),
                                              vector(frozen::kAuthoredAcceleration))
                  .with_radius(10.0));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity, simulation::Controllable{simulation::ControllerId::create(7)});
  return world;
}

[[nodiscard]] simulation::GameSimulation sequence_game() {
  auto map = testing::gameplay_map(1);
  auto world = sequence_world(map);
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPreKernel, gameplay::ThrustSteeringSystem::create()});
  return simulation::GameSimulation::create(
      testing::gameplay_configuration(), std::move(world),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(std::move(map))
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
}

} // namespace

TEST_CASE("locomotion promotion preserves frozen old normalization scaling and failure domains",
          "[unit][gameplay][locomotion][promotion]") {
  for (const auto& input : frozen::directions()) {
    for (const double maximum : frozen::kScalars) {
      CAPTURE(input.name, maximum);
      const auto direction = vector(input.direction);
      const auto expected = frozen::steered_acceleration(input.direction, maximum);
      const auto intent = gameplay::normalized_thrust_intent(direction);
      if (!std::isfinite(expected.x) || !std::isfinite(expected.y) ||
          std::abs(expected.x) > frozen::kPhysicalComponentLimit ||
          std::abs(expected.y) > frozen::kPhysicalComponentLimit) {
        CHECK_THROWS_AS(gameplay::steered_acceleration(direction, maximum),
                        simulation::SimulationValidationError);
        CHECK_THROWS_AS(gameplay::thrust_acceleration_from_intent(intent, maximum),
                        simulation::SimulationValidationError);
      } else {
        check_bits(gameplay::steered_acceleration(direction, maximum), expected);
        check_bits(gameplay::thrust_acceleration_from_intent(intent, maximum), expected);
      }
    }
  }
}

TEST_CASE("locomotion promotion retains the actual steering system's frozen command sequence",
          "[unit][gameplay][locomotion][promotion]") {
  // Both toolchains proved these same inputs against the old reader before delegation. The live
  // reader now uses locomotion; this independent frozen reference remains the expected bits.
  auto game = sequence_game();
  frozen::Body expected;
  std::optional<simulation::Vector2> candidate_intent;
  for (const auto& step : frozen::kCommandSequence) {
    CAPTURE(step.name);
    const auto before = game.snapshot();
    const auto bodies_before = before.components<simulation::PhysicsBody>();
    REQUIRE(bodies_before.size() == 1);
    const auto& body_before = bodies_before.front().value;
    std::vector<simulation::Command> commands;
    if (step.direction) {
      commands.push_back(testing::thrust_command(1, step.direction->x, step.direction->y));
      candidate_intent = gameplay::normalized_thrust_intent(vector(*step.direction));
    }
    const auto candidate_requested =
        candidate_intent ? gameplay::thrust_acceleration_from_intent(*candidate_intent,
                                                                     frozen::kSequenceAcceleration)
                         : body_before.acceleration();
    const auto candidate =
        gameplay::limit_normal_propulsion(body_before.velocity(), candidate_requested,
                                          frozen::kInactiveCeiling, testing::kGameplayFixedDelta);
    frozen::advance(expected, step);
    check_bits(candidate, expected.acceleration);
    game.step(testing::kGameplayFixedDelta,
              simulation::InputBatch::create(std::move(commands), game.accepted_command_kinds(),
                                             simulation::EntityIdReservation::none()));
    const auto snapshot = game.snapshot();
    const auto bodies = snapshot.components<simulation::PhysicsBody>();
    REQUIRE(bodies.size() == 1);
    check_bits(bodies.front().value.acceleration(), expected.acceleration);
    check_bits(bodies.front().value.velocity(), expected.velocity);
    check_bits(bodies.front().value.position(), expected.position);
  }
}

TEST_CASE("locomotion promotion pins steering admission in every existing match phase",
          "[unit][gameplay][locomotion][promotion]") {
  const auto map = testing::gameplay_map(1);
  const testing::TickHarness harness{simulation::TickSequence::create(1), map};
  const auto system = gameplay::ThrustSteeringSystem::create();
  const auto entity = simulation::EntityId::create(1);
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded}) {
    CAPTURE(phase);
    auto world = sequence_world(map);
    world.mutable_match().phase = phase;
    auto expected = frozen::kAuthoredAcceleration;
    for (const auto& step : frozen::kCommandSequence) {
      CAPTURE(step.name);
      auto* controllable = world.mutable_store<simulation::Controllable>().mutable_find(entity);
      REQUIRE(controllable != nullptr);
      controllable->commands_this_tick.clear();
      if (step.direction) {
        controllable->commands_this_tick.push_back(
            testing::thrust_command(1, step.direction->x, step.direction->y));
        expected = frozen::steered_acceleration(*step.direction, frozen::kSequenceAcceleration);
      }
      system->apply(world, harness.context());
      const auto* body = world.store<simulation::PhysicsBody>().find(entity);
      REQUIRE(body != nullptr);
      check_bits(body->acceleration(), expected);
      CHECK(world.match().phase == phase);
    }
  }
}
