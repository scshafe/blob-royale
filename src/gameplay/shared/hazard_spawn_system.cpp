#include "shared/hazard_spawn_system.hpp"

#include "component_store.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/create_crossing_hazard.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
HazardSpawnSystem::create(std::vector<HazardArchetype> archetypes) {
  return std::make_unique<const HazardSpawnSystem>(std::move(archetypes));
}

void HazardSpawnSystem::apply(simulation::GameWorld& world,
                              const simulation::TickContext& context) const {
  // Hazards belong to a match in progress. Seating one during `lobby` or `countdown` would put a
  // body in the arena before anyone can steer away from it, and one during `ended` would keep the
  // world changing after the outcome was decided.
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }

  const std::uint64_t tick = context.tick_sequence().value();
  const simulation::ArenaBounds& bounds = context.map().bounds();
  const double seconds_per_tick = context.fixed_delta().seconds();

  // Declaration order, which is the configuration file's order. It is the tie-break when two kinds
  // are due on the same tick and the budget seats only one, so it must be a written order rather
  // than a container's iteration accident -- which is why `GameModeConfiguration::hazards` is a
  // vector and says so.
  for (const HazardArchetype& archetype : archetypes_) {
    if (tick % archetype.spawn_interval_ticks() != 0) {
      continue;
    }
    // Both budget checks precede every draw. If either stops this kind, the generator has not been
    // advanced, so a tick that seats nothing leaves `draw_count` exactly where it was and two runs
    // that skipped identically stay bit-identical.
    if (world.entity_id_reservation().empty()) {
      return;
    }
    if (world.store<simulation::PhysicsBody>().size() >= simulation::kMaximumEntityCount) {
      return;
    }

    static_cast<void>(create_crossing_hazard(world, bounds, archetype, seconds_per_tick));
  }
}

} // namespace blob_royale::gameplay
