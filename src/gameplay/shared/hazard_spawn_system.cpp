#include "shared/hazard_spawn_system.hpp"

#include "component_store.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/hazard_crossing.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"
#include "vector2.hpp"

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

    const HazardCrossing crossing =
        draw_hazard_crossing(world.random(), bounds, archetype.radius(), archetype.speed());

    // `is_static` is false because the general impulse divides by both masses and
    // `resolve_general_pair_collision` requires two dynamic bodies; a static hazard would be a body
    // no contact row could resolve. The bounds behaviour is `kCross`, without which phase 4 would
    // fold the body back off the wall it just entered through and it would rattle around the arena
    // forever instead of leaving.
    const simulation::PhysicsBody body =
        simulation::PhysicsBody::create(
            crossing.position, crossing.velocity, simulation::Vector2::create(0.0, 0.0),
            archetype.radius(), archetype.mass(), simulation::PhysicsBody::kDefaultCollisionLayer,
            simulation::PhysicsBody::kDefaultCollisionMask, false)
            .with_restitution(archetype.restitution())
            .with_bounds_behavior(simulation::BoundsBehavior::kCross);

    const simulation::EntityId entity = world.create_entity();
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity, body);
    // The one call that decides how long this body lives, and the same function
    // `match_startup_validation` calls at the worst-case distance to bound the standing population
    // at startup. Neither may hold its own copy of the arithmetic; see
    // `shared/hazard_crossing.hpp`.
    world.mutable_store<simulation::Lifetime>().insert_or_assign(
        entity, simulation::Lifetime{hazard_lifetime_ticks(crossing.travel_distance,
                                                           archetype.speed(), seconds_per_tick)});
    // The marker is attached only when the archetype declares it, so a heavy-but-harmless boulder
    // and a lethal comet differ by exactly one component and one configuration key.
    if (archetype.lethal_on_contact()) {
      world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
          entity, simulation::LethalOnContact{});
    }
  }
}

} // namespace blob_royale::gameplay
