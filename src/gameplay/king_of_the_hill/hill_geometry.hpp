#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_GEOMETRY_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_GEOMETRY_HPP

#include "map_definition.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace blob_royale::gameplay {

// canonical: hill_geometry -- where the hill is, as a pure function of the map and one integer.
//
// The hill tours the map's `hill` markers in declared order: it dwells at each for `D` ticks,
// glides to the next over `T` ticks, and cycles. With `S = D + T` (at least one, which the
// configuration guarantees), `n` markers `m_0 .. m_{n-1}`, and `e` elapsed running ticks:
//
//     if n == 1:            center(e) = m_0
//     k = (e / S) mod n     integer division
//     t = e mod S
//     if t < D:             center(e) = m_k
//     else:                 f = (t - D) / T      T > 0 here, because t >= D and t < S
//                           center(e) = m_k + (m_{(k+1) mod n} - m_k) * f, per component
//
// **The division precedes the multiplication and that order is the contract**, as it is for the
// zone's shrink curve, so the hill carries no accumulated floating-point state: any committed
// snapshot determines every later position, and a replay from an arbitrary snapshot reproduces the
// tour exactly (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "The hill"). A
// travel of zero is a hop, and one marker never moves.
// related: hill_movement_system.hpp -- the system that evaluates this each tick.

// The marker kind the hill tours. Every other kind is some other mode's vocabulary.
inline constexpr std::string_view kHillMarkerKind = "hill";

// The positions of the map's `hill` markers, in declared order. Empty for a map with none, which
// `KingOfTheHillMode::validate_map` refuses at construction.
[[nodiscard]] std::vector<simulation::Vector2> hill_markers(const simulation::MapDefinition& map);

// The centre after `elapsed_running_ticks` committed ticks of `running`. `markers` must be
// non-empty and `dwell_ticks + travel_ticks` at least one; both are validated before any tick.
[[nodiscard]] simulation::Vector2 hill_center(std::span<const simulation::Vector2> markers,
                                              std::uint64_t dwell_ticks, std::uint64_t travel_ticks,
                                              std::uint64_t elapsed_running_ticks);

} // namespace blob_royale::gameplay

#endif
