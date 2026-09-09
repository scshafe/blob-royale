#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_DISC_GEOMETRY_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_DISC_GEOMETRY_HPP

#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <cmath>

namespace blob_royale::gameplay {

// canonical: disc_geometry -- whether a centre is outside a circle, written once.
//
// `sqrt(dx * dx + dy * dy) > radius + kPositionTolerance`, exactly as
// `docs/architecture/0005-royale-mode.md` § "Elimination and placement" states it for the zone: a
// centre exactly on the rim is inside, consistent with the baseline's inclusive contact rule, and
// the square root of a written-out sum is used rather than `std::hypot`, which computes a
// different binary64 value for the same inputs. The expression is `zone_elimination`'s, moved here
// byte for byte the day a hill and a race gate needed the same question answered the same way
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Shared rules promoted, because
// they now have a second customer"). Every royale fixture's elimination tick is where it was.
// related: ../royale/zone_elimination_system.cpp -- the first caller.
[[nodiscard]] inline bool center_is_outside(const simulation::Vector2& position,
                                            const simulation::Vector2& center,
                                            const double radius) noexcept {
  const double offset_x = position.x() - center.x();
  const double offset_y = position.y() - center.y();
  const double distance = std::sqrt((offset_x * offset_x) + (offset_y * offset_y));
  return distance > radius + simulation::kPositionTolerance;
}

} // namespace blob_royale::gameplay

#endif
