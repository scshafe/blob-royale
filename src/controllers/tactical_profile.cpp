#include "tactical_profile.hpp"

#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <cmath>

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
  return TacticalProfile(simulation::BotProfileName::create(section.profile_name), section);
}

TacticalProfile::TacticalProfile(simulation::BotProfileName name, const Section& section) noexcept
    : name_(name), objective_seek_probability_(section.objective_seek_probability),
      reaction_delay_ticks_(section.reaction_delay_ticks), aim_error_(section.aim_error),
      target_persistence_ticks_(section.target_persistence_ticks) {}

} // namespace blob_royale::controllers
