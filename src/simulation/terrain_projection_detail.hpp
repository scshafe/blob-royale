#ifndef BLOB_ROYALE_SIMULATION_TERRAIN_PROJECTION_DETAIL_HPP
#define BLOB_ROYALE_SIMULATION_TERRAIN_PROJECTION_DETAIL_HPP

#include "terrain_definition.hpp"

namespace blob_royale::simulation::detail {

// Internal arithmetic seam, not another public geometry capability. Raw projected coordinates
// have not passed Vector2 validation and can round beyond its bound on standalone signed extreme
// corridors. Keeping distance independent of point materialization preserves its existing domain.
// Related: terrain_queries.hpp; terrain_projection_promotion_tests.cpp retains the independent
// pre-delegation proof, including inputs the typed point result cannot represent.
struct RawCorridorProjection final {
  double x;
  double y;
  double distance;
};

[[nodiscard]] RawCorridorProjection project_corridor_raw(const TerrainCorridor& corridor,
                                                         const Vector2& point) noexcept;

} // namespace blob_royale::simulation::detail

#endif
