#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "simulation_system.hpp"
#include "simulation_test_fixture.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(
      500.0, 500.0, 10.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 10, 10);
}

[[nodiscard]] simulation::TickContext context() {
  return simulation::TickContext::create(simulation::TickSequence::create(7),
                                         simulation::FixedDelta::canonical(), configuration());
}

[[nodiscard]] simulation::GameWorld world() {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::GameWorld::create(
      {simulation::GameWorld::EntitySeed::create(
           simulation::EntityId::create(1),
           simulation::PhysicsBody::create(simulation::Vector2::create(40.0, 50.0), zero, zero)),
       simulation::GameWorld::EntitySeed::create(
           simulation::EntityId::create(2),
           simulation::PhysicsBody::create(simulation::Vector2::create(90.0, 50.0), zero, zero))});
}

} // namespace

TEST_CASE("SimulationSystem is a non-copyable interface no caller can construct directly",
          "[unit][simulation][simulation_system]") {
  STATIC_REQUIRE(std::is_abstract_v<simulation::SimulationSystem>);
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<simulation::SimulationSystem>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<simulation::SimulationSystem>);
  STATIC_REQUIRE(std::has_virtual_destructor_v<simulation::SimulationSystem>);

  // The default constructor is protected, so only a derived system may run it. A public default
  // constructor would let a caller instantiate a rule that answers to no name.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::SimulationSystem>);
}

TEST_CASE("a system applies its whole effect through the world reference it is handed",
          "[unit][simulation][simulation_system]") {
  const std::unique_ptr<const simulation::SimulationSystem> probe =
      std::make_unique<const testing::PositionProbeSystem>("position_probe");
  simulation::GameWorld probed_world = world();

  probe->apply(probed_world, context());

  CHECK(probe->name() == std::string_view("position_probe"));
  REQUIRE(probed_world.store<simulation::Score>().entries().size() == 2);
  CHECK(probed_world.store<simulation::Score>().entries()[0].entity ==
        simulation::EntityId::create(1));
  CHECK(probed_world.store<simulation::Score>().entries()[0].value == simulation::Score{40});
  CHECK(probed_world.store<simulation::Score>().entries()[1].value == simulation::Score{90});
}

TEST_CASE("applying a system twice to one world is applying it to the world it left",
          "[unit][simulation][simulation_system]") {
  // `apply` is a pure function of (world, context) onto world, so a system holds no state that a
  // second call could observe: the only thing that changes between calls is the world.
  const testing::OrderTrailSystem trail("order_trail", 3);
  simulation::GameWorld trailed_world = world();

  trail.apply(trailed_world, context());
  trail.apply(trailed_world, context());

  REQUIRE(trailed_world.store<simulation::Lifetime>().entries().size() == 2);
  CHECK(trailed_world.store<simulation::Lifetime>().entries()[0].value ==
        simulation::Lifetime{33});
  CHECK(trailed_world.store<simulation::Lifetime>().entries()[1].value ==
        simulation::Lifetime{33});
}
