#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_ROAMING_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_ROAMING_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"

#include "arena_bounds.hpp"
#include "deterministic_random.hpp"
#include "fixed_delta.hpp"
#include "vector2.hpp"

#include <cstdint>

namespace blob_royale::gameplay {

struct HillRoamSelection final {
  simulation::Vector2 velocity;
  std::uint64_t retarget_after_ticks;

  friend bool operator==(const HillRoamSelection&, const HillRoamSelection&) = default;
};

// canonical: hill_roam_sampling -- one positive-speed selection from the world's named hill stream.
// The validated configuration bounds the selected scalar speed and inclusive retarget interval.
// The velocity is its rounded binary64 realization, not an exact-norm certificate at either bound.
// Draw order, including equal ranges: unit t, quadrant below four, unit speed, interval below the
// inclusive tick span. Canonical interval rejection may consume additional recorded draws.
// Direction uses a rational quarter-circle and one written square-root normalization; angles are
// deliberately nonuniform. No trigonometry, standard distribution, heading retry, or zero fallback
// is used.
// Physical arithmetic failures propagate SimulationValidationError. The caller owns the stream,
// next-retarget deadline validation, and transaction rollback; this function owns none of them.
[[nodiscard]] HillRoamSelection sample_hill_roam(simulation::DeterministicRandom& random,
                                                 const KingOfTheHillConfiguration& configuration);

struct HillRoamMotion final {
  simulation::Vector2 center;
  simulation::Vector2 velocity;

  friend bool operator==(const HillRoamMotion&, const HillRoamMotion&) = default;
};

// canonical: hill_roam_boundary_motion -- cancel outward motion at the map's center rectangle.
// The starting center must lie in the exact closed ArenaBounds, else GameplayValidationError
// GAMEPLAY.KING_OF_THE_HILL_SCALAR_OUT_OF_RANGE is thrown. The canonical fixed-step multiplication
// and position integration propose the endpoint; their physical arithmetic failures propagate.
// Each strictly overshooting axis retains its START coordinate and gets zero velocity. An exact
// boundary arrival commits the endpoint and gets zero outward velocity. Other axes remain intact.
// There is no radius inset, terrain query, bounce, partial advance to the edge, speed restoration,
// or random draw. Boundary cancellation can remain stationary across multiple scheduled retargets.
[[nodiscard]] HillRoamMotion advance_hill_roam(const simulation::Vector2& center,
                                               const simulation::Vector2& velocity,
                                               const simulation::ArenaBounds& bounds,
                                               simulation::FixedDelta fixed_delta);

} // namespace blob_royale::gameplay

#endif
