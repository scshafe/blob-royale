#include "shared/hazard_crossing.hpp"

#include "deterministic_random.hpp"
#include "map_definition.hpp"
#include "shared/hazard_archetype.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <cmath>
#include <cstdint>

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
  switch (edge % kHazardEntryEdgeCount) {
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
  switch (edge % kHazardEntryEdgeCount) {
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

// **The one implementation of "how far does a hazard travel".** Both the drawn crossing and the
// worst case route through it, differing only in the length they are handed: the distance across
// the arena, plus the clearance the body starts outside on the entry side, plus the same again so
// it is fully clear on the exit side.
[[nodiscard]] double travel_distance_of(const double crossing_length,
                                        const double radius) noexcept {
  return crossing_length + (2.0 * (kHazardEntryClearanceRadii * radius));
}

} // namespace

HazardCrossing draw_hazard_crossing(simulation::DeterministicRandom& random,
                                    const simulation::ArenaBounds& bounds, const double radius,
                                    const double speed) {
  const std::uint64_t edge = random.next_below(kHazardEntryEdgeCount);
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
  const double clearance = kHazardEntryClearanceRadii * radius;
  return HazardCrossing{simulation::Vector2::create(entry.x() + (normal.x() * clearance),
                                                    entry.y() + (normal.y() * clearance)),
                        simulation::Vector2::create(direction.x() * speed, direction.y() * speed),
                        travel_distance_of(length, radius)};
}

double longest_hazard_travel_distance(const simulation::ArenaBounds& bounds,
                                      const double radius) noexcept {
  const double width = bounds.width();
  const double height = bounds.height();
  // Deliberately the same expression `draw_hazard_crossing` computes its own length with, at the
  // extents no drawn crossing can exceed. See the header for why this is not `std::hypot`.
  return travel_distance_of(std::sqrt((width * width) + (height * height)), radius);
}

std::uint64_t hazard_lifetime_ticks(const double travel_distance, const double speed,
                                    const double seconds_per_tick) {
  const double ticks = std::ceil(travel_distance / speed / seconds_per_tick);
  if (!(ticks >= 1.0)) {
    return 1;
  }
  if (ticks >= static_cast<double>(simulation::kMaximumProtocolSafeInteger)) {
    return simulation::kMaximumProtocolSafeInteger;
  }
  return static_cast<std::uint64_t>(ticks);
}

std::uint64_t maximum_standing_hazard_count(const HazardArchetype& archetype,
                                            const simulation::ArenaBounds& bounds,
                                            const double seconds_per_tick) {
  const std::uint64_t lifetime_ticks =
      hazard_lifetime_ticks(longest_hazard_travel_distance(bounds, archetype.radius()),
                            archetype.speed(), seconds_per_tick);
  // At least one by `HazardArchetype::create`, which rejects an interval that rounds to zero
  // ticks, so this never divides by zero.
  const std::uint64_t interval_ticks = archetype.spawn_interval_ticks();
  // Integer ceiling written as quotient-plus-remainder rather than `(a + b - 1) / b`, because the
  // lifetime is clamped only to the protocol-safe integer range and the addition form could
  // overflow for an interval near the top of it. Then one more for the hazard seated this tick.
  return (lifetime_ticks / interval_ticks) + (lifetime_ticks % interval_ticks == 0 ? 0 : 1) + 1;
}

} // namespace blob_royale::gameplay
