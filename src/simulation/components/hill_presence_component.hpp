#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_HILL_PRESENCE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_HILL_PRESENCE_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_lifetime.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: hill_presence_component -- how many consecutive ticks one entity has held the hill
// toward its next point.
//
// **An absent `HillPresence` reads as zero**, for the reasons `ZoneExposure` is spelled the same
// way: nothing seeds a counter at spawn, a knocked-out entity's partial point is forgotten by the
// shared body-bound cleanup, and one world state has one spelling. `hill_scoring` erases the
// entry when the entity leaves the hill or when the counter rolls over into a `Score` point, and
// leaves it exactly as it is while the hill is contested
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Scoring").
//
// Being a component makes the counter snapshot-visible, which is what lets a client draw the
// local player's progress toward the next point as a ring against the published
// `point_interval_ticks`, and lets two runs that diverge in presence diverge at the first
// differing tick.
// related: components/hill_component.hpp -- the circle this counts presence in.
// related: component_registry.hpp -- the closed list this kind is registered in.
struct HillPresence final {
  // Consecutive committed ticks this entity's centre has been inside the hill and scoring.
  std::uint64_t inside_ticks{};

  friend bool operator==(const HillPresence&, const HillPresence&) = default;
};

template <> struct ComponentKindName<HillPresence> {
  static constexpr std::string_view value = "hill_presence";
};

template <> struct ComponentLifetime<HillPresence> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
