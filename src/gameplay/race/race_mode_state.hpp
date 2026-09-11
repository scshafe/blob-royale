#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_MODE_STATE_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_MODE_STATE_HPP

#include "game_world.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_course.hpp"

#include <span>
#include <variant>

namespace blob_royale::gameplay {

// canonical: race_mode_state_access -- the race block's initial state and member access.
//
// A world carrying another arm has no recorded finishes, exactly as a race before its first
// tick. Readers expose only standings; declared course and clock values belong to each rule's
// immutable constructor state. Writers install the race arm once, then mutate only their members.
// related: standings_recorder_system.hpp -- observed member writer.
// related: course_publisher_system.hpp -- declared member writer.
[[nodiscard]] inline std::span<const simulation::RaceStanding>
race_standings_of(const simulation::GameWorld& world) noexcept {
  if (const auto* held = std::get_if<simulation::RaceModeState>(&world.match().mode_state);
      held != nullptr) {
    return held->standings;
  }
  return {};
}

// The course has already validated exact terrain membership. Initializing here preserves the
// recorder-before-publisher lifecycle order without an empty or inferred road-name sentinel.
[[nodiscard]] inline simulation::RaceModeState& race_mode_state_in(simulation::GameWorld& world,
                                                                  const RaceCourse& course) {
  simulation::ModeMatchState& mode_state = world.mutable_match().mode_state;
  if (auto* held = std::get_if<simulation::RaceModeState>(&mode_state); held != nullptr) {
    return *held;
  }
  return mode_state.emplace<simulation::RaceModeState>(
      simulation::RaceModeState{.road = simulation::RaceRoadName::create(course.road_name())});
}

} // namespace blob_royale::gameplay

#endif
