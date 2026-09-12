#ifndef BLOB_ROYALE_TESTING_CROSSING_HAZARD_CREATION_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTING_CROSSING_HAZARD_CREATION_FROZEN_REFERENCE_HPP

#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "random_stream_registry.hpp"
#include "shared/hazard_archetype.hpp"
#include "shared/hazard_crossing.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"
#include "vector2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace blob_royale::testing::crossing_hazard_creation_reference {

inline constexpr std::array<std::uint64_t, 3> kSeeds{0, 2026,
                                                     std::numeric_limits<std::uint64_t>::max()};
inline constexpr std::uint64_t kFirstEntity = 100;
inline constexpr std::uint64_t kReservationSize = 8;
inline constexpr std::uint64_t kPriorHazardDrawCount = 5;
inline constexpr std::uint64_t kPriorHillDrawCount = 7;
inline constexpr std::size_t kSequenceRepetitions = 2;
inline constexpr std::uint64_t kCommonDueTick = 140;
inline constexpr std::uint64_t kNotDueTick = 1;
inline constexpr std::uint64_t kCreatedHazardDrawCount = 3;
inline constexpr std::uint64_t kFirstCapacityFixtureEntity = 1'000;

[[nodiscard]] inline simulation::PhysicsBody capacity_fixture_body() {
  return simulation::PhysicsBody::create_static(simulation::Vector2::create(50.0, 50.0));
}

[[nodiscard]] inline std::vector<gameplay::HazardArchetype> archetypes() {
  return {
      gameplay::HazardArchetype::create({"plaid_meteorite", 26.0, 40.0, 0.2, 200.0, 0.05, true}),
      gameplay::HazardArchetype::create({"velvet_boulder", 0.25, 2.0, 0.0, 27.5, 0.07, false}),
      gameplay::HazardArchetype::create({"elastic_pebble", 4.5, 0.5, 1.0, 350.0, 0.05, true})};
}

// Frozen from dbc99ad:src/gameplay/shared/hazard_spawn_system.cpp, the complete inner creation
// operation before promotion. Its unchanged crossing/lifetime dependencies remain shared; only
// the operation under promotion is copied. The independent hazard_stream_frozen_reference adds
// a separate old RNG/geometry/lifetime oracle for the accepted 2026 fixture.
// Never delegate this reference to create_crossing_hazard or the production spawner.
[[nodiscard]] inline simulation::EntityId create_hazard(simulation::GameWorld& world,
                                                        const simulation::ArenaBounds& bounds,
                                                        const gameplay::HazardArchetype& archetype,
                                                        const double seconds_per_tick) {
  const gameplay::HazardCrossing crossing =
      gameplay::draw_hazard_crossing(world.random(simulation::RandomStreamKind::kHazards), bounds,
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
      entity, simulation::Lifetime{gameplay::hazard_lifetime_ticks(
                  crossing.travel_distance, archetype.speed(), seconds_per_tick)});
  if (archetype.lethal_on_contact()) {
    world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
        entity, simulation::LethalOnContact{});
  }
  return entity;
}

// Independently retained old schedule and checks: the production reader must still match this
// before cutover, and the later delegation must preserve which declarations consume draws/ids.
inline void spawn_due(simulation::GameWorld& world, const simulation::TickContext& context,
                      const std::span<const gameplay::HazardArchetype> archetypes) {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }
  for (const auto& archetype : archetypes) {
    if (context.tick_sequence().value() % archetype.spawn_interval_ticks() != 0) {
      continue;
    }
    if (world.entity_id_reservation().empty()) {
      return;
    }
    if (world.store<simulation::PhysicsBody>().size() >= simulation::kMaximumEntityCount) {
      return;
    }
    static_cast<void>(
        create_hazard(world, context.map().bounds(), archetype, context.fixed_delta().seconds()));
  }
}

} // namespace blob_royale::testing::crossing_hazard_creation_reference

#endif
