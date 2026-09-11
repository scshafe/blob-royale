#ifndef BLOB_ROYALE_SIMULATION_RACE_ROAD_NAME_HPP
#define BLOB_ROYALE_SIMULATION_RACE_ROAD_NAME_HPP

#include "bounded_name.hpp"
#include "race_road_name_policy.hpp"

namespace blob_royale::simulation {

// canonical: race_road_name -- the owned, required identity of a race's bound terrain corridor.
// Create from a non-empty published snake_case name or receive SIMULATION.RACE_ROAD_NAME_INVALID.
// Unlike contact/seat sentinels this name cannot be default-constructed: a race arm is created only
// with an explicit binding. The value owns its bytes across copied worlds and retained snapshots;
// terrain membership is checked where the bound course or snapshot terrain is available.
// related: mode_states/race_mode_state.hpp -- the race arm carrying this identity, not geometry.
using RaceRoadName = BoundedName<RaceRoadNamePolicy>;

} // namespace blob_royale::simulation

#endif
