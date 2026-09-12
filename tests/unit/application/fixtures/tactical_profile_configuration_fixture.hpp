#ifndef BLOB_ROYALE_TESTS_UNIT_APPLICATION_FIXTURES_TACTICAL_PROFILE_CONFIGURATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_APPLICATION_FIXTURES_TACTICAL_PROFILE_CONFIGURATION_FIXTURE_HPP

#include "../application_input_test_fixture.hpp"

#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "match_configuration.hpp"
#include "tactical_objective_candidates.hpp"
#include "tactical_profile_catalogue.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::application::tactical_profile_configuration_fixture {

inline constexpr std::string_view kFirstProfileName = "porcelain_otter";
inline constexpr std::string_view kSecondProfileName = "velvet_ibis";
inline constexpr std::string_view kUnknownProfileName = "unconfigured_profile";
inline constexpr std::string_view kProfileKind = "tactical";
inline constexpr std::string_view kPlainKind = "wanderer";
inline constexpr std::string_view kDefaultMode = "royale";
inline constexpr std::string_view kBaseRosterLine = "bots=wanderer:2, chaser:1";
inline constexpr std::string_view kProfileRoster = "tactical@porcelain_otter:1";
inline constexpr std::string_view kMixedRoster =
    "tactical@porcelain_otter:1, wanderer:1, tactical@velvet_ibis:2";
inline constexpr std::string_view kSpacedRoster =
    " tactical @ porcelain_otter : 1 , wanderer : 1 , tactical @ velvet_ibis : 2 ";
inline constexpr std::string_view kDuplicateProfileRoster =
    "tactical@porcelain_otter:1,tactical@porcelain_otter:2";
inline constexpr std::string_view kFirstProfileHeader = "[bot_profile.porcelain_otter]";

// **Every authored number here is a distinct exact binary fraction, and no key holds the same
// value in both profiles.** Equal weights, or values that only round-trip to a hand-counted number
// of decimal places, would let a section whose keys were wired to the wrong fields still compare
// equal, which would turn the round-trip proof in
// `tactical_profile_configuration_tests.cpp` into a size check. The weight keys are spelled
// out rather than built from `controllers::tactical_objective_weight_key` because these two are the
// readable examples every other test substitutes into, and a literal section is what a reader
// compares against `config/blob-royale.cfg`; the bound sections below are built from the names and
// the limits instead, because those are about the limits and must follow them when they move.
//
// The three keys Step 22b added are authored here on the same terms: a fifth weight, a charge
// screen distinct from every other fraction in its section -- `risk_tolerance` included, since the
// two are both plain `[0,1]` doubles and a crossed pair would otherwise pass -- and an anticipation
// window distinct from every other tick count in its section.
inline constexpr std::string_view kFirstProfileSection = "\n[bot_profile.porcelain_otter]\n"
                                                         "objective_seek_probability=1\n"
                                                         "reaction_delay_ticks=80\n"
                                                         "aim_error=0.05\n"
                                                         "target_persistence_ticks=400\n"
                                                         "objective_weight_hill=1\n"
                                                         "objective_weight_zone=0.5\n"
                                                         "objective_weight_race_gate=0.25\n"
                                                         "objective_weight_race_recovery=0.125\n"
                                                         "objective_weight_shove_setup=0.0625\n"
                                                         "risk_tolerance=0.4\n"
                                                         "prediction_horizon_ticks=64\n"
                                                         "charge_screen_diagonal_fraction=0.75\n"
                                                         "shield_anticipation_ticks=12\n";
inline constexpr std::string_view kSecondProfileSection = "\n[bot_profile.velvet_ibis]\n"
                                                          "objective_seek_probability=0.25\n"
                                                          "reaction_delay_ticks=23\n"
                                                          "aim_error=0.125\n"
                                                          "target_persistence_ticks=71\n"
                                                          "objective_weight_hill=0.125\n"
                                                          "objective_weight_zone=0.25\n"
                                                          "objective_weight_race_gate=0.5\n"
                                                          "objective_weight_race_recovery=1\n"
                                                          "objective_weight_shove_setup=0.75\n"
                                                          "risk_tolerance=0.9\n"
                                                          "prediction_horizon_ticks=16\n"
                                                          "charge_screen_diagonal_fraction=0.375\n"
                                                          "shield_anticipation_ticks=31\n";
