#include "shared/hazard_archetype.hpp"

#include "contact_effect_admission.hpp"
#include "gameplay_validation_error.hpp"
#include "shared/duration_ticks.hpp"
#include "snake_case_identity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::gameplay {
namespace {

// The `hazard.<kind>.<key>` form the section is authored in, so a rejection reads straight back
// onto a line of the configuration file.
[[nodiscard]] std::string hazard_context(const std::string& kind_name, const std::string_view key) {
  std::string context{"hazard."};
  context.append(kind_name);
  context.push_back('.');
  context.append(key);
  return context;
}

// The kind-name grammar is `simulation::is_wire_kind_name`, not a copy of it. It used to be
// stated here on the argument that `blob_gameplay` may not depend on `blob_application`; that is
// true and was the wrong conclusion, because both depend on `blob_simulation`, which is where the
// one implementation now lives (`src/simulation/snake_case_identity.hpp`).

void require_finite(const double value, const std::string& kind_name, const std::string_view key) {
  if (!std::isfinite(value)) {
    throw GameplayValidationError(GameplayValidationCode::kHazardScalarNotFinite,
                                  hazard_context(kind_name, key),
                                  "every [hazard.<kind>] value must be a finite number");
  }
}

// Strictly positive rather than merely not negative: a hazard with no size, no mass, or no speed is
// not a hazard, and zero mass would divide by zero in the impulse equation the body is headed for.
void require_finite_and_positive(const double value, const std::string& kind_name,
                                 const std::string_view key) {
  require_finite(value, kind_name, key);
  if (value <= 0.0) {
    throw GameplayValidationError(GameplayValidationCode::kHazardScalarOutOfRange,
                                  hazard_context(kind_name, key),
                                  std::to_string(value) + " must be greater than zero");
  }
}

} // namespace

HazardArchetype HazardArchetype::create(const Section& section) {
  if (!simulation::is_wire_kind_name(section.kind_name)) {
    throw GameplayValidationError(
        GameplayValidationCode::kHazardKindNameInvalid, "hazard." + section.kind_name,
        "hazard kind name must match the published kind grammar: lower snake case, first "
        "character a letter, at most 64 characters");
  }
  require_finite_and_positive(section.radius_world_units, section.kind_name, "radius_world_units");
  require_finite_and_positive(section.mass, section.kind_name, "mass");
  require_finite(section.restitution, section.kind_name, "restitution");
  if (section.restitution < 0.0 || section.restitution > 1.0) {
    throw GameplayValidationError(GameplayValidationCode::kHazardScalarOutOfRange,
                                  hazard_context(section.kind_name, "restitution"),
                                  std::to_string(section.restitution) +
                                      " must be within [0, 1]: a contact cannot absorb less than "
                                      "none of the closing speed or return more than all of it");
  }
  require_finite_and_positive(section.speed_world_units_per_second, section.kind_name,
                              "speed_world_units_per_second");
  // Converted through the one shared conversion, which rejects a non-finite, negative, or
  // unrepresentable duration on its own. What is left for this value to say is that the interval
  // must be at least one whole tick: `0.001 s` rounds to zero ticks, and a zero interval spawns a
  // body every tick until the entity budget runs out.
  const std::uint64_t spawn_interval_ticks = duration_ticks(
      section.spawn_interval_seconds, hazard_context(section.kind_name, "spawn_interval_seconds"));
  if (spawn_interval_ticks == 0) {
    throw GameplayValidationError(
        GameplayValidationCode::kHazardScalarOutOfRange,
        hazard_context(section.kind_name, "spawn_interval_seconds"),
        std::to_string(section.spawn_interval_seconds) +
            " s rounds to zero ticks, which would spawn one hazard of this kind every tick");
  }
  simulation::validate_contact_effect_policy(section.contact_effect_policy);
  return HazardArchetype(section.kind_name, section.radius_world_units, section.mass,
                         section.restitution, section.speed_world_units_per_second,
                         spawn_interval_ticks, section.lethal_on_contact,
                         section.contact_effect_policy);
}

HazardArchetype::HazardArchetype(std::string kind_name, const double radius, const double mass,
                                 const double restitution, const double speed,
                                 const std::uint64_t spawn_interval_ticks,
                                 const bool lethal_on_contact,
                                 const simulation::ContactEffectPolicy contact_effect_policy)
    : kind_name_(std::move(kind_name)), radius_(radius), mass_(mass), restitution_(restitution),
      speed_(speed), spawn_interval_ticks_(spawn_interval_ticks),
      lethal_on_contact_(lethal_on_contact), contact_effect_policy_(contact_effect_policy) {}

} // namespace blob_royale::gameplay
