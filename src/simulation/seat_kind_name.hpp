#ifndef BLOB_ROYALE_SIMULATION_SEAT_KIND_NAME_HPP
#define BLOB_ROYALE_SIMULATION_SEAT_KIND_NAME_HPP

#include "bounded_name.hpp"
#include "seat_kind_name_policy.hpp"

namespace blob_royale::simulation {

// canonical: seat_kind_name -- owned bounded NPC kind, including the existing unnamed sentinel.
using SeatKindName = BoundedName<SeatKindNamePolicy>;

} // namespace blob_royale::simulation

#endif