inline constexpr std::string_view kHazardSection = "\n[hazard.porcelain_otter]\n"
                                                   "radius_world_units=10\n"
                                                   "mass=1\n"
                                                   "restitution=1\n"
                                                   "speed_world_units_per_second=260\n"
                                                   "spawn_interval_seconds=6\n"
                                                   "lethal_on_contact=true\n"
                                                   "contact_effect_policy=closing_impact\n";

// Shortest round-trip decimal text for one authored number, so a section this fixture writes and
// the `Section` value a test compares it against hold the same binary64 *by construction*. Spelling
// a bound with a hand-counted number of decimal places is how a bound test passes at 0.25 and
// silently stops testing the bound the day someone picks 0.3.
[[nodiscard]] inline std::string authored_decimal(const double value) {
  std::array<char, 64> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (error != std::errc{}) {
    throw std::logic_error{"a binary64's shortest round-trip text must fit sixty-four bytes"};
  }
  return std::string{buffer.data(), end};
}

// The weight lines of one section, keyed by `controllers::tactical_objective_weight_key` and
// walked by ordinal rather than by a written list of kinds. Nothing here spells a key, so a new
// objective kind widens every section this builds and cannot leave one silently unauthored --
// which is how the bound sections below already carried Step 22b's fifth weight for free.
[[nodiscard]] inline std::string
objective_weight_lines(const controllers::TacticalObjectiveWeights& weights) {
  std::string lines;
  for (std::size_t ordinal = 0; ordinal < controllers::kTacticalObjectiveKindCount; ++ordinal) {
    const auto kind = static_cast<controllers::TacticalObjectiveKind>(ordinal);
    lines.append(controllers::tactical_objective_weight_key(kind));
    lines.push_back('=');
    lines.append(authored_decimal(controllers::tactical_objective_weight_of(weights, kind)));
    lines.push_back('\n');
  }
  return lines;
}

// **Every member is named and none is omitted.** A designated initializer value-initializes the
// members it leaves out rather than refusing to compile, and zero is a legal authored value for
// both trailing combat settings, so an omission here would be a section that loads, validates, and
// silently disagrees with the text above it in exactly the field the text was added to prove.
inline const controllers::TacticalProfile::Section kFirstProfileValues{
    .profile_name = "porcelain_otter",
    .objective_seek_probability = 1,
    .reaction_delay_ticks = 80,
    .aim_error = 0.05,
    .target_persistence_ticks = 400,
    .objective_weights = {.hill = 1.0,
                          .zone = 0.5,
                          .race_gate = 0.25,
                          .race_recovery = 0.125,
                          .shove_setup = 0.0625},
    .risk_tolerance = 0.4,
    .prediction_horizon_ticks = 64,
    .charge_screen_diagonal_fraction = 0.75,
    .shield_anticipation_ticks = 12};
inline const controllers::TacticalProfile::Section kSecondProfileValues{
    .profile_name = "velvet_ibis",
    .objective_seek_probability = 0.25,
    .reaction_delay_ticks = 23,
    .aim_error = 0.125,
    .target_persistence_ticks = 71,
    .objective_weights =
        {.hill = 0.125, .zone = 0.25, .race_gate = 0.5, .race_recovery = 1.0, .shove_setup = 0.75},
    .risk_tolerance = 0.9,
    .prediction_horizon_ticks = 16,
    .charge_screen_diagonal_fraction = 0.375,
    .shield_anticipation_ticks = 31};

