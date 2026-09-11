#ifndef BLOB_ROYALE_SIMULATION_TERRAIN_BOUNDARY_HPP
#define BLOB_ROYALE_SIMULATION_TERRAIN_BOUNDARY_HPP

#include "terrain_queries.hpp"

#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace blob_royale::simulation::detail {

// An open supported angular sector at one arrangement vertex, in counterclockwise order.
// Tangent rays are excluded: their second-order curved behavior is not an open face.
struct TerrainSupportedSector final {
  Vector2 begin_direction;
  Vector2 end_direction;
  Vector2 interior_direction;
};

struct TerrainLineSpan final {
  Vector2 begin;
  Vector2 end;
};

// Counterclockwise arc, split at cardinal directions and intersections (at most one quadrant).
struct TerrainArcSpan final {
  Vector2 center;
  double radius;
  Vector2 begin;
  Vector2 end;
  Vector2 begin_direction;
  Vector2 end_direction;
};

struct TerrainBoundarySpan final {
  BoundaryFeatureId feature;
  std::variant<TerrainLineSpan, TerrainArcSpan> geometry;
  // +1: the raw curve's left side is supported; -1: its right side; 0: rim-only.
  // This provenance directs representable witness rounding, never event-time comparisons.
  int supported_side;
  std::optional<TerrainSupportedSector> begin_sector;
  std::optional<TerrainSupportedSector> end_sector;
};

struct TerrainBoundaryPoint final {
  BoundaryFeatureId feature;
  Vector2 point;
};

// Derived once at immutable terrain construction. It is not another authoring or publication
// owner: TerrainStorage owns this cache beside the only authored geometry, without back-pointers.
struct TerrainBoundary final {
  std::vector<TerrainBoundarySpan> spans;
  std::vector<TerrainBoundaryPoint> isolated_points;
};

struct TerrainQueryAccess final {
  [[nodiscard]] static const TerrainBoundary& boundary(const TerrainDefinition& terrain);
};

[[nodiscard]] TerrainBoundary compile_terrain_boundary(const ArenaBounds& bounds,
                                                       TerrainGround ground,
                                                       std::span<const TerrainCorridor> corridors,
                                                       std::span<const TerrainHole> holes);

} // namespace blob_royale::simulation::detail

#endif
