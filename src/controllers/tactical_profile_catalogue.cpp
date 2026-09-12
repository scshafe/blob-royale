#include "tactical_profile_catalogue.hpp"

#include "controllers_validation_error.hpp"
#include "simulation_limits.hpp"

#include <utility>

namespace blob_royale::controllers {

TacticalProfileCatalogue TacticalProfileCatalogue::create(std::vector<TacticalProfile> profiles) {
  if (profiles.size() > simulation::kMaximumNpcProfileCount) {
    throw ControllersValidationError(ControllersValidationCode::kTacticalProfileCatalogueFull,
                                     "bot_profiles",
                                     "at most 16 tactical profiles may be authored");
  }
  for (std::size_t index = 0; index < profiles.size(); ++index) {
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (profiles[previous].name() == profiles[index].name()) {
        throw ControllersValidationError(
            ControllersValidationCode::kTacticalProfileNameDuplicate, "bot_profiles",
            "duplicate profile name " + std::string{profiles[index].name().value()});
      }
    }
  }
  return TacticalProfileCatalogue(std::move(profiles));
}

TacticalProfileCatalogue::TacticalProfileCatalogue(std::vector<TacticalProfile> profiles) noexcept
    : profiles_(std::move(profiles)) {}

const TacticalProfile*
TacticalProfileCatalogue::find(const simulation::BotProfileName& name) const& noexcept {
  for (const auto& profile : profiles_) {
    if (profile.name() == name) {
      return &profile;
    }
  }
  return nullptr;
}

} // namespace blob_royale::controllers