// The two bound sections are written from the named limits rather than from copied numerals, so
// the "accepts exact inclusive bounds without clamping" case keeps testing the bound after a limit
// moves instead of quietly testing an interior value.
//
// **The minimum section cannot author every weight at zero**, because that combination is refused
// by `TacticalProfile::create` as a set that expresses nothing
// (`CONTROLLERS.TACTICAL_PROFILE_OBJECTIVE_WEIGHTS_DEGENERATE`, exercised by its own domain-failure
// case below). One weight therefore sits at its maximum while the rest sit at the inclusive
// zero this case exists to prove is accepted rather than clamped away.
//
// **The two combat settings pull in opposite directions here, and the minimum section is the
// permissive one.** A zero `charge_screen_diagonal_fraction` is a screen that examines nothing and
// so refuses nothing, while its maximum is the strictest screen a profile can author; a zero
// `shield_anticipation_ticks` anticipates nothing at all. Both zeros are real authored answers,
// which is why they belong in the section that proves the inclusive low end is accepted.
inline const controllers::TacticalObjectiveWeights kMinimumObjectiveWeights{
    .hill = controllers::kMaximumTacticalObjectiveWeight,
    .zone = 0.0,
    .race_gate = 0.0,
    .race_recovery = 0.0,
    .shove_setup = 0.0};
inline const controllers::TacticalObjectiveWeights kMaximumObjectiveWeights{
    .hill = controllers::kMaximumTacticalObjectiveWeight,
    .zone = controllers::kMaximumTacticalObjectiveWeight,
    .race_gate = controllers::kMaximumTacticalObjectiveWeight,
    .race_recovery = controllers::kMaximumTacticalObjectiveWeight,
    .shove_setup = controllers::kMaximumTacticalObjectiveWeight};
inline const std::string kMinimumProfileSection =
    std::string{"\n[bot_profile.porcelain_otter]\nobjective_seek_probability=0\n"
                "reaction_delay_ticks=0\naim_error=0\ntarget_persistence_ticks=0\n"} +
    objective_weight_lines(kMinimumObjectiveWeights) + "risk_tolerance=0\n" +
    "prediction_horizon_ticks=0\n" + "charge_screen_diagonal_fraction=0\n" +
    "shield_anticipation_ticks=0\n";
inline const std::string kMaximumProfileSection =
    std::string{"\n[bot_profile.porcelain_otter]\nobjective_seek_probability=1\n"
                "reaction_delay_ticks=4000\naim_error=0.25\ntarget_persistence_ticks=4000\n"} +
    objective_weight_lines(kMaximumObjectiveWeights) +
    "risk_tolerance=" + authored_decimal(controllers::kMaximumTacticalRiskTolerance) + "\n" +
    "prediction_horizon_ticks=" +
    std::to_string(controllers::kMaximumTacticalPredictionHorizonTicks) + "\n" +
    "charge_screen_diagonal_fraction=" +
    authored_decimal(controllers::kMaximumTacticalChargeScreenDiagonalFraction) + "\n" +
    "shield_anticipation_ticks=" +
    std::to_string(controllers::kMaximumTacticalShieldAnticipationTicks) + "\n";
inline const controllers::TacticalProfile::Section kMinimumProfileValues{
    .profile_name = "porcelain_otter",
    .objective_seek_probability = 0,
    .reaction_delay_ticks = 0,
    .aim_error = 0,
    .target_persistence_ticks = 0,
    .objective_weights = kMinimumObjectiveWeights,
    .risk_tolerance = 0,
    .prediction_horizon_ticks = 0,
    .charge_screen_diagonal_fraction = 0,
    .shield_anticipation_ticks = 0};
inline const controllers::TacticalProfile::Section kMaximumProfileValues{
    .profile_name = "porcelain_otter",
    .objective_seek_probability = 1,
    .reaction_delay_ticks = 4000,
    .aim_error = 0.25,
    .target_persistence_ticks = 4000,
    .objective_weights = kMaximumObjectiveWeights,
    .risk_tolerance = controllers::kMaximumTacticalRiskTolerance,
    .prediction_horizon_ticks = controllers::kMaximumTacticalPredictionHorizonTicks,
    .charge_screen_diagonal_fraction = controllers::kMaximumTacticalChargeScreenDiagonalFraction,
    .shield_anticipation_ticks = controllers::kMaximumTacticalShieldAnticipationTicks};

