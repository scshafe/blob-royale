#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_CONTACT_EFFECT_ADMISSION_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_CONTACT_EFFECT_ADMISSION_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_lifetime.hpp"
#include "motion_contact_observation.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: contact_effect_admission_component -- sparse, body-bound nondefault effect policy.
// Only kAnyTouch may be stored. Absence means kClosingImpact. Assignment and publication validate
// through contact_effect_admission.hpp; this never changes collision impulse admission.
struct ContactEffectAdmission final {
  ContactEffectPolicy policy = ContactEffectPolicy::kAnyTouch;
  friend bool operator==(const ContactEffectAdmission&, const ContactEffectAdmission&) = default;
};

template <> struct ComponentKindName<ContactEffectAdmission> {
  static constexpr std::string_view value = "contact_effect_admission";
};

template <> struct ComponentLifetime<ContactEffectAdmission> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
