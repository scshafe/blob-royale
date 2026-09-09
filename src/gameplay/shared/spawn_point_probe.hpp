#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_SPAWN_POINT_PROBE_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_SPAWN_POINT_PROBE_HPP

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::gameplay {

// canonical: spawn_point_probe -- the forward probe every rotation-ordered spawn policy shares.
//
//     n = spawn_point_is_free.size()
//     for probe in [0, n):
//       k = (rotation_counter + probe) mod n
//       if spawn_point_is_free[k]: return k
//     return nullopt
//
// Probing forward from the world-owned rotation counter the `SpawnSystem` offers is what spreads
// consecutive joiners around the map instead of stacking them on the first free point, and the
// counter being world state is what makes the choice a deterministic function of the committed
// world. A full ring yields nullopt, which defers the entity one tick; an empty ring yields nullopt
// without dividing by zero, because the loop never runs.
//
// The loop was written twice, in royale's ring policy and sandbox's open-field policy, before a
// third policy needed it; a policy now owns only the phase rule it wraps around this
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Shared rules promoted, because
// they now have a second customer").
// related: next_free_spawn_point_policy.hpp -- the open-field wrapper.
// related: ../royale/rotating_ring_spawn_policy.hpp -- the between-matches wrapper.
[[nodiscard]] inline std::optional<std::size_t>
next_free_spawn_point(const std::size_t rotation_counter,
                      const std::span<const bool> spawn_point_is_free) noexcept {
  const std::size_t point_count = spawn_point_is_free.size();
  for (std::size_t probe = 0; probe < point_count; ++probe) {
    const std::size_t index = (rotation_counter + probe) % point_count;
    if (spawn_point_is_free[index]) {
      return index;
    }
  }
  return std::nullopt;
}

} // namespace blob_royale::gameplay

#endif
