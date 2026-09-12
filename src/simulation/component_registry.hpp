#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_REGISTRY_HPP

#include "component_list.hpp"
#include "components/contact_effect_admission_component.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "components/score_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "components/team_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "physics_body.hpp"

namespace blob_royale::simulation {

// canonical: component_registry -- the closed, ordered list of component kinds.
// @extension-point entity_component
//
// Adding a kind is a new header under `src/simulation/components/` declaring the value struct and
// its ComponentKindName, plus one type in this list. World equality, GameWorld::destroy_entity,
// and WorldSnapshot construction are generated from this list, so a new kind cannot forget to
// participate in any of them. The list is closed at compile time and ordered by declaration, so no
// iteration order depends on a runtime registry.
//
// The first five kinds are everything the engine itself needs. A mode contributes further kinds to
// the same one registry, which is why the list is closed at compile time but not fixed for all
// time: `Zone` and `ZoneExposure` are royale's, added by plan Step 21 as two headers plus this one
// line, with no other kernel file edited
// (`docs/architecture/0005-royale-mode.md` § "Where zone and elimination state live").
//
// Two implementations of the seam beyond the engine set: `Zone` for the royale safe zone, and
// `Flag` for capture the flag.
//
// `LethalOnContact` is the third, and it is the one that proves the seam is not royale-shaped: it
// is declared by no mode's directory at all but by `src/gameplay/shared/`, because an object that
// kills on touch is a mechanic any mode may field. It is also the first kind whose presence is its
// whole value, which is why it publishes an empty object rather than a synthetic flag member
// (`components/lethal_on_contact_component.hpp`).
//
// `RespawnTimer` is the fourth and belongs to `src/gameplay/shared/` for the same reason: a
// player who comes back after being knocked out of play is a mechanic the second and third
// competitive modes both field and royale never does
// (`components/respawn_timer_component.hpp`).
//
// `Hill` and `HillPresence` are king of the hill's, the way `Zone` and `ZoneExposure` are royale's:
// two headers and this one line (`components/hill_component.hpp`). `RaceProgress` is race's
// ordered gate counter, retained while its racer awaits a body
// (`components/race_progress_component.hpp`).
//
// `Shield` is the latest, and it belongs to `src/gameplay/shared/` for the same reason
// `LethalOnContact` and `RespawnTimer` do: a body that can raise a guard for a moment is a
// mechanic any mode may field, and every mode that fields abilities declares the one system that
// writes it. It cost the same one header plus this one line, with no kernel file edited
// (`components/shield_component.hpp`).
using ComponentRegistry =
    ComponentList<PhysicsBody, Controllable, Lifetime, Score, Team, Zone, ZoneExposure,
                  LethalOnContact, RespawnTimer, Hill, HillPresence, RaceProgress, HillMotion, Stun,
                  ContactEffectAdmission, Shield>;

} // namespace blob_royale::simulation

#endif
