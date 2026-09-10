#include "race/course_publisher_system.hpp"

#include "game_world.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race/race_mode_state.hpp"

#include <memory>
#include <utility>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
CoursePublisherSystem::create(RaceCourse course, RaceConfiguration configuration) {
  return std::make_unique<const CoursePublisherSystem>(std::move(course), std::move(configuration));
}

CoursePublisherSystem::CoursePublisherSystem(RaceCourse course,
                                             RaceConfiguration configuration) noexcept
    : course_(std::move(course)), configuration_(std::move(configuration)) {}

void CoursePublisherSystem::apply(simulation::GameWorld& world,
                                  const simulation::TickContext&) const {
  simulation::RaceModeState& block = race_mode_state_in(world);
  block.track_half_width = course_.track_half_width();
  block.checkpoint_radius = course_.checkpoint_radius();
  block.track.assign(course_.track().begin(), course_.track().end());
  block.checkpoints.assign(course_.checkpoints().begin(), course_.checkpoints().end());
  block.time_limit_ticks = configuration_.time_limit_ticks();
  block.finish_window_ticks = configuration_.finish_window_ticks();
}

} // namespace blob_royale::gameplay
