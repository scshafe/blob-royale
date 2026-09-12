#include "static_body_declaration.hpp"

#include "contact_effect_admission.hpp"
#include "fixtures/contact_effect_admission_fixture.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::contact_effect_admission_fixture;

TEST_CASE("static declarations retain their explicit per-object policy and body",
          "[unit][simulation][static_body_declaration]") {
  for (const auto policy : fixture::kPolicies) {
    const auto declaration =
        simulation::StaticBodyDeclaration::create(fixture::static_body(), policy);
    CHECK(declaration.body() == fixture::static_body());
    CHECK(declaration.contact_effect_policy() == policy);
  }
  CHECK_THROWS_AS(simulation::StaticBodyDeclaration::create(
                      fixture::body(), simulation::ContactEffectPolicy::kClosingImpact),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::StaticBodyDeclaration::create(fixture::static_body(), fixture::kInvalidPolicy),
      simulation::SimulationValidationError);
}

TEST_CASE("map seating preserves authored policy order while retaining configured static radius",
          "[unit][simulation][static_body_declaration]") {
  const auto configuration = fixture::configuration();
  const auto map = simulation::MapDefinition::create(
      "contact_policy_map", simulation::ArenaBounds::create(100.0, 100.0),
      {simulation::StaticBodyDeclaration::create(fixture::static_body(),
                                                 simulation::ContactEffectPolicy::kAnyTouch),
       simulation::StaticBodyDeclaration::create(fixture::static_body(),
                                                 simulation::ContactEffectPolicy::kClosingImpact)},
      {}, simulation::MapMetadata::none());
  const auto world = simulation::GameWorld::create(configuration, map, 0);
  REQUIRE(world.store<simulation::PhysicsBody>().size() == 2);
  for (const auto& entry : world.store<simulation::PhysicsBody>().entries()) {
    CHECK(entry.value == fixture::static_body().with_radius(configuration.player_radius()));
  }
  CHECK(simulation::effective_contact_effect_policy(world, simulation::EntityId::create(1)) ==
        simulation::ContactEffectPolicy::kAnyTouch);
  CHECK(simulation::effective_contact_effect_policy(world, simulation::EntityId::create(2)) ==
        simulation::ContactEffectPolicy::kClosingImpact);
  REQUIRE(world.store<simulation::ContactEffectAdmission>().size() == 1);
  CHECK(world.store<simulation::ContactEffectAdmission>().entries()[0].entity ==
        simulation::EntityId::create(1));
}
