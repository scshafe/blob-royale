#include "shared/create_crossing_hazard.hpp"

#include "component_store.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "contact_effect_admission.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "random_stream_registry.hpp"
#include "shared/hazard_archetype.hpp"
#include "shared/hazard_crossing.hpp"
#include "vector2.hpp"

namespace blob_royale::gameplay {

simulation::EntityId
create_crossing_hazard(simulation::GameWorld& world, const simulation::ArenaBounds& bounds,
                       const HazardArchetype& archetype, const double seconds_per_tick,
                       const std::optional<simulation::ContactEffectPolicy> instance_override) {
  if (instance_override) {
    simulation::validate_contact_effect_policy(*instance_override);
  }
  const HazardCrossing crossing =
      draw_hazard_crossing(world.random(simulation::RandomStreamKind::kHazards), bounds,
                           archetype.radius(), archetype.speed());
  const simulation::PhysicsBody body =
      simulation::PhysicsBody::create(
          crossing.position, crossing.velocity, simulation::Vector2::create(0.0, 0.0),
          archetype.radius(), archetype.mass(), simulation::PhysicsBody::kDefaultCollisionLayer,
          simulation::PhysicsBody::kDefaultCollisionMask, false)
          .with_restitution(archetype.restitution())
          .with_drag_scale(simulation::PhysicsBody::kMinimumDragScale)
          .with_bounds_behavior(simulation::BoundsBehavior::kCross);

  const simulation::EntityId entity = world.create_entity();
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity, body);
  world.mutable_store<simulation::Lifetime>().insert_or_assign(
      entity, simulation::Lifetime{hazard_lifetime_ticks(crossing.travel_distance,
                                                         archetype.speed(), seconds_per_tick)});
  if (archetype.lethal_on_contact()) {
    world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
        entity, simulation::LethalOnContact{});
  }
  simulation::assign_contact_effect_policy(world, entity, instance_override,
                                           archetype.contact_effect_policy());
  return entity;
}

} // namespace blob_royale::gameplay
