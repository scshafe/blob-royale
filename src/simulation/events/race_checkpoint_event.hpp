#ifndef BLOB_ROYALE_SIMULATION_EVENTS_RACE_CHECKPOINT_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_RACE_CHECKPOINT_EVENT_HPP

#include "entity_id.hpp"
#include "motion_event_order.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: race_checkpoint_event -- certified ordered-gate credit within the current tick.
// The resulting count advances by one. The solver's normalized time is retained unchanged for
// finish ranking; consumers never re-detect occupancy or reconstruct a root from a position.
struct RaceCheckpointEvent final {
  EntityId entity;
  std::uint64_t next_checkpoint;
  MotionTime tick_offset;

  friend bool operator==(const RaceCheckpointEvent&, const RaceCheckpointEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
