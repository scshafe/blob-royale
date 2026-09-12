#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/tactical_profile_fixture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace controllers = blob_royale::controllers;
namespace fixture = blob_royale::testing::tactical_profile_fixture;

namespace {
// The four weight members in `TacticalObjectiveKind` ordinal order. This table is the test's own
// second opinion: production reaches a weight through the switch in `tactical_objective_weight_of`,
// and one case below proves the two agree ordinal by ordinal. It is also what lets a case set "the
// weight of kind n" without a switch of its own.
constexpr std::array<double controllers::TacticalObjectiveWeights::*, 4> kWeightMembers{
    &controllers::TacticalObjectiveWeights::hill, &controllers::TacticalObjectiveWeights::zone,
    &controllers::TacticalObjectiveWeights::race_gate,
    &controllers::TacticalObjectiveWeights::race_recovery};
static_assert(kWeightMembers.size() == controllers::kTacticalObjectiveKindCount);

constexpr controllers::TacticalObjectiveWeights kWeights{
    .hill = 1.0, .zone = 0.5, .race_gate = 0.25, .race_recovery = 0.125};
constexpr double kRiskTolerance = 0.5;
constexpr std::uint64_t kPredictionHorizonTicks = 40;

// The fixture owns the four Step 15 settings; this file authors the three the pipeline added, so a
// case here proves a rule of `create` rather than a fixture value. One case at the end separately
// proves the fixture authors them, which is where a caller left behind by a new setting surfaces.
[[nodiscard]] controllers::TacticalProfile::Section authored_section() {
  auto section = fixture::immediate_section();
  section.objective_weights = kWeights;
  section.risk_tolerance = kRiskTolerance;
  section.prediction_horizon_ticks = kPredictionHorizonTicks;
  return section;
}

[[nodiscard]] controllers::TacticalObjectiveWeights uniform_weights(const double weight) noexcept {
  return {.hill = weight, .zone = weight, .race_gate = weight, .race_recovery = weight};
}

[[nodiscard]] std::string key_context(const std::string_view key) {
  return "bot_profile." + std::string{fixture::kName} + "." + std::string{key};
}

// Returns the rejected context, so a case can assert *which* key was blamed and not merely that
// something was. Deliberately not [[nodiscard]]: most cases assert only the code.
std::string require_rejection(const controllers::TacticalProfile::Section& section,
                              const controllers::ControllersValidationCode code) {
  try {
    static_cast<void>(controllers::TacticalProfile::create(section));
    FAIL("invalid profile must fail before construction");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() == code);
    return error.context();
  }
  return {};
}
} // namespace

static_assert(!std::is_default_constructible_v<controllers::TacticalProfile>);

TEST_CASE("Tactical profile retains every authored setting and bounded identity",
          "[unit][controllers][tactical_profile]") {
  const auto section = authored_section();
  const auto profile = controllers::TacticalProfile::create(section);
  CHECK(profile.name() == fixture::kName);
  CHECK(profile.objective_seek_probability() == section.objective_seek_probability);
  CHECK(profile.reaction_delay_ticks() == section.reaction_delay_ticks);
  CHECK(profile.aim_error() == section.aim_error);
  CHECK(profile.target_persistence_ticks() == section.target_persistence_ticks);
  CHECK(profile.objective_weights() == kWeights);
  CHECK(profile.risk_tolerance() == kRiskTolerance);
  CHECK(profile.prediction_horizon_ticks() == kPredictionHorizonTicks);
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
  auto section = authored_section();
  for (const double probability : {0.0, 1.0}) {
    section.objective_seek_probability = probability;
    for (const double aim : {0.0, controllers::kMaximumTacticalAimError}) {
      section.aim_error = aim;
      section.reaction_delay_ticks = controllers::kMaximumTacticalReactionDelayTicks;
      section.target_persistence_ticks = controllers::kMaximumTacticalTargetPersistenceTicks;
      CHECK_NOTHROW(controllers::TacticalProfile::create(section));
    }
  }
  section = authored_section();
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
  section = authored_section();
  section.target_persistence_ticks = controllers::kMaximumTacticalTargetPersistenceTicks + 1;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfilePersistenceInvalid);
}

