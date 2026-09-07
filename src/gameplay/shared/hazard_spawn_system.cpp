#include "shared/hazard_spawn_system.hpp"

#include "component_store.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "deterministic_random.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"
#include "vector2.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// One point on one arena edge, parameterized by `along` in [0, 1).
//
// The edges are numbered 0 top, 1 right, 2 bottom, 3 left, so `(edge + 2) % 4` is the opposite
// edge and a hazard aimed from one at the other always crosses the arena. The arena is anchored at
// the origin and spans `[0, width] x [0, height]` (`simulation/map_definition.hpp`), so these are
// the four segments of that rectangle and nothing here re-derives the anchoring.
[[nodiscard]] simulation::Vector2 point_on_edge(const std::uint64_t edge, const double along,
                                                const simulation::ArenaBounds& bounds) {
  switch (edge % HazardSpawnSystem::kEntryEdgeCount) {
  case 0:
    return simulation::Vector2::create(along * bounds.width(), 0.0);
  case 1:
    return simulation::Vector2::create(bounds.width(), along * bounds.height());
  case 2:
    return simulation::Vector2::create(along * bounds.width(), bounds.height());
  default:
    return simulation::Vector2::create(0.0, along * bounds.height());
  }
}

// The outward unit normal of one edge: the direction that leads out of the arena. Used only to
// place the body clear of the edge it enters through.
[[nodiscard]] simulation::Vector2 outward_normal(const std::uint64_t edge) {
  switch (edge % HazardSpawnSystem::kEntryEdgeCount) {
  case 0:
    return simulation::Vector2::create(0.0, -1.0);
  case 1:
    return simulation::Vector2::create(1.0, 0.0);
  case 2:
    return simulation::Vector2::create(0.0, 1.0);
  default:
    return simulation::Vector2::create(-1.0, 0.0);
  }
}

// The complete geometry of one crossing, drawn from the seeded generator.
//
// **Three draws, in this order, and no other source.** The edge, the point along it, and the point
// on the opposite edge that fixes the direction. Two points rather than an angle because an angle
// would need a range that guaranteed the body actually entered, and the range depends on where on
// the edge it started; aiming at the opposite edge makes crossing structural instead of a
// constraint to enforce afterwards.
struct Crossing final {
  simulation::Vector2 position;
  simulation::Vector2 velocity;
  double travel_distance{};
};

[[nodiscard]] Crossing draw_crossing(simulation::DeterministicRandom& random,
                                     const simulation::ArenaBounds& bounds, const double radius,
                                     const double speed) {
  const std::uint64_t edge = random.next_below(HazardSpawnSystem::kEntryEdgeCount);
  const double entry_along = random.next_unit_interval();
  const double exit_along = random.next_unit_interval();

  const simulation::Vector2 entry = point_on_edge(edge, entry_along, bounds);
  const simulation::Vector2 exit =
      point_on_edge(edge + 2, exit_along, bounds); // `point_on_edge` takes the modulus.

  const double delta_x = exit.x() - entry.x();
  const double delta_y = exit.y() - entry.y();
  // Strictly positive: opposite edges are separated by the arena's width or height, and
  // `ArenaBounds::create` rejects a non-positive extent, so this never divides by zero.
  const double length = std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
  const simulation::Vector2 direction =
      simulation::Vector2::create(delta_x / length, delta_y / length);

  const simulation::Vector2 normal = outward_normal(edge);
  const double clearance = HazardSpawnSystem::kEntryClearanceRadii * radius;
  return Crossing{
      simulation::Vector2::create(entry.x() + (normal.x() * clearance),
                                  entry.y() + (normal.y() * clearance)),
      simulation::Vector2::create(direction.x() * speed, direction.y() * speed),
      // The distance the body must cover to leave: across the arena, plus the clearance it starts
      // outside on the entry side, plus the same again so it is fully clear on the exit side.
      length + (2.0 * clearance)};
}

// How long this crossing takes, in whole ticks, rounded up so a hazard is never removed before it
// has finished leaving. Derived from the arena and the archetype's own speed; no constant here is a
// tuned number.
[[nodiscard]] std::uint64_t crossing_lifetime_ticks(const double travel_distance,
                                                    const double speed,
                                                    const double seconds_per_tick) {
  const double ticks = std::ceil(travel_distance / speed / seconds_per_tick);
  // At least one tick, so a hazard always exists for the tick it was seated on; and clamped to the
  // protocol-safe integer range, because `Lifetime` publishes the count and an archetype with an
  // absurdly low speed could otherwise produce one no frame can encode.
  if (!(ticks >= 1.0)) {
    return 1;
  }
  if (ticks >= static_cast<double>(simulation::kMaximumProtocolSafeInteger)) {
    return simulation::kMaximumProtocolSafeInteger;
  }
  return static_cast<std::uint64_t>(ticks);
}

} // namespace

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

    const Crossing crossing =
        draw_crossing(world.random(), bounds, archetype.radius(), archetype.speed());

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
    world.mutable_store<simulation::Lifetime>().insert_or_assign(
        entity, simulation::Lifetime{crossing_lifetime_ticks(crossing.travel_distance,
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
