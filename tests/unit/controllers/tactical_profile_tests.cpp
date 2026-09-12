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
// The weight members in `TacticalObjectiveKind` ordinal order. This table is the test's own
// second opinion: production reaches a weight through the switch in `tactical_objective_weight_of`,
// and one case below proves the two agree ordinal by ordinal. It is also what lets a case set "the
// weight of kind n" without a switch of its own. The pointee is `AuthoredObjectiveWeight` and not
// `double`, which is the type change that makes an omitted weight a compile error at every
// construction site in the tree rather than a silent zero.
constexpr std::array<controllers::AuthoredObjectiveWeight controllers::TacticalObjectiveWeights::*,
                     5>
    kWeightMembers{&controllers::TacticalObjectiveWeights::hill,
                   &controllers::TacticalObjectiveWeights::zone,
                   &controllers::TacticalObjectiveWeights::race_gate,
                   &controllers::TacticalObjectiveWeights::race_recovery,
                   &controllers::TacticalObjectiveWeights::shove_setup};
static_assert(kWeightMembers.size() == controllers::kTacticalObjectiveKindCount);

constexpr controllers::TacticalObjectiveWeights kWeights{
    .hill = 1.0, .zone = 0.5, .race_gate = 0.25, .race_recovery = 0.125, .shove_setup = 0.0625};
constexpr double kRiskTolerance = 0.5;
constexpr std::uint64_t kPredictionHorizonTicks = 40;
constexpr double kChargeScreenDiagonalFraction = 0.75;
constexpr std::uint64_t kShieldAnticipationTicks = 24;
// The four personality settings, every one of them distinct from every other number above so that
// a `create` that copied one member into another's field would fail the retention case rather than
// compare equal to itself.
constexpr double kRoadCautionFraction = 0.375;
constexpr double kArrivalBrakeFraction = 0.6875;
constexpr double kExposurePreference = 0.3125;
constexpr double kMinimumOpening = 0.1875;

// The fixture owns the four Step 15 settings; this file authors the nine the pipeline, this step's
// combat and the named personalities added, so a case here proves a rule of `create` rather than a
// fixture value. One case at the end separately proves the fixture authors them, which is where a
// caller left behind by a new setting surfaces.
[[nodiscard]] controllers::TacticalProfile::Section authored_section() {
  auto section = fixture::immediate_section();
  section.objective_weights = kWeights;
  section.risk_tolerance = kRiskTolerance;
  section.prediction_horizon_ticks = kPredictionHorizonTicks;
  section.charge_screen_diagonal_fraction = kChargeScreenDiagonalFraction;
  section.shield_anticipation_ticks = kShieldAnticipationTicks;
  section.road_caution_fraction = kRoadCautionFraction;
  section.arrival_brake_fraction = kArrivalBrakeFraction;
  section.exposure_preference = kExposurePreference;
  section.minimum_opening = kMinimumOpening;
  return section;
}