struct RequiredField final {
  std::string_view line;
  std::string_view context;
};
inline constexpr std::array kRequiredFields{
    RequiredField{"objective_seek_probability=1\n",
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    RequiredField{"reaction_delay_ticks=80\n", "bot_profile.porcelain_otter.reaction_delay_ticks"},
    RequiredField{"aim_error=0.05\n", "bot_profile.porcelain_otter.aim_error"},
    RequiredField{"target_persistence_ticks=400\n",
                  "bot_profile.porcelain_otter.target_persistence_ticks"},
    RequiredField{"objective_weight_hill=1\n", "bot_profile.porcelain_otter.objective_weight_hill"},
    RequiredField{"objective_weight_zone=0.5\n",
                  "bot_profile.porcelain_otter.objective_weight_zone"},
    RequiredField{"objective_weight_race_gate=0.25\n",
                  "bot_profile.porcelain_otter.objective_weight_race_gate"},
    RequiredField{"objective_weight_race_recovery=0.125\n",
                  "bot_profile.porcelain_otter.objective_weight_race_recovery"},
    RequiredField{"objective_weight_shove_setup=0.0625\n",
                  "bot_profile.porcelain_otter.objective_weight_shove_setup"},
    RequiredField{"risk_tolerance=0.4\n", "bot_profile.porcelain_otter.risk_tolerance"},
    RequiredField{"prediction_horizon_ticks=64\n",
                  "bot_profile.porcelain_otter.prediction_horizon_ticks"},
    RequiredField{"charge_screen_diagonal_fraction=0.75\n",
                  "bot_profile.porcelain_otter.charge_screen_diagonal_fraction"},
    RequiredField{"shield_anticipation_ticks=12\n",
                  "bot_profile.porcelain_otter.shield_anticipation_ticks"}};

struct ParserFailure final {
  std::string_view original;
  std::string_view replacement;
  ApplicationInputErrorCode code;
};
inline constexpr std::array kParserFailures{
    ParserFailure{kFirstProfileHeader, "[bot_profile]",
                  ApplicationInputErrorCode::kConfigurationSectionUnknown},
    ParserFailure{kFirstProfileHeader, "[bot_profile.]",
                  ApplicationInputErrorCode::kConfigurationSectionUnknown},
    ParserFailure{kFirstProfileHeader, "[bot_profiles.porcelain_otter]",
                  ApplicationInputErrorCode::kConfigurationSectionUnknown},
    ParserFailure{kFirstProfileHeader, "[bot_profile.porcelain_otter",
                  ApplicationInputErrorCode::kConfigurationSyntaxInvalid},
    ParserFailure{"aim_error=0.05", "aggression=0.05",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"aim_error=0.05", "aim_error=0.05\naim_error=0.1",
                  ApplicationInputErrorCode::kConfigurationKeyDuplicate},
    ParserFailure{"aim_error=0.05",
                  "aim_error=", ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"aim_error=0.05", "aim_error=0.05oops",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"objective_seek_probability=1", "objective_seek_probability=true",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=-1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=+1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=1.5",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=1e2",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=18446744073709551616",
                  ApplicationInputErrorCode::kConfigurationValueOutOfRange},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=-1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=2.5",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=4e2",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=nan",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=18446744073709551616",
                  ApplicationInputErrorCode::kConfigurationValueOutOfRange},
    // The nine selection and combat keys answer to the same closed-schema rules the four above do.
    // The unknown-key cases are the near-misses an author actually types -- a pluralised kind, a
    // misspelled tolerance, a horizon in the wrong unit -- because a key that is almost right is
    // the one a laxer parser would silently ignore, leaving that setting at whatever an aggregate
    // initializer had zeroed it to.
    ParserFailure{"objective_weight_hill=1", "objective_weight_hills=1",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"risk_tolerance=0.4", "risk_tolerence=0.4",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"prediction_horizon_ticks=64", "prediction_horizon_seconds=0.16",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"objective_weight_zone=0.5", "objective_weight_zone=0.5\nobjective_weight_zone=1",
                  ApplicationInputErrorCode::kConfigurationKeyDuplicate},
    ParserFailure{"risk_tolerance=0.4", "risk_tolerance=0.4\nrisk_tolerance=0.9",
                  ApplicationInputErrorCode::kConfigurationKeyDuplicate},
    ParserFailure{"prediction_horizon_ticks=64",
                  "prediction_horizon_ticks=64\nprediction_horizon_ticks=32",
                  ApplicationInputErrorCode::kConfigurationKeyDuplicate},
    ParserFailure{"objective_weight_race_gate=0.25", "objective_weight_race_gate=oops",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"objective_weight_race_recovery=0.125", "objective_weight_race_recovery=",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"risk_tolerance=0.4",
                  "risk_tolerance=", ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"risk_tolerance=0.4", "risk_tolerance=true",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"prediction_horizon_ticks=64", "prediction_horizon_ticks=-1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"prediction_horizon_ticks=64", "prediction_horizon_ticks=6.4",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"prediction_horizon_ticks=64", "prediction_horizon_ticks=6e1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"prediction_horizon_ticks=64", "prediction_horizon_ticks=18446744073709551616",
                  ApplicationInputErrorCode::kConfigurationValueOutOfRange},
    // Step 22b's three, on the same terms. The two near-misses are the ones ADR 0008's concepts
    // invite: `aggression` above is the concept that has no key at all, and these are the two that
    // do have keys but not under the names a reader of the ADR would reach for -- a bare
    // `charge_screen` in unstated units, and a shield window spelled as the timing *error* the ADR
    // originally named. Both must be refused by name rather than ignored.
    ParserFailure{"objective_weight_shove_setup=0.0625", "objective_weight_shove=0.0625",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"charge_screen_diagonal_fraction=0.75", "charge_screen=0.75",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"shield_anticipation_ticks=12", "shield_timing_error=12",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"charge_screen_diagonal_fraction=0.75",
                  "charge_screen_diagonal_fraction=0.75\ncharge_screen_diagonal_fraction=0.25",
                  ApplicationInputErrorCode::kConfigurationKeyDuplicate},
    ParserFailure{"charge_screen_diagonal_fraction=0.75", "charge_screen_diagonal_fraction=",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"shield_anticipation_ticks=12", "shield_anticipation_ticks=1.2",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"shield_anticipation_ticks=12", "shield_anticipation_ticks=-1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"shield_anticipation_ticks=12", "shield_anticipation_ticks=18446744073709551616",
                  ApplicationInputErrorCode::kConfigurationValueOutOfRange}};

