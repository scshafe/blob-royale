#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/tactical_profile_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace controllers = blob_royale::controllers;
namespace fixture = blob_royale::testing::tactical_profile_fixture;

namespace {
void require_rejection(const controllers::TacticalProfile::Section& section,
                       const controllers::ControllersValidationCode code) {
  try {
    static_cast<void>(controllers::TacticalProfile::create(section));
    FAIL("invalid profile must fail before construction");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() == code);
  }
}
} // namespace

static_assert(!std::is_default_constructible_v<controllers::TacticalProfile>);

TEST_CASE("Tactical profile retains all four authored settings and bounded identity",
          "[unit][controllers][tactical_profile]") {
  const auto section = fixture::configured_section();
  const auto profile = controllers::TacticalProfile::create(section);
  CHECK(profile.name() == fixture::kName);
  CHECK(profile.objective_seek_probability() == section.objective_seek_probability);
  CHECK(profile.reaction_delay_ticks() == section.reaction_delay_ticks);
  CHECK(profile.aim_error() == section.aim_error);
  CHECK(profile.target_persistence_ticks() == section.target_persistence_ticks);
  CHECK(profile == controllers::TacticalProfile::create(section));
  for (const auto invalid : fixture::kInvalidNames) {
    auto changed = section;
    changed.profile_name = invalid;
    require_rejection(changed, controllers::ControllersValidationCode::kTacticalProfileNameInvalid);
  }
  auto maximum = section;
  maximum.profile_name = std::string(64, 'a');
  CHECK_NOTHROW(controllers::TacticalProfile::create(maximum));
  maximum.profile_name.push_back('a');
  require_rejection(maximum, controllers::ControllersValidationCode::kTacticalProfileNameInvalid);
}

TEST_CASE("Tactical profile admits inclusive endpoints and rejects nonfinite out of range controls",
          "[unit][controllers][tactical_profile]") {
  auto section = fixture::immediate_section();
  for (const double probability : {0.0, 1.0}) {
    section.objective_seek_probability = probability;
    for (const double aim : {0.0, controllers::kMaximumTacticalAimError}) {
      section.aim_error = aim;
      section.reaction_delay_ticks = controllers::kMaximumTacticalReactionDelayTicks;
      section.target_persistence_ticks = controllers::kMaximumTacticalTargetPersistenceTicks;
      CHECK_NOTHROW(controllers::TacticalProfile::create(section));
    }
  }
  section = fixture::immediate_section();
  section.target_persistence_ticks = 0;
  CHECK_NOTHROW(controllers::TacticalProfile::create(section));
  for (const double probability : fixture::kInvalidProbabilities) {
    auto changed = section;
    changed.objective_seek_probability = probability;
    require_rejection(changed,
                      controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid);
  }
  for (const double aim : fixture::kInvalidAimErrors) {
    auto changed = section;
    changed.aim_error = aim;
    require_rejection(changed,
                      controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid);
  }
  section.reaction_delay_ticks = controllers::kMaximumTacticalReactionDelayTicks + 1;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileReactionDelayInvalid);
  section = fixture::immediate_section();
  section.target_persistence_ticks = controllers::kMaximumTacticalTargetPersistenceTicks + 1;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfilePersistenceInvalid);
}
