#include "contact_effect_admission.hpp"

#include "fixtures/contact_effect_admission_fixture.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::contact_effect_admission_fixture;

TEST_CASE("contact effect policy names are exact validated round trips",
          "[unit][simulation][contact_effect_admission]") {
  for (const auto policy : fixture::kPolicies) {
    CHECK(simulation::parse_contact_effect_policy(simulation::contact_effect_policy_name(policy)) ==
          policy);
  }
  CHECK(simulation::contact_effect_policy_name(simulation::ContactEffectPolicy::kClosingImpact) ==
        "closing_impact");
  CHECK(simulation::contact_effect_policy_name(simulation::ContactEffectPolicy::kAnyTouch) ==
        "any_touch");
  for (const std::string_view name : {"", "touch", "ANY_TOUCH", "any_touch ", "center_entry"}) {
    CHECK_THROWS_AS(simulation::parse_contact_effect_policy(name),
                    simulation::SimulationValidationError);
  }
  CHECK_THROWS_AS(simulation::contact_effect_policy_name(fixture::kInvalidPolicy),
                  simulation::SimulationValidationError);
}

TEST_CASE("absent contact admission remains closing impact and projects no sparse row",
          "[unit][simulation][contact_effect_admission]") {
  const auto world = fixture::world();
  CHECK(
      simulation::effective_contact_effect_policy(world, fixture::entity(fixture::kFirstEntity)) ==
      simulation::ContactEffectPolicy::kClosingImpact);
  CHECK(simulation::project_contact_effect_policies(world).empty());
}

TEST_CASE(
    "contact effect instance overrides dominate archetype defaults with sparse closing absence",
    "[unit][simulation][contact_effect_admission]") {
  const std::array<std::optional<simulation::ContactEffectPolicy>, 3> choices{
      std::nullopt, simulation::ContactEffectPolicy::kClosingImpact,
      simulation::ContactEffectPolicy::kAnyTouch};
  for (const auto instance : choices) {
    for (const auto archetype : choices) {
      auto world = fixture::world();
      const auto entity = fixture::entity(fixture::kFirstEntity);
      simulation::assign_contact_effect_policy(world, entity,
                                               simulation::ContactEffectPolicy::kAnyTouch);
      simulation::assign_contact_effect_policy(world, entity, instance, archetype);
      const auto expected =
          instance.value_or(archetype.value_or(simulation::ContactEffectPolicy::kClosingImpact));
      CHECK(simulation::effective_contact_effect_policy(world, entity) == expected);
      CHECK((world.store<simulation::ContactEffectAdmission>().find(entity) != nullptr) ==
            (expected == simulation::ContactEffectPolicy::kAnyTouch));
    }
  }
}

TEST_CASE("contact admission projection validates and returns ascending sparse entity rows",
          "[unit][simulation][contact_effect_admission]") {
  auto world = fixture::world();
  for (const auto value : {fixture::kSecondEntity, fixture::kFirstEntity}) {
    simulation::assign_contact_effect_policy(world, fixture::entity(value),
                                             simulation::ContactEffectPolicy::kAnyTouch);
  }
  CHECK(simulation::project_contact_effect_policies(world) ==
        std::vector<simulation::MotionContactEffectPolicy>{
            {fixture::entity(fixture::kFirstEntity), simulation::ContactEffectPolicy::kAnyTouch},
            {fixture::entity(fixture::kSecondEntity), simulation::ContactEffectPolicy::kAnyTouch}});
}

TEST_CASE("invalid contact policy inputs fail before changing the world even when overridden",
          "[unit][simulation][contact_effect_admission]") {
  auto world = fixture::world();
  const auto before = world;
  const auto entity = fixture::entity(fixture::kFirstEntity);
  CHECK_THROWS_AS(simulation::assign_contact_effect_policy(world, entity, fixture::kInvalidPolicy),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::assign_contact_effect_policy(
          world, entity, simulation::ContactEffectPolicy::kClosingImpact, fixture::kInvalidPolicy),
      simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::assign_contact_effect_policy(world, fixture::entity(fixture::kMissingEntity),
                                               simulation::ContactEffectPolicy::kAnyTouch),
      simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::effective_contact_effect_policy(world, fixture::entity(fixture::kMissingEntity)),
      simulation::SimulationValidationError);
  CHECK(world == before);
}

TEST_CASE("stored default unknown and bodyless contact admission fail projection without repair",
          "[unit][simulation][contact_effect_admission]") {
  for (const auto policy :
       {simulation::ContactEffectPolicy::kClosingImpact, fixture::kInvalidPolicy}) {
    auto world = fixture::world();
    const auto entity = fixture::entity(fixture::kFirstEntity);
    world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(entity, {policy});
    const auto before = world;
    CHECK_THROWS_AS(simulation::project_contact_effect_policies(world),
                    simulation::SimulationValidationError);
    CHECK_THROWS_AS(simulation::assign_contact_effect_policy(world, entity),
                    simulation::SimulationValidationError);
    CHECK(world == before);
  }
  auto world = fixture::world();
  world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
      fixture::entity(fixture::kMissingEntity), {});
  const auto before = world;
  CHECK_THROWS_AS(simulation::project_contact_effect_policies(world),
                  simulation::SimulationValidationError);
  CHECK(world == before);
}

TEST_CASE("contact admission follows body cleanup and cannot survive entity destruction",
          "[unit][simulation][contact_effect_admission]") {
  auto world = fixture::world();
  const auto entity = fixture::entity(fixture::kFirstEntity);
  simulation::assign_contact_effect_policy(world, entity,
                                           simulation::ContactEffectPolicy::kAnyTouch);
  world.mutable_store<simulation::PhysicsBody>().erase(entity);
  world.erase_body_bound_components_without_body();
  CHECK(world.store<simulation::ContactEffectAdmission>().empty());
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity, fixture::body());
  CHECK(simulation::effective_contact_effect_policy(world, entity) ==
        simulation::ContactEffectPolicy::kClosingImpact);
  simulation::assign_contact_effect_policy(world, entity,
                                           simulation::ContactEffectPolicy::kAnyTouch);
  world.destroy_entity(entity);
  CHECK(world.store<simulation::ContactEffectAdmission>().empty());
  CHECK_FALSE(world.contains(entity));
}
