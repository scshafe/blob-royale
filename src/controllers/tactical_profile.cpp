#include "tactical_profile.hpp"

#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <cmath>
#include <cstddef>
#include <string>

namespace blob_royale::controllers {

TacticalProfile TacticalProfile::create(const Section& section) {
  if (!simulation::is_wire_kind_name(section.profile_name)) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileNameInvalid,
                                     "bot_profile.profile_name",
                                     "profile name must be lower snake case, 1..64 bytes");
  }
  const std::string context = "bot_profile." + section.profile_name + ".";
  if (!std::isfinite(section.objective_seek_probability) ||
      section.objective_seek_probability < 0.0 ||
      section.objective_seek_probability > kMaximumTacticalObjectiveSeekProbability) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                                     context + "objective_seek_probability",
                                     "seek probability must be finite in [0,1]");
  }
  if (section.reaction_delay_ticks > kMaximumTacticalReactionDelayTicks) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfileReactionDelayInvalid,
        context + "reaction_delay_ticks", "reaction delay must be an integer in [0,4000]");
  }
  if (!std::isfinite(section.aim_error) || section.aim_error < 0.0 ||
      section.aim_error > kMaximumTacticalAimError) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                                     context + "aim_error", "aim error must be finite in [0,0.25]");
  }
  if (section.target_persistence_ticks > kMaximumTacticalTargetPersistenceTicks) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfilePersistenceInvalid,
                                     context + "target_persistence_ticks",
                                     "target persistence must be an integer in [0,4000]");
  }
  // Ordinal order *is* declared key order: the five weight keys are declared in
  // `TacticalObjectiveKind` order in `kConfigFamilyFieldSpecs`, which is the same stable ordinal
  // the candidate tie-break already uses, so no reader keeps a second ordering and a section with
  // two bad weights blames the same one at every layer. Walking ordinals rather than a written list
  // of kinds is what keeps this loop from falling out of step with the enum;
  // `tactical_objective_weight_of` is what keeps the enum from growing past it unnoticed.
  bool weighted = false;
  for (std::size_t ordinal = 0; ordinal < kTacticalObjectiveKindCount; ++ordinal) {
    const auto kind = static_cast<TacticalObjectiveKind>(ordinal);
    const double weight = tactical_objective_weight_of(section.objective_weights, kind);
    if (!std::isfinite(weight) || weight < 0.0 || weight > kMaximumTacticalObjectiveWeight) {
      throw ControllersValidationError(
          ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid,
          context + std::string{tactical_objective_weight_key(kind)},
          "objective weight must be finite in [0,1]");
    }
    weighted = weighted || weight > 0.0;
  }
  // **Every weight at zero expresses nothing, so it is refused.** Each zero is individually legal
  // -- "this profile does not care about that objective" -- but all five together is the one
  // authored set that makes selection inexpressive: with no positive term every candidate scores
  // alike and the choice collapses onto the stable kind ordinal, which is a bot deciding by
  // tie-break. A profile that genuinely has no preference authors equal *positive* weights and
  // keeps distance ordering. The context names the section rather than a key, because no single
  // key is at fault.
  //
  // **What this rejection no longer has to catch is a C++ caller left behind.** It used to be the
  // only thing between a construction site that omitted a weight and a bot that silently ignored an
  // objective, and it was never equal to that job: it fires only when *every* weight is zero, so an
  // omission sitting beside four authored numbers passed straight through it. That hole is closed
  // one layer up and at the site itself, by `AuthoredObjectiveWeight` having no default constructor
  // (`tactical_profile.hpp`), which is why this rule may now be read as what it always should have
  // been alone -- a domain rule about an authored section, the only remaining way five zeros
  // arrive.
  if (!weighted) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfileObjectiveWeightsDegenerate,
        "bot_profile." + section.profile_name, "at least one objective weight must be positive");
  }
  if (!std::isfinite(section.risk_tolerance) || section.risk_tolerance < 0.0 ||
      section.risk_tolerance > kMaximumTacticalRiskTolerance) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfileRiskToleranceInvalid, context + "risk_tolerance",
        "risk tolerance must be finite in [0,1]");
  }
  if (section.prediction_horizon_ticks > kMaximumTacticalPredictionHorizonTicks) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid,
        context + "prediction_horizon_ticks", "prediction horizon must be an integer in [0,400]");
  }
  if (!std::isfinite(section.charge_screen_diagonal_fraction) ||
      section.charge_screen_diagonal_fraction < 0.0 ||
      section.charge_screen_diagonal_fraction > kMaximumTacticalChargeScreenDiagonalFraction) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileChargeScreenInvalid,
                                     context + "charge_screen_diagonal_fraction",
                                     "charge screen must be finite in [0,1]");
  }
  if (section.shield_anticipation_ticks > kMaximumTacticalShieldAnticipationTicks) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid,
        context + "shield_anticipation_ticks", "shield anticipation must be an integer in [0,40]");
  }
  // **The one fraction in this family whose zero is refused, and the domain is `RacerController`'s
  // rather than a new one.** The race provider recovers when
  // `nearest.distance > fraction * road->half_width()`, so a zero recovers unless the body sits
  // exactly on the centreline: it inverts race behaviour instead of disabling it. Refusing it is
  // also what turns a positional `Section` site that value-initialized this trailing double into a
  // named throw at the site that made it, which is the protection `AuthoredObjectiveWeight` gives
  // the weights and nothing gives a bare `double`. `kMinimumProfileValues` in
  // `tactical_profile_configuration_fixture.hpp` therefore cannot author a zero for this one key as
  // it does for every other fraction, and authors the inclusive upper bound instead: the low end
  // this key accepts is not zero, so a zero there would be testing the rejection, not the bound.
  if (!std::isfinite(section.road_caution_fraction) ||
      section.road_caution_fraction <= kMinimumRacerCautionFraction ||
      section.road_caution_fraction > kMaximumRacerCautionFraction) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileRoadCautionInvalid,
                                     context + "road_caution_fraction",
                                     "road caution must be finite in (0,1]");
  }
  if (!std::isfinite(section.arrival_brake_fraction) || section.arrival_brake_fraction < 0.0 ||
      section.arrival_brake_fraction > kMaximumTacticalArrivalBrakeFraction) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileArrivalBrakeInvalid,
                                     context + "arrival_brake_fraction",
                                     "arrival brake must be finite in [0,1]");
  }
  if (!std::isfinite(section.exposure_preference) || section.exposure_preference < 0.0 ||
      section.exposure_preference > kMaximumTacticalExposurePreference) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfileExposurePreferenceInvalid,
        context + "exposure_preference", "exposure preference must be finite in [0,1]");
  }
  if (!std::isfinite(section.minimum_opening) || section.minimum_opening < 0.0 ||
      section.minimum_opening > kMaximumTacticalMinimumOpening) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalProfileMinimumOpeningInvalid,
        context + "minimum_opening", "minimum opening must be finite in [0,1]");
  }
  return TacticalProfile(simulation::BotProfileName::create(section.profile_name), section);
}

TacticalProfile::TacticalProfile(simulation::BotProfileName name, const Section& section) noexcept
    : name_(name), objective_seek_probability_(section.objective_seek_probability),
      reaction_delay_ticks_(section.reaction_delay_ticks), aim_error_(section.aim_error),
      target_persistence_ticks_(section.target_persistence_ticks),
      objective_weights_(section.objective_weights), risk_tolerance_(section.risk_tolerance),
      prediction_horizon_ticks_(section.prediction_horizon_ticks),
      charge_screen_diagonal_fraction_(section.charge_screen_diagonal_fraction),
      shield_anticipation_ticks_(section.shield_anticipation_ticks),
      road_caution_fraction_(section.road_caution_fraction),
      arrival_brake_fraction_(section.arrival_brake_fraction),
      exposure_preference_(section.exposure_preference), minimum_opening_(section.minimum_opening) {
}

} // namespace blob_royale::controllers