struct DomainFailure final {
  std::string_view original;
  std::string_view replacement;
  controllers::ControllersValidationCode code;
  std::string_view context;
};
inline constexpr std::array kDomainFailures{
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=-0.01",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=1.01",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=nan",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=inf",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=4001",
                  controllers::ControllersValidationCode::kTacticalProfileReactionDelayInvalid,
                  "bot_profile.porcelain_otter.reaction_delay_ticks"},
    DomainFailure{"aim_error=0.05", "aim_error=-0.01",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"aim_error=0.05", "aim_error=0.251",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"aim_error=0.05", "aim_error=nan",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"aim_error=0.05", "aim_error=inf",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"target_persistence_ticks=400", "target_persistence_ticks=4001",
                  controllers::ControllersValidationCode::kTacticalProfilePersistenceInvalid,
                  "bot_profile.porcelain_otter.target_persistence_ticks"},
    // **One rejection per new key, each perturbing exactly that key**, so the case proves both the
    // code and the `<family>.<instance>.<key>` context the domain reports it under -- which is the
    // half a "does it throw" assertion misses and the half an author reading a startup failure
    // actually needs. The five weights take one failure mode each, which is exactly the five a
    // finite `[0,1]` bound has: below the range, above it, and the three non-finite spellings.
    DomainFailure{"objective_weight_hill=1", "objective_weight_hill=-0.01",
                  controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid,
                  "bot_profile.porcelain_otter.objective_weight_hill"},
    DomainFailure{"objective_weight_zone=0.5", "objective_weight_zone=1.01",
                  controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid,
                  "bot_profile.porcelain_otter.objective_weight_zone"},
    DomainFailure{"objective_weight_race_gate=0.25", "objective_weight_race_gate=nan",
                  controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid,
                  "bot_profile.porcelain_otter.objective_weight_race_gate"},
    DomainFailure{"objective_weight_race_recovery=0.125", "objective_weight_race_recovery=inf",
                  controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid,
                  "bot_profile.porcelain_otter.objective_weight_race_recovery"},
    DomainFailure{"objective_weight_shove_setup=0.0625", "objective_weight_shove_setup=-inf",
                  controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid,
                  "bot_profile.porcelain_otter.objective_weight_shove_setup"},
    // Five individually legal zeros, refused as a combination. The context names the section
    // because no single key is at fault, which is the one rejection in this list that does. The
    // fifth line is not optional: leave `objective_weight_shove_setup` positive and the set still
    // expresses a preference, so this case would stop reaching the rule it is named for.
    DomainFailure{
        "objective_weight_hill=1\nobjective_weight_zone=0.5\n"
        "objective_weight_race_gate=0.25\nobjective_weight_race_recovery=0.125\n"
        "objective_weight_shove_setup=0.0625",
        "objective_weight_hill=0\nobjective_weight_zone=0\n"
        "objective_weight_race_gate=0\nobjective_weight_race_recovery=0\n"
        "objective_weight_shove_setup=0",
        controllers::ControllersValidationCode::kTacticalProfileObjectiveWeightsDegenerate,
        "bot_profile.porcelain_otter"},
    DomainFailure{"risk_tolerance=0.4", "risk_tolerance=-0.01",
                  controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid,
                  "bot_profile.porcelain_otter.risk_tolerance"},
    DomainFailure{"risk_tolerance=0.4", "risk_tolerance=1.01",
                  controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid,
                  "bot_profile.porcelain_otter.risk_tolerance"},
    DomainFailure{"risk_tolerance=0.4", "risk_tolerance=nan",
                  controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid,
                  "bot_profile.porcelain_otter.risk_tolerance"},
    DomainFailure{"risk_tolerance=0.4", "risk_tolerance=inf",
                  controllers::ControllersValidationCode::kTacticalProfileRiskToleranceInvalid,
                  "bot_profile.porcelain_otter.risk_tolerance"},
    // One tick past the bound, and the whole unsigned range: the first proves the bound is exactly
    // where it is stated, the second that a value the parser accepts lexically is still refused.
    DomainFailure{"prediction_horizon_ticks=64", "prediction_horizon_ticks=401",
                  controllers::ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid,
                  "bot_profile.porcelain_otter.prediction_horizon_ticks"},
    DomainFailure{"prediction_horizon_ticks=64", "prediction_horizon_ticks=18446744073709551615",
                  controllers::ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid,
                  "bot_profile.porcelain_otter.prediction_horizon_ticks"},
    // The charge screen is a finite fraction, so it fails the way the weights do; the anticipation
    // window is a tick count, so it fails the way the horizon does -- one past the bound, and the
    // whole unsigned range the parser accepts lexically.
    DomainFailure{"charge_screen_diagonal_fraction=0.75", "charge_screen_diagonal_fraction=1.01",
                  controllers::ControllersValidationCode::kTacticalProfileChargeScreenInvalid,
                  "bot_profile.porcelain_otter.charge_screen_diagonal_fraction"},
    DomainFailure{"charge_screen_diagonal_fraction=0.75", "charge_screen_diagonal_fraction=nan",
                  controllers::ControllersValidationCode::kTacticalProfileChargeScreenInvalid,
                  "bot_profile.porcelain_otter.charge_screen_diagonal_fraction"},
    DomainFailure{"shield_anticipation_ticks=12", "shield_anticipation_ticks=41",
                  controllers::ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid,
                  "bot_profile.porcelain_otter.shield_anticipation_ticks"},
    DomainFailure{"shield_anticipation_ticks=12", "shield_anticipation_ticks=18446744073709551615",
                  controllers::ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid,
                  "bot_profile.porcelain_otter.shield_anticipation_ticks"}};

