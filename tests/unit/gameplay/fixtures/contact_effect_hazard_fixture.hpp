#ifndef BLOB_ROYALE_TESTING_CONTACT_EFFECT_HAZARD_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_CONTACT_EFFECT_HAZARD_FIXTURE_HPP

#include "shared/hazard_archetype.hpp"

#include <array>
#include <optional>

namespace blob_royale::testing::contact_effect_hazard_fixture {

inline constexpr std::array kPolicies{simulation::ContactEffectPolicy::kClosingImpact,
                                      simulation::ContactEffectPolicy::kAnyTouch};
inline constexpr std::array<std::optional<simulation::ContactEffectPolicy>, 3> kOverrides{
    std::nullopt, simulation::ContactEffectPolicy::kClosingImpact,
    simulation::ContactEffectPolicy::kAnyTouch};
inline constexpr auto kInvalidPolicy = static_cast<simulation::ContactEffectPolicy>(73);

[[nodiscard]] inline gameplay::HazardArchetype
archetype(const simulation::ContactEffectPolicy policy) {
  return gameplay::HazardArchetype::create(
      {"velvet_boulder", 26.0, 40.0, 0.2, 200.0, 0.05, false, policy});
}

} // namespace blob_royale::testing::contact_effect_hazard_fixture

#endif
