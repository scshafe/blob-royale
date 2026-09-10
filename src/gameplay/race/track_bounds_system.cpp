#include "race/track_bounds_system.hpp"

#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/roster.hpp"
#include "simulation_limits.hpp"

#include <memory>
#include <utility>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem> TrackBoundsSystem::create(RaceCourse course) {
  return std::make_unique<const TrackBoundsSystem>(std::move(course));
}

TrackBoundsSystem::TrackBoundsSystem(RaceCourse course) noexcept : course_(std::move(course)) {}

void TrackBoundsSystem::apply(simulation::GameWorld& world, const simulation::TickContext&) const {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }
  for (const simulation::EntityId entity : alive_entities(world)) {
    const simulation::PhysicsBody& body = *world.store<simulation::PhysicsBody>().find(entity);
    if (course_.distance_to_centreline(body.position()) >
        course_.track_half_width() + simulation::kPositionTolerance) {
      world.emit(simulation::EliminationEvent{entity});
    }
  }
}

} // namespace blob_royale::gameplay
