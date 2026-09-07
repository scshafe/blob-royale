#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_ZONE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_ZONE_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "vector2.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: zone_component -- one circular safe area, carried by its own entity.
//
// The royale safe zone is **an entity with a component**, not a field on the world or a member of
// `MatchState` (`docs/architecture/0005-royale-mode.md` § "Where zone and elimination state live";
// `docs/architecture/0004-gameplay-architecture.md` § "Snapshots and protocol shape"). Registering
// it here buys three generated behaviors a mode-owned field would not get: structural `operator==`
// over the whole world, erasure by `destroy_entity`, and snapshot participation. The third is the
// one that matters most -- the radius a client renders is exactly the radius `zone_elimination`
// tested against on the tick being rendered, because both read one committed value.
//
// The zone entity owns no `PhysicsBody` and no `Controllable`, so it never enters a contact pair,
// never integrates, is never counted alive, and is never wiped between matches. It persists for the
// life of the simulation and `zone_shrink` rewrites this component every tick.
//
// A moving zone, a second zone, or a per-team zone is a value change or a second entity carrying
// this component; none of the three needs a new component kind, a new encoder, or a new renderer.
//
// This is a `blob_simulation` value struct with no behavior. Everything the zone *does* --
// shrinking, and eliminating the players outside it -- is `blob_gameplay` systems, which is the
// "values live in blob_simulation, rules live in blob_gameplay" split of
// `docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a new thing goes"
// applied literally.
// related: component_registry.hpp -- the closed list this kind is registered in.
// related: components/zone_exposure_component.hpp -- the per-entity grace counter against it.
struct Zone final {
  // World units. The centre never moves under `royale`; it is a field rather than a derived arena
  // property so a moving zone is a value change instead of an architecture change.
  Vector2 center;
  // World units. A player centre farther than this from `center`, beyond `kPositionTolerance`, is
  // outside.
  double radius{};

  friend bool operator==(const Zone&, const Zone&) = default;
};

template <> struct ComponentKindName<Zone> {
  static constexpr std::string_view value = "zone";
};

} // namespace blob_royale::simulation

#endif
