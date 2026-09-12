#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_CATALOGUE_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_CATALOGUE_HPP

#include "tactical_profile.hpp"

#include <span>
#include <vector>

namespace blob_royale::controllers {

// canonical: tactical_profile_catalogue -- ordered immutable authored profiles, never defaults.
class TacticalProfileCatalogue final {
public:
  TacticalProfileCatalogue() = default;
  // Rejects more than the shared 16-profile bound or duplicate names with CONTROLLERS.* errors.
  [[nodiscard]] static TacticalProfileCatalogue create(std::vector<TacticalProfile> profiles);
  [[nodiscard]] std::span<const TacticalProfile> profiles() const& noexcept { return profiles_; }
  std::span<const TacticalProfile> profiles() const&& = delete;
  // nullptr is an unknown name, not a substituted profile. The pointer borrows this catalogue.
  [[nodiscard]] const TacticalProfile* find(const simulation::BotProfileName& name) const& noexcept;
  const TacticalProfile* find(const simulation::BotProfileName&) const&& = delete;
  friend bool operator==(const TacticalProfileCatalogue&,
                         const TacticalProfileCatalogue&) = default;

private:
  explicit TacticalProfileCatalogue(std::vector<TacticalProfile> profiles) noexcept;
  std::vector<TacticalProfile> profiles_;
};

} // namespace blob_royale::controllers

#endif
