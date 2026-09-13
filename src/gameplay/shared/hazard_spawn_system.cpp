#include "shared/hazard_spawn_system.hpp"

#include "component_store.hpp"
#include "components/crossing_hazard_component.hpp"
#include "deterministic_random.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "random_stream_registry.hpp"
#include "shared/create_crossing_hazard.hpp"
#include "shared/hazard_crossing.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"

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

  double lethal_weight = 0.0;
  double nonlethal_weight = 0.0;
  for (const auto& archetype : archetypes_) {
    const double weight = 1.0 / static_cast<double>(archetype.spawn_interval_ticks());
    (archetype.lethal_on_contact() ? lethal_weight : nonlethal_weight) += weight;
  }
  const auto& tuning = world.match().movement.current;
  const double lethal_rate = lethal_weight == 0.0 ? 0.0 : tuning.lethal_spawn_rate_per_second();
  const double nonlethal_rate =
      nonlethal_weight == 0.0 ? 0.0 : tuning.nonlethal_spawn_rate_per_second();
  if (lethal_rate + nonlethal_rate == 0.0 || world.entity_id_reservation().empty() ||
      world.store<simulation::CrossingHazard>().size() >= kMaximumActiveCrossingHazardCount ||
      world.store<simulation::PhysicsBody>().size() >= simulation::kMaximumMotionBodyCount ||
      world.entities().size() >= simulation::kMaximumEntityCount) {
    return;
  }

  auto& random = world.random(simulation::RandomStreamKind::kHazards);
  const double seconds_per_tick = context.fixed_delta().seconds();
  const double class_sample = random.next_unit_interval();
  const double lethal_probability = lethal_rate * seconds_per_tick;
  if (class_sample >= (lethal_rate + nonlethal_rate) * seconds_per_tick) {
    return;
  }
  const bool lethal = class_sample < lethal_probability;
  const double total_weight = lethal ? lethal_weight : nonlethal_weight;
  const double kind_sample = random.next_unit_interval() * total_weight;
  double cumulative_weight = 0.0;
  const HazardArchetype* selected = nullptr;
  for (const auto& archetype : archetypes_) {
    if (archetype.lethal_on_contact() != lethal) {
      continue;
    }
    selected = &archetype;
    cumulative_weight += 1.0 / static_cast<double>(archetype.spawn_interval_ticks());
    if (kind_sample < cumulative_weight) {
      break;
    }
  }
  // A selected class has positive weight. The last matching kind also covers binary64 rounding
  // of sample*total onto the upper endpoint; this keeps every admitted draw assigned exactly once.
  static_cast<void>(
      create_crossing_hazard(world, context.map().bounds(), *selected, seconds_per_tick));
}

} // namespace blob_royale::gameplay
