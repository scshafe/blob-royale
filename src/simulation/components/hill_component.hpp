#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_HILL_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_HILL_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "vector2.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: hill_component -- one circular scoring area, carried by its own entity.
//
// The same shape as `Zone` and a different meaning, which is why it is a different kind: inside a
// hill is where a player scores, inside a zone is where a player is safe, and a client keyed by
// kind must draw the two apart and count nothing against the wrong one. A mode that fields a hill
// inside a shrinking zone needs both at once on distinct kinds
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Considered Options").
//
// Like the zone, the hill is **an entity with a component**, not a field on the world: registering
// it buys structural `operator==`, erasure by `destroy_entity`, and snapshot participation, and the
// third is the one that matters -- the circle a client draws is exactly the circle `hill_scoring`
// tested against on the tick being rendered. The hill entity owns no `PhysicsBody` and no
// `Controllable`, so it never collides, never enters kernel integration, is never a participant,
// and is never wiped. `hill_movement` creates it once and rewrites this component every tick.
//
// This is a `blob_simulation` value struct with no behavior. Everything the hill *does* -- touring
// the map's `hill` markers or roaming independently of terrain, and scoring the players inside it
// -- is `blob_gameplay` systems. The circle never creates floor or protects players from cliffs.
// related: component_registry.hpp -- the closed list this kind is registered in.
// related: components/hill_presence_component.hpp -- the per-entity counter against it.
// related: ../../gameplay/king_of_the_hill/hill_movement_system.hpp -- the only writer.
struct Hill final {
  // World units. The configured motion policy's committed center; scoring and rendering agree.
  Vector2 center;
  // World units. A player centre no farther than this from `center`, within `kPositionTolerance`,
  // is inside.
  double radius{};

  friend bool operator==(const Hill&, const Hill&) = default;
};

template <> struct ComponentKindName<Hill> {
  static constexpr std::string_view value = "hill";
};

} // namespace blob_royale::simulation

#endif