[[nodiscard]] controllers::TacticalObjectiveWeights uniform_weights(const double weight) noexcept {
  return {.hill = weight,
          .zone = weight,
          .race_gate = weight,
          .race_recovery = weight,
          .shove_setup = weight};
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
// **The set of authored weights is closed by the compiler at every construction site.** A weight is
// not a `double`, so a braced list one member short of `TacticalObjectiveKind` cannot
// value-initialize the member it omitted: `TacticalObjectiveWeights` inherits the deleted default
// constructor and the site fails to build. That is the whole reason `AuthoredObjectiveWeight`
// exists, and this pair of assertions is where the tree states it -- the degenerate-set rejection
// below can only catch a *complete* set of zeros and never caught an omission beside four authored
// numbers.
static_assert(!std::is_default_constructible_v<controllers::AuthoredObjectiveWeight>);
static_assert(!std::is_default_constructible_v<controllers::TacticalObjectiveWeights>);

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
  CHECK(profile.charge_screen_diagonal_fraction() == kChargeScreenDiagonalFraction);
  CHECK(profile.shield_anticipation_ticks() == kShieldAnticipationTicks);
  CHECK(profile.road_caution_fraction() == kRoadCautionFraction);
  CHECK(profile.arrival_brake_fraction() == kArrivalBrakeFraction);
  CHECK(profile.exposure_preference() == kExposurePreference);
  CHECK(profile.minimum_opening() == kMinimumOpening);
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
  CHECK(controllers::tactical_objective_weight_key(
            controllers::TacticalObjectiveKind::kShoveSetup) == "objective_weight_shove_setup");
  const auto profile = controllers::TacticalProfile::create(authored_section());
  for (std::size_t ordinal = 0; ordinal < controllers::kTacticalObjectiveKindCount; ++ordinal) {
    const auto kind = static_cast<controllers::TacticalObjectiveKind>(ordinal);
    CHECK(controllers::tactical_objective_kind_ordinal(kind) == ordinal);
    CHECK(controllers::tactical_objective_weight_of(kWeights, kind) ==
          (kWeights.*kWeightMembers[ordinal]).value());
    CHECK(profile.objective_weight(kind) == (kWeights.*kWeightMembers[ordinal]).value());
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

TEST_CASE("Tactical profile admits both combat endpoints and refuses past either bound",
          "[unit][controllers][tactical_profile]") {
  // **Both zeros are authored answers, and they mean opposite things.** A zero charge screen
  // examines nothing and so refuses nothing, which is the permissive end; a zero anticipation
  // window predicts nothing and raises no speculative shield, which is the inert end -- the same
  // reading a zero `prediction_horizon_ticks` already has. Neither is a clamped-away value, so both
  // are asserted to survive `create` and to read back exactly.
  auto section = authored_section();
  for (const double fraction : {0.0, controllers::kMaximumTacticalChargeScreenDiagonalFraction}) {
    section.charge_screen_diagonal_fraction = fraction;
    for (const std::uint64_t window :
         {std::uint64_t{0}, controllers::kMaximumTacticalShieldAnticipationTicks}) {
      section.shield_anticipation_ticks = window;
      const auto profile = controllers::TacticalProfile::create(section);
      CHECK(profile.charge_screen_diagonal_fraction() == fraction);
      CHECK(profile.shield_anticipation_ticks() == window);
    }
  }
  // The charge screen is a finite fraction, so it refuses exactly what every other `[0,1]` setting
  // in this file refuses, and by the same shared list.
  for (const double invalid : fixture::kInvalidProbabilities) {
    auto changed = authored_section();
    changed.charge_screen_diagonal_fraction = invalid;
    CHECK(
        require_rejection(
            changed, controllers::ControllersValidationCode::kTacticalProfileChargeScreenInvalid) ==
        key_context("charge_screen_diagonal_fraction"));
  }
  // One tick past the bound, and the whole unsigned range, exactly as the horizon above: the first
  // proves the bound is where the constant says it is, the second that no integer escapes it.
  auto beyond = authored_section();
  beyond.shield_anticipation_ticks = controllers::kMaximumTacticalShieldAnticipationTicks + 1;
  CHECK(require_rejection(
            beyond,
            controllers::ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid) ==
        key_context("shield_anticipation_ticks"));
  beyond.shield_anticipation_ticks = std::numeric_limits<std::uint64_t>::max();
  CHECK(require_rejection(
            beyond,
            controllers::ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid) ==
        key_context("shield_anticipation_ticks"));
}

TEST_CASE("Tactical profile refuses a zero road caution and admits every other personality zero",
          "[unit][controllers][tactical_profile]") {
  // **`road_caution_fraction` is the one fraction in this family whose zero is refused**, and the
  // asymmetry is the point of the case. The race provider recovers when
  // `nearest.distance > fraction * road->half_width()`, so a zero recovers unless the body is
  // exactly on the centreline: it *inverts* race behaviour rather than switching it off. Its domain
  // is `RacerController`'s own pair, exclusive at the bottom and inclusive at the top, so the
  // smallest legal value is a denormal rather than a written constant -- there is no low endpoint
  // to admit, only an excluded one to refuse.
  auto section = authored_section();
  section.road_caution_fraction = controllers::kMaximumRacerCautionFraction;
  CHECK_NOTHROW(controllers::TacticalProfile::create(section));
  section.road_caution_fraction = std::numeric_limits<double>::denorm_min();
  CHECK_NOTHROW(controllers::TacticalProfile::create(section));
  // Both spellings of the excluded endpoint. A negative zero is an authored zero and not a distinct
  // value, exactly as the degenerate-weight rule already reads it.
  for (const double refused : {0.0, -0.0}) {
    auto changed = authored_section();
    changed.road_caution_fraction = refused;
    CHECK(
        require_rejection(
            changed, controllers::ControllersValidationCode::kTacticalProfileRoadCautionInvalid) ==
        key_context("road_caution_fraction"));
  }
  for (const double invalid : fixture::kInvalidProbabilities) {
    auto changed = authored_section();
    changed.road_caution_fraction = invalid;
    CHECK(
        require_rejection(
            changed, controllers::ControllersValidationCode::kTacticalProfileRoadCautionInvalid) ==
        key_context("road_caution_fraction"));
  }
  // **The other three are inclusive at both ends, and their zeros are the answers that reproduce
  // the behaviour before they existed**: no arrival brake, so an arrived bot coasts; no exposure
  // preference, so every opening stays one; no opening floor, so every fight is admitted. Each is
  // read back exactly, because a clamped-away endpoint would be a profile that cannot author the
  // neutral value the shipped `steady` section relies on.
  section = authored_section();
  for (const double brake : {0.0, controllers::kMaximumTacticalArrivalBrakeFraction}) {
    section.arrival_brake_fraction = brake;
    for (const double preference : {0.0, controllers::kMaximumTacticalExposurePreference}) {
      section.exposure_preference = preference;
      for (const double opening : {0.0, controllers::kMaximumTacticalMinimumOpening}) {
        section.minimum_opening = opening;
        const auto profile = controllers::TacticalProfile::create(section);
        CHECK(profile.arrival_brake_fraction() == brake);
        CHECK(profile.exposure_preference() == preference);
        CHECK(profile.minimum_opening() == opening);
      }
    }
  }
  for (const double invalid : fixture::kInvalidProbabilities) {
    auto brake = authored_section();
    brake.arrival_brake_fraction = invalid;
    CHECK(require_rejection(
              brake, controllers::ControllersValidationCode::kTacticalProfileArrivalBrakeInvalid) ==
          key_context("arrival_brake_fraction"));
    auto preference = authored_section();
    preference.exposure_preference = invalid;
    CHECK(require_rejection(
              preference,
              controllers::ControllersValidationCode::kTacticalProfileExposurePreferenceInvalid) ==
          key_context("exposure_preference"));
    auto opening = authored_section();
    opening.minimum_opening = invalid;
    CHECK(require_rejection(
              opening,
              controllers::ControllersValidationCode::kTacticalProfileMinimumOpeningInvalid) ==
          key_context("minimum_opening"));
  }
}

TEST_CASE("Tactical profile reports the first declared key when several are invalid",
          "[unit][controllers][tactical_profile]") {
  // Declared key order is the configuration family's order: seek probability, reaction delay, aim
  // error, target persistence, the five objective weights in kind order, risk tolerance, horizon,
  // charge screen, shield anticipation, and then the four personality keys. Every group has been
  // appended rather than interleaved, which is what keeps an existing multi-defect section blaming
  // the same key it blamed before Step 22b and before this step.
  // Every case below spoils a later key too, so only the ordering can decide which is reported.
  auto section = authored_section();
  section.objective_seek_probability = 1.01;
  section.aim_error = 0.26;
  section.objective_weights.zone = 1.01;
  section.risk_tolerance = 1.01;
  section.prediction_horizon_ticks = controllers::kMaximumTacticalPredictionHorizonTicks + 1;
  section.charge_screen_diagonal_fraction = 1.01;
  section.shield_anticipation_ticks = controllers::kMaximumTacticalShieldAnticipationTicks + 1;
  section.road_caution_fraction = 0.0;
  section.arrival_brake_fraction = 1.01;
  section.exposure_preference = 1.01;
  section.minimum_opening = 1.01;
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
  // The fifth weight is blamed after the fourth and before risk tolerance, which is the ordinal
  // order the validation loop walks and the order `kConfigFamilyFieldSpecs` declares.
  section.objective_weights.shove_setup = -0.01;
  CHECK(require_rejection(
            section,
            controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid) ==
        key_context("objective_weight_shove_setup"));
  section.objective_weights.shove_setup = 0.0625;
  require_rejection(section,
                    controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid);
  section.risk_tolerance = 1.0;
  require_rejection(
      section, controllers::ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid);
  section.prediction_horizon_ticks = 0;
  CHECK(require_rejection(
            section, controllers::ControllersValidationCode::kTacticalProfileChargeScreenInvalid) ==
        key_context("charge_screen_diagonal_fraction"));
  section.charge_screen_diagonal_fraction = 0.75;
  CHECK(require_rejection(
            section,
            controllers::ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid) ==
        key_context("shield_anticipation_ticks"));
  section.shield_anticipation_ticks = 0;
  // The four personality keys close the chain in the order they were appended, so a section that is
  // wrong in all four still blames the road caution -- which is the one whose zero a positional
  // construction site produces, and therefore the one an author most needs named first.
  CHECK(require_rejection(
            section, controllers::ControllersValidationCode::kTacticalProfileRoadCautionInvalid) ==
        key_context("road_caution_fraction"));
  section.road_caution_fraction = kRoadCautionFraction;
  CHECK(require_rejection(
            section, controllers::ControllersValidationCode::kTacticalProfileArrivalBrakeInvalid) ==
        key_context("arrival_brake_fraction"));
  section.arrival_brake_fraction = 0.0;
  CHECK(require_rejection(
            section,
            controllers::ControllersValidationCode::kTacticalProfileExposurePreferenceInvalid) ==
        key_context("exposure_preference"));
  section.exposure_preference = 0.0;
  CHECK(
      require_rejection(
          section, controllers::ControllersValidationCode::kTacticalProfileMinimumOpeningInvalid) ==
      key_context("minimum_opening"));
  section.minimum_opening = 0.0;
  // An invalid name outranks every setting, exactly as it did before these keys existed.
  section.profile_name = "Upper";
  require_rejection(section, controllers::ControllersValidationCode::kTacticalProfileNameInvalid);
}

TEST_CASE("Tactical profile fixture sections author every family key",
          "[unit][controllers][tactical_profile]") {
  // An omitted aggregate initializer is a zero, not a compile error, so a fixture left behind by a
  // new setting would quietly hand every controllers case a profile with that setting off. These
  // two calls are what turn that into one named failure instead of a surprise in some unrelated
  // case.
  CHECK_NOTHROW(controllers::TacticalProfile::create(fixture::immediate_section()));
  CHECK_NOTHROW(controllers::TacticalProfile::create(fixture::configured_section()));
  // **Constructing is not enough for a setting whose inert value is legal.** Zero passes `create`
  // for both combat keys and means "never screens" and "never anticipates", so a fixture that
  // omitted either would build, load, and silently switch off the behaviour every combat case in
  // `tactical_controller_tests.cpp` is written to observe. The weights need no such assertion:
  // `AuthoredObjectiveWeight` has no default constructor, so omitting one is a build failure at the
  // fixture rather than a value here.
  const auto immediate = fixture::immediate_section();
  const auto configured = fixture::configured_section();
  CHECK(immediate.charge_screen_diagonal_fraction > 0.0);
  CHECK(immediate.shield_anticipation_ticks > 0);
  CHECK(configured.charge_screen_diagonal_fraction > 0.0);
  CHECK(configured.shield_anticipation_ticks > 0);
  // **The three personality settings whose inert value is the *wanted* value are asserted to be
  // exactly zero**, which is the opposite assertion to the two above and for the opposite reason.
  // Zero is what reproduces the behaviour every case written before this step observes: no arrival
  // brake, so `kArrived` still coasts; no exposure preference, so every candidate's opening is one
  // and every utility score is the one Step 22a wrote; no opening floor, so the shove provider
  // yields every fight Step 22b's cases expect. A fixture that authored any of them positive would
  // move all three at once, silently, in a file that owns none of those cases.
  //
  // `road_caution_fraction` needs no assertion in either direction: `create` above already refused
  // the only value that could have been left behind here, which is the whole reason its domain
  // excludes zero.
  CHECK(immediate.arrival_brake_fraction == 0.0);
  CHECK(immediate.exposure_preference == 0.0);
  CHECK(immediate.minimum_opening == 0.0);
  CHECK(configured.arrival_brake_fraction == 0.0);
  CHECK(configured.exposure_preference == 0.0);
  CHECK(configured.minimum_opening == 0.0);
}
