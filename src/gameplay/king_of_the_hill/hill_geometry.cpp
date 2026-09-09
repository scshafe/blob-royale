#include "king_of_the_hill/hill_geometry.hpp"

#include "map_definition.hpp"
#include "vector2.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace blob_royale::gameplay {

std::vector<simulation::Vector2> hill_markers(const simulation::MapDefinition& map) {
  std::vector<simulation::Vector2> markers;
  for (const simulation::MapDefinition::Marker& marker : map.markers()) {
    if (marker.kind == kHillMarkerKind) {
      markers.push_back(marker.position);
    }
  }
  return markers;
}

simulation::Vector2 hill_center(const std::span<const simulation::Vector2> markers,
                                const std::uint64_t dwell_ticks, const std::uint64_t travel_ticks,
                                const std::uint64_t elapsed_running_ticks) {
  const std::size_t marker_count = markers.size();
  if (marker_count == 1) {
    return markers[0];
  }
  const std::uint64_t stop_ticks = dwell_ticks + travel_ticks;
  const std::size_t stop = static_cast<std::size_t>((elapsed_running_ticks / stop_ticks) %
                                                    static_cast<std::uint64_t>(marker_count));
  const std::uint64_t ticks_into_stop = elapsed_running_ticks % stop_ticks;
  const simulation::Vector2& from = markers[stop];
  if (ticks_into_stop < dwell_ticks) {
    return from;
  }
  const simulation::Vector2& to = markers[(stop + 1) % marker_count];
  // The division precedes the multiplication, per component, and that order is the contract.
  const double fraction =
      static_cast<double>(ticks_into_stop - dwell_ticks) / static_cast<double>(travel_ticks);
  return simulation::Vector2::create(from.x() + ((to.x() - from.x()) * fraction),
                                     from.y() + ((to.y() - from.y()) * fraction));
}

} // namespace blob_royale::gameplay