TEST_CASE("Tactical objective weight names and lookup stay closed against the kind enum",
          "[unit][controllers][tactical_profile]") {
  CHECK(controllers::tactical_objective_weight_key(controllers::TacticalObjectiveKind::kHill) ==
        "objective_weight_hill");
  CHECK(controllers::tactical_objective_weight_key(controllers::TacticalObjectiveKind::kZone) ==
        "objective_weight_zone");
  CHECK(controllers::tactical_objective_weight_key(controllers::TacticalObjectiveKind::kRaceGate) ==
        "objective_weight_race_gate");
  CHECK(controllers::tactical_objective_weight_key(
            controllers::TacticalObjectiveKind::kRaceRecovery) == "objective_weight_race_recovery");
  const auto profile = controllers::TacticalProfile::create(authored_section());
  for (std::size_t ordinal = 0; ordinal < controllers::kTacticalObjectiveKindCount; ++ordinal) {
    const auto kind = static_cast<controllers::TacticalObjectiveKind>(ordinal);
    CHECK(controllers::tactical_objective_kind_ordinal(kind) == ordinal);
    CHECK(controllers::tactical_objective_weight_of(kWeights, kind) ==
          kWeights.*kWeightMembers[ordinal]);
    CHECK(profile.objective_weight(kind) == kWeights.*kWeightMembers[ordinal]);
  }
  // A cast past the declared kinds is an error answer, never the plausible zero that would read as
  // "this profile ignores that objective".
  const auto undeclared =
      static_cast<controllers::TacticalObjectiveKind>(controllers::kTacticalObjectiveKindCount + 7);
  CHECK(std::isnan(controllers::tactical_objective_weight_of(kWeights, undeclared)));
  CHECK(std::isnan(profile.objective_weight(undeclared)));
  CHECK(controllers::tactical_objective_weight_key(undeclared) == "objective_weight_invalid");
}

TEST_CASE("Tactical profile admits every objective weight endpoint and blames the failed kind",
          "[unit][controllers][tactical_profile]") {
  auto section = authored_section();
  section.objective_weights = uniform_weights(controllers::kMaximumTacticalObjectiveWeight);
  CHECK_NOTHROW(controllers::TacticalProfile::create(section));
  for (std::size_t ordinal = 0; ordinal < controllers::kTacticalObjectiveKindCount; ++ordinal) {
    const auto kind = static_cast<controllers::TacticalObjectiveKind>(ordinal);
    // One kind at each endpoint while the rest hold the other, so an authored zero is proven legal
    // per kind and a single positive weight is proven enough to author a profile.
    auto lowest = authored_section();
    lowest.objective_weights = uniform_weights(controllers::kMaximumTacticalObjectiveWeight);
    lowest.objective_weights.*kWeightMembers[ordinal] = 0.0;
    CHECK_NOTHROW(controllers::TacticalProfile::create(lowest));
    auto only = authored_section();
    only.objective_weights = uniform_weights(0.0);
    only.objective_weights.*kWeightMembers[ordinal] = controllers::kMaximumTacticalObjectiveWeight;
    CHECK_NOTHROW(controllers::TacticalProfile::create(only));
    for (const double invalid : fixture::kInvalidProbabilities) {
      auto changed = authored_section();
      changed.objective_weights.*kWeightMembers[ordinal] = invalid;
      CHECK(require_rejection(
                changed,
                controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid) ==
            key_context(controllers::tactical_objective_weight_key(kind)));
    }
  }
}

TEST_CASE("Tactical profile rejects an objective weight set that cannot express a preference",
          "[unit][controllers][tactical_profile]") {
  auto section = authored_section();
  section.objective_weights = uniform_weights(0.0);
  CHECK(require_rejection(
            section,
            controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightsDegenerate) ==
        "bot_profile." + std::string{fixture::kName});
  // Negative zero is an authored zero, not a distinct value, so it is degenerate on the same terms.
  section.objective_weights = uniform_weights(-0.0);
  require_rejection(
      section, controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightsDegenerate);
  // The smallest positive weight is enough: the rule rejects an omission, not a faint preference.
  section.objective_weights = uniform_weights(0.0);
  section.objective_weights.race_recovery = std::numeric_limits<double>::denorm_min();
  CHECK_NOTHROW(controllers::TacticalProfile::create(section));
  // An out of range weight outranks it, so a section with both is diagnosed by its bad key.
  section.objective_weights = uniform_weights(0.0);
  section.objective_weights.zone = 1.01;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid);
}

