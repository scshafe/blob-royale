#ifndef BLOB_ROYALE_SIMULATION_MODE_STATES_RACE_MODE_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MODE_STATES_RACE_MODE_STATE_HPP

#include "controller_id.hpp"
#include "entity_id.hpp"
#include "race_road_name.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <vector>

namespace blob_royale::simulation {

// One recorded finish. Racers finishing on the same tick share a placement, in ascending entity
// order; the controller remains recognizable after the entity is reset or disconnected.
struct RaceStanding final {
  EntityId entity;
  ControllerId controller;
  std::uint64_t placement{};
  TickSequence finished_tick;

  friend bool operator==(const RaceStanding&, const RaceStanding&) = default;
};

// canonical: race_mode_state -- the selected road, gates, durations, and observed finishes.
//
// The required road identity is bound explicitly when the race arm is created; its geometry lives
// only in the map's terrain. `course_publisher` stamps declared members last at kLifecycle, while
// `standings_recorder` owns the observed standings. A racer's gate and return countdown are its
// RaceProgress and RespawnTimer components; finishes stay here because they outlive a racer.
// related: ../../gameplay/race/course_publisher_system.hpp -- declared-state writer.
// related: ../../gameplay/race/standings_recorder_system.hpp -- observed-state writer.
struct RaceModeState final {
  RaceRoadName road;
  double checkpoint_radius{};
  std::vector<Vector2> checkpoints{};
  std::uint64_t time_limit_ticks{};
  std::uint64_t finish_window_ticks{};
  std::vector<RaceStanding> standings{};

  friend bool operator==(const RaceModeState&, const RaceModeState&) = default;
};

} // namespace blob_royale::simulation

#endif