struct SelectionFailure final {
  std::string_view roster;
  ApplicationInputErrorCode code;
};
inline constexpr std::array kSelectionFailures{
    SelectionFailure{"tactical:1", ApplicationInputErrorCode::kMatchBotProfileRequired},
    SelectionFailure{"wanderer@porcelain_otter:1",
                     ApplicationInputErrorCode::kMatchBotProfileUnexpected},
    SelectionFailure{"chaser@porcelain_otter:1",
                     ApplicationInputErrorCode::kMatchBotProfileUnexpected},
    SelectionFailure{"tactical@unconfigured_profile:1",
                     ApplicationInputErrorCode::kMatchBotProfileUnknown},
    SelectionFailure{"unregistered@porcelain_otter:1",
                     ApplicationInputErrorCode::kMatchBotKindUnknown}};

inline constexpr std::array kMalformedRosterTerms{"tactical@:1",
                                                  "@porcelain_otter:1",
                                                  "tactical@@porcelain_otter:1",
                                                  "tactical@porcelain_otter",
                                                  "tactical@porcelain_otter:",
                                                  "tactical@porcelain_otter:0",
                                                  "tactical@porcelain_otter:-1",
                                                  "tactical@porcelain_otter:1.5",
                                                  "tactical@porcelain_otter:1:2"};
