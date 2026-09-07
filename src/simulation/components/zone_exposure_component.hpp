#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_ZONE_EXPOSURE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_ZONE_EXPOSURE_COMPONENT_HPP

#include "component_kind_name.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: zone_exposure_component -- how many consecutive ticks one entity has been outside a
// zone.
//
// **An absent `ZoneExposure` reads as zero.** That is the whole reason this is a component rather
// than a field on `MatchState`: nothing has to seed a counter at spawn, an eliminated entity's
// counter dies with the entity through `destroy_entity`, and a controller that rejoins under a new
// `EntityId` carries no partial grace (`docs/architecture/0005-royale-mode.md` § "Where zone and
// elimination state live"). `zone_elimination` therefore *erases* the entry when an entity is
// inside rather than storing an explicit zero, so one world state has one spelling.
//
// State that is per entity and durable across ticks is a component by the rule
// `docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages" states, and this counter could not have been match state in any case: `MatchState` is for
// match-wide state.
//
// Being a component also makes the counter snapshot-visible, so two runs that diverge in exposure
// are comparable at the first differing tick instead of being invisible until someone is
// eliminated.
// related: components/zone_component.hpp -- the circle this counts exposure to.
// related: component_registry.hpp -- the closed list this kind is registered in.
struct ZoneExposure final {
  // Consecutive committed ticks this entity's centre has been outside the zone. Reset to absent by
  // re-entering, so partial grace is lost rather than banked.
  std::uint64_t outside_ticks{};

  friend bool operator==(const ZoneExposure&, const ZoneExposure&) = default;
};

template <> struct ComponentKindName<ZoneExposure> {
  static constexpr std::string_view value = "zone_exposure";
};

} // namespace blob_royale::simulation

#endif
