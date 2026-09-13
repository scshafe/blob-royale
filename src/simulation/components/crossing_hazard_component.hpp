#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_CROSSING_HAZARD_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_CROSSING_HAZARD_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_lifetime.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: crossing_hazard_component -- this body's membership in the active crossing population.
// Presence is the entire value. Runtime admission counts this sparse store rather than guessing
// from lethality, radius, or lifetime, which other mechanics may also use. Removing the body clears
// membership through the shared body-bound sweep; destroying the entity clears it by registration.
// related: ../../gameplay/shared/hazard_spawn_system.hpp -- active crossing capacity admission.
// related: ../../gameplay/shared/create_crossing_hazard.hpp -- the canonical writer.
struct CrossingHazard final {
  friend bool operator==(const CrossingHazard&, const CrossingHazard&) = default;
};

template <> struct ComponentKindName<CrossingHazard> {
  static constexpr std::string_view value = "crossing_hazard";
};

template <> struct ComponentLifetime<CrossingHazard> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
