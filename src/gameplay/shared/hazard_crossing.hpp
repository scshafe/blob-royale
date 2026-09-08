#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_CROSSING_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_CROSSING_HPP

#include "shared/hazard_archetype.hpp"

#include "map_definition.hpp"
#include "vector2.hpp"

#include <cstdint>

namespace blob_royale::simulation {
class DeterministicRandom;
} // namespace blob_royale::simulation

namespace blob_royale::gameplay {

// canonical: hazard_crossing -- one hazard's trip across the arena, and the **only** implementation
// of how far one can be and how long one lives.
//
// **It exists because two callers need the same arithmetic from opposite ends.**
// `hazard_spawn_system` draws a crossing and asks how long *this* one lives;
// `application/match_startup_validation.cpp` asks how long the *longest* one could live, so it can
// refuse at startup a hazard table whose standing population would exceed the protocol snapshot
// bound. A second copy of the formula in the validator would be a bound that agrees with the
// spawner only until someone edits one of them, and a bound that silently stops bounding is worse
// than no bound at all: the failure it exists to prevent is a match that degrades under load rather
// than a configuration that fails closed. This tree has already paid for duplicated logic twice --
// the published kind-name grammar and the system-created entity headroom each existed in three or
// four places before being unified -- so the arithmetic is written once, here, and both callers
// route through it.
//
// **The clearance and the edge count live here rather than on the spawn system**, because they are
// terms of the distance formula, not scheduling parameters: `travel_distance` reads the clearance
// and the worst-case length reads the edge geometry, and a constant that two functions in this file
// need has no business hanging off a class in another one.
// related: shared/hazard_spawn_system.hpp -- the caller that draws one crossing per hazard.
// related: shared/hazard_archetype.hpp -- the configured kind a crossing is drawn for.
// related: ../../application/match_startup_validation.hpp -- the caller that bounds the population.

// The four arena edges a hazard may enter through, in the order the entry draw indexes them.
// Closed and ordered, because the draw is `next_below(kHazardEntryEdgeCount)` and the mapping from
// a drawn integer to a geometry must be one written thing rather than an arithmetic accident.
inline constexpr std::uint64_t kHazardEntryEdgeCount = 4;

// How far outside the arena a hazard's centre starts, as a multiple of its own radius. Two radii
// puts the whole disc clear of the edge it enters through, so the body is unambiguously outside on
// the tick it is seated and enters under its own velocity rather than starting in contact with a
// player standing at the wall.
inline constexpr double kHazardEntryClearanceRadii = 2.0;

// The complete geometry of one crossing, drawn from the seeded generator.
struct HazardCrossing final {
  simulation::Vector2 position;
  simulation::Vector2 velocity;
  double travel_distance{};
};

// Draws one crossing: where the body starts, how fast and which way it moves, and how far it must
// go to be fully clear of the arena again.
//
// **Three draws, in this order, and no other source.** The entry edge, the point along it, and the
// point on the opposite edge that fixes the direction. Two points rather than an angle because an
// angle would need a range that guaranteed the body actually entered, and the range depends on
// where on the edge it started; aiming at the opposite edge makes crossing structural instead of a
// constraint to enforce afterwards. `GameWorld::random()` is the only randomness source inside a
// tick, so a replay of `(map, mode configuration, seed, command log)` reproduces every crossing
// exactly (`docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for
// framework code").
[[nodiscard]] HazardCrossing draw_hazard_crossing(simulation::DeterministicRandom& random,
                                                  const simulation::ArenaBounds& bounds,
                                                  double radius, double speed);

// The largest `HazardCrossing::travel_distance` this arena can produce for a body of this radius.
//
// A crossing runs from a point on one edge to a point on the opposite one, so its length is
// bounded by the arena's diagonal and nothing longer is reachable: a top-to-bottom crossing spans
// the full height and at most the full width, and a side-to-side one spans the full width and at
// most the full height. Both are `sqrt(w^2 + h^2)` at the limit, and the limit is not attained
// because the point drawn along an edge lies in `[0, 1)`. The clearance at each end is then added
// by the same expression `draw_hazard_crossing` uses, which is what makes this a bound on that
// function rather than a second opinion about it.
//
// The diagonal is spelled `sqrt(w*w + h*h)` rather than `std::hypot(w, h)` on purpose. `hypot` is
// the more accurate of the two and would therefore be free to return a *smaller* value than the
// drawn length computed the other way, which would turn a bound into an off-by-one-ulp defect.
// Written identically, monotone rounding does the rest: a drawn separation below an arena extent
// cannot square to more than that extent squared.
[[nodiscard]] double longest_hazard_travel_distance(const simulation::ArenaBounds& bounds,
                                                    double radius) noexcept;

// How long a body covering `travel_distance` at `speed` lives, in whole ticks, rounded up so a
// hazard is never removed before it has finished leaving. Derived from a distance and the
// archetype's own speed; no constant here is a tuned number.
//
// **`distance / speed` is a duration only for a body that keeps its speed**, which is why
// `HazardSpawnSystem` seats every hazard at `PhysicsBody::kMinimumDragScale`: phase 1's drag factor
// is geometric, so a dragged body would cover `speed / drag_per_second` world units in total and
// this count would be a lifetime for a crossing it never completes. The two are one decision, not
// two that happen to agree.
//
// At least one tick, so a hazard always exists for the tick it was seated on; and clamped to the
// protocol-safe integer range, because `Lifetime` publishes the count and an archetype with an
// absurdly low speed could otherwise produce one no frame can encode.
[[nodiscard]] std::uint64_t hazard_lifetime_ticks(double travel_distance, double speed,
                                                  double seconds_per_tick);

// The most bodies of one kind that can stand in the world at the same time.
//
// A hazard of this kind is seated every `spawn_interval_ticks` and lives at most
// `hazard_lifetime_ticks(longest_hazard_travel_distance(...), ...)` ticks, so at most
// `ceil(lifetime / interval)` earlier hazards are still standing when the next one is seated, plus
// that one itself. It is an upper bound rather than the exact count -- the drawn crossing is
// usually shorter than the diagonal and the spawner skips a kind that loses the one-id race -- and
// an upper bound is what a startup rejection needs, because a bound that is sometimes optimistic
// is a bound that sometimes fails to reject.
//
// **It lives beside the lifetime it is computed from** rather than in the validator that sums it,
// so that "how long does a hazard live" and "how many of them are there" cannot drift apart across
// a library boundary. The validator's job is the summation and the diagnostic, which is the part
// that belongs to the application.
[[nodiscard]] std::uint64_t maximum_standing_hazard_count(const HazardArchetype& archetype,
                                                          const simulation::ArenaBounds& bounds,
                                                          double seconds_per_tick);

} // namespace blob_royale::gameplay

#endif