TEST_CASE("Tactical profile admits risk tolerance and prediction horizon endpoints",
          "[unit][controllers][tactical_profile]") {
  auto section = authored_section();
  for (const double tolerance : {0.0, controllers::kMaximumTacticalRiskTolerance}) {
    section.risk_tolerance = tolerance;
    for (const std::uint64_t horizon :
         {std::uint64_t{0}, controllers::kMaximumTacticalPredictionHorizonTicks}) {
      section.prediction_horizon_ticks = horizon;
      const auto profile = controllers::TacticalProfile::create(section);
      CHECK(profile.risk_tolerance() == tolerance);
      CHECK(profile.prediction_horizon_ticks() == horizon);
    }
  }
  for (const double tolerance : fixture::kInvalidProbabilities) {
    auto changed = authored_section();
    changed.risk_tolerance = tolerance;
    CHECK(require_rejection(
              changed,
              controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid) ==
          key_context("risk_tolerance"));
  }
  auto beyond = authored_section();
  beyond.prediction_horizon_ticks = controllers::kMaximumTacticalPredictionHorizonTicks + 1;
  CHECK(require_rejection(
            beyond,
            controllers::ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid) ==
        key_context("prediction_horizon_ticks"));
}

TEST_CASE("Tactical profile reports the first declared key when several are invalid",
          "[unit][controllers][tactical_profile]") {
  // Declared key order is the configuration family's order: seek probability, reaction delay, aim
  // error, target persistence, the four objective weights in kind order, risk tolerance, horizon.
  // Every case below spoils a later key too, so only the ordering can decide which is reported.
  auto section = authored_section();
  section.objective_seek_probability = 1.01;
  section.aim_error = 0.26;
  section.objective_weights.zone = 1.01;
  section.risk_tolerance = 1.01;
  section.prediction_horizon_ticks = controllers::kMaximumTacticalPredictionHorizonTicks + 1;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid);
  section.objective_seek_probability = 1.0;
  section.reaction_delay_ticks = controllers::kMaximumTacticalReactionDelayTicks + 1;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileReactionDelayInvalid);
  section.reaction_delay_ticks = 0;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid);
  section.aim_error = 0.0;
  section.target_persistence_ticks = controllers::kMaximumTacticalTargetPersistenceTicks + 1;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfilePersistenceInvalid);
  section.target_persistence_ticks = 0;
  // Two bad weights: the earlier declared kind is blamed, so weight order is the kind ordinal.
  section.objective_weights.race_gate = -0.01;
  CHECK(require_rejection(
            section,
            controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid) ==
        key_context("objective_weight_zone"));
  section.objective_weights.zone = 0.5;
  CHECK(require_rejection(
            section,
            controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid) ==
        key_context("objective_weight_race_gate"));
  section.objective_weights.race_gate = 0.25;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid);
  section.risk_tolerance = 1.0;
  require_rejection(
      section, controllers::ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid);
  // An invalid name outranks every setting, exactly as it did before these keys existed.
  section.profile_name = "Upper";
  require_rejection(section, controllers::ControllersValidationCode::kTacticalProfileNameInvalid);
}

TEST_CASE("Tactical profile fixture sections author every family key",
          "[unit][controllers][tactical_profile]") {
  // An omitted aggregate initializer is a zero, not a compile error, so a fixture left behind by a
  // new setting would quietly hand every controllers case an unweighted profile. These two calls
  // are what turn that into one named failure instead of a surprise in some unrelated case.
  CHECK_NOTHROW(controllers::TacticalProfile::create(fixture::immediate_section()));
  CHECK_NOTHROW(controllers::TacticalProfile::create(fixture::configured_section()));
}