inline constexpr std::array kMalformedNames{"Porcelain_otter", "porcelain-otter", "porcelain.otter",
                                            "7otter"};
inline const std::string kOversizedName(65, 'a');
inline const std::string kMaximumName(64, 'a');

struct ModeAdmission final {
  std::string_view mode;
  bool admits_profiled_roster;
};
inline constexpr std::array kModeAdmissions{
    ModeAdmission{"sandbox", false}, ModeAdmission{"royale", true},
    ModeAdmission{"king_of_the_hill", true}, ModeAdmission{"race", true}};

[[nodiscard]] inline std::string
configuration(const std::string_view roster = kProfileRoster,
              const std::string_view mode = kDefaultMode,
              const std::string_view sections = kFirstProfileSection) {
  std::string text = test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                                kBaseRosterLine, "bots=" + std::string{roster});
  text = test_fixture::replace_once(std::move(text), "mode=royale", "mode=" + std::string{mode});
  text.append(sections);
  return text;
}

[[nodiscard]] inline std::string two_profile_configuration() {
  std::string sections{kFirstProfileSection};
  sections.append(kHazardSection);
  sections.append(kSecondProfileSection);
  return configuration(kMixedRoster, kDefaultMode, sections);
}

[[nodiscard]] inline std::string profile_sections(const std::size_t count) {
  std::string sections;
  for (std::size_t index = 0; index < count; ++index) {
    sections.append(
        test_fixture::replace_once(std::string{kFirstProfileSection}, kFirstProfileHeader,
                                   "[bot_profile.authored_" + std::to_string(index) + "]"));
  }
  return sections;
}

[[nodiscard]] inline std::vector<MatchConfiguration::BotRosterEntry>
profiled_roster(const std::size_t count) {
  std::vector<MatchConfiguration::BotRosterEntry> entries;
  for (std::size_t index = 0; index < count; ++index) {
    entries.push_back({std::string{kProfileKind}, 1,
                       simulation::BotProfileName::create("authored_" + std::to_string(index))});
  }
  return entries;
}

[[nodiscard]] inline std::string
roster_text(const std::vector<MatchConfiguration::BotRosterEntry>& entries) {
  std::string text;
  for (const auto& entry : entries) {
    if (!text.empty()) {
      text.append(",");
    }
    text.append(entry.controller_kind);
    if (entry.profile_name.has_value()) {
      text.append("@");
      text.append(entry.profile_name->value());
    }
    text.append(":" + std::to_string(entry.count));
  }
  return text;
}

[[nodiscard]] inline ApplicationConfig
load(const test_fixture::TemporaryApplicationInputWorkspace& workspace,
     const std::string_view text) {
  const auto result = test_fixture::load_application_config(
      workspace.write_file("tactical-profile-configuration.cfg", text));
  return std::get<ApplicationConfigLoader::RunRequest>(result).application_config();
}

[[nodiscard]] inline MatchConfiguration
match_with_roster(std::vector<MatchConfiguration::BotRosterEntry> entries,
                  const std::string_view mode = kDefaultMode) {
  return MatchConfiguration::create(std::string{mode}, "arena-960x640", "maps", 1, 4,
                                    std::move(entries));
}

[[nodiscard]] inline ApplicationConfig
application_with_match(const ApplicationConfig& baseline, MatchConfiguration match,
                       controllers::TacticalProfileCatalogue profiles) {
  return ApplicationConfig::create(baseline.server_config(), baseline.simulation_config(),
                                   std::move(match), baseline.game_mode_configuration(),
                                   baseline.lobbies_configuration(), std::move(profiles));
}

} // namespace blob_royale::application::tactical_profile_configuration_fixture

#endif
