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
  // Ordinal order *is* declared key order: the four weight keys are declared in
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
  // **Every weight at zero is an omission, not a personality.** Each zero is individually legal --
  // "this profile does not care about that objective" -- but all four together is the one authored
  // value indistinguishable from a caller left behind by this setting, because C++ fills an omitted
  // aggregate initializer with zeros rather than refusing to compile, and the parser cannot catch
  // that: it only sees the keys a `.cfg` did or did not write. It is also the only weight set that
  // makes selection inexpressive -- with no positive term every candidate scores alike and the
  // choice collapses onto the stable kind ordinal, which is a bot deciding by tie-break. A profile
  // that genuinely has no preference authors equal *positive* weights and keeps distance ordering.
  // The context names the section rather than a key, because no single key is at fault.
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
  return TacticalProfile(simulation::BotProfileName::create(section.profile_name), section);
}

TacticalProfile::TacticalProfile(simulation::BotProfileName name, const Section& section) noexcept
    : name_(name), objective_seek_probability_(section.objective_seek_probability),
      reaction_delay_ticks_(section.reaction_delay_ticks), aim_error_(section.aim_error),
      target_persistence_ticks_(section.target_persistence_ticks),
      objective_weights_(section.objective_weights), risk_tolerance_(section.risk_tolerance),
      prediction_horizon_ticks_(section.prediction_horizon_ticks) {}

} // namespace blob_royale::controllers
