#include "controllers_validation_error.hpp"
#include "fixtures/tactical_profile_fixture.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tactical_profile_fixture;

TEST_CASE(
    "Tactical profile catalogue owns declaration order and reports unknown names without defaults",
    "[unit][controllers][tactical_profile]") {
  const controllers::TacticalProfileCatalogue empty;
  const auto unknown_profile = fixture::profile();
  CHECK(empty.profiles().empty());
  CHECK(empty.find(unknown_profile.name()) == nullptr);
  const auto values = fixture::profiles(simulation::kMaximumNpcProfileCount);
  const auto catalogue = controllers::TacticalProfileCatalogue::create(values);
  REQUIRE(catalogue.profiles().size() == values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    CHECK(catalogue.profiles()[index] == values[index]);
    CHECK(catalogue.find(values[index].name()) == &catalogue.profiles()[index]);
  }
  CHECK(catalogue == controllers::TacticalProfileCatalogue::create(values));
  CHECK(catalogue.find(simulation::BotProfileName::create(fixture::kName)) == nullptr);
}

TEST_CASE("Tactical profile catalogue rejects duplicate identities and its seventeenth profile",
          "[unit][controllers][tactical_profile]") {
  try {
    static_cast<void>(
        controllers::TacticalProfileCatalogue::create({fixture::profile(), fixture::profile()}));
    FAIL("duplicate name must not resolve by declaration order");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kTacticalProfileNameDuplicate);
  }
  try {
    static_cast<void>(controllers::TacticalProfileCatalogue::create(
        fixture::profiles(simulation::kMaximumNpcProfileCount + 1)));
    FAIL("profile budget must be enforced before publication");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kTacticalProfileCatalogueFull);
  }
}
