#include "terrain_definition.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"
#include "terrain_boundary.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::simulation {
namespace detail {

// The immutable allocation contains every authored field and its one derived query cache. Neither
// MapDefinition nor a query carries another geometry copy or a cache with independent lifetime.
struct TerrainStorage final {
  TerrainStorage(const ArenaBounds authored_bounds, const TerrainGround authored_ground,
                 std::vector<TerrainCorridor> authored_corridors,
                 std::vector<TerrainHole> authored_holes)
      : bounds(authored_bounds), ground(authored_ground), corridors(std::move(authored_corridors)),
        holes(std::move(authored_holes)),
        boundary(compile_terrain_boundary(bounds, ground, corridors, holes)) {}

  ArenaBounds bounds;
  TerrainGround ground;
  std::vector<TerrainCorridor> corridors;
  std::vector<TerrainHole> holes;
  TerrainBoundary boundary;
};

const TerrainBoundary& TerrainQueryAccess::boundary(const TerrainDefinition& terrain) {
  return terrain.storage_->boundary;
}

} // namespace detail
namespace {

void require_terrain_name(const std::string_view name, const std::string_view context) {
  if (!is_wire_kind_name(name)) {
    throw SimulationValidationError{
        SimulationValidationCode::kTerrainDefinitionInvalid, std::string{context},
        "terrain shape name must be a snake_case identity within the accepted kind-name length"};
  }
}

void require_terrain_extent(const double value, const std::string_view context) {
  if (!std::isfinite(value) || value <= 0.0 || value > kMaximumWorldDimension) {
    throw SimulationValidationError{
        SimulationValidationCode::kTerrainDefinitionInvalid, std::string{context},
        "terrain extent must be finite, greater than zero, and at most 1000000000"};
  }
}

void require_terrain_count(const std::size_t actual, const std::size_t limit,
                           const std::string_view context) {
  if (actual > limit) {
    throw SimulationValidationError{SimulationValidationCode::kTerrainShapeLimitExceeded,
                                    std::string{context},
                                    "terrain shape count " + std::to_string(actual) +
                                        " exceeds limit " + std::to_string(limit)};
  }
}

} // namespace

TerrainCorridor TerrainCorridor::create(std::string name, const double half_width,
                                        std::vector<Vector2> points) {
  require_terrain_name(name, "terrain_corridor.name");
  require_terrain_extent(half_width, "terrain_corridor.half_width");
  if (points.size() < 2) {
    throw SimulationValidationError{SimulationValidationCode::kTerrainDefinitionInvalid,
                                    "terrain_corridor.points",
                                    "a corridor requires at least two points"};
  }
  require_terrain_count(points.size(), kMaximumTerrainPointCount, "terrain_corridor.points");
  require_terrain_count(points.size() - 1, kMaximumTerrainSegmentCount,
                        "terrain_corridor.segments");

  for (std::size_t index = 1; index < points.size(); ++index) {
    const Vector2& begin = points[index - 1];
    const Vector2& end = points[index];
    if (begin == end) {
      throw SimulationValidationError{
          SimulationValidationCode::kTerrainDefinitionInvalid,
          "terrain_corridor.points[" + std::to_string(index) + "]",
          "consecutive corridor points must define a nonzero-length segment"};
    }
    const double dx = end.x() - begin.x();
    const double dy = end.y() - begin.y();
    const double squared_length = dx * dx + dy * dy;
    if (!std::isfinite(squared_length) || squared_length == 0.0) {
      throw SimulationValidationError{
          SimulationValidationCode::kTerrainGeometryPrecisionLost,
          "terrain_corridor.points[" + std::to_string(index) + "]",
          "corridor segment squared length must be finite and representably greater than zero"};
    }
  }

  return TerrainCorridor{std::move(name), half_width, std::move(points)};
}

TerrainCorridor::TerrainCorridor(std::string name, const double half_width,
                                 std::vector<Vector2> points) noexcept
    : name_(std::move(name)), half_width_(half_width), points_(std::move(points)) {}

TerrainHole TerrainHole::create(std::string name, Vector2 center, const double radius) {
  require_terrain_name(name, "terrain_hole.name");
  require_terrain_extent(radius, "terrain_hole.radius");
  return TerrainHole{std::move(name), center, radius};
}

TerrainHole::TerrainHole(std::string name, Vector2 center, const double radius) noexcept
    : name_(std::move(name)), center_(center), radius_(radius) {}

TerrainDefinition TerrainDefinition::create(const ArenaBounds bounds, const TerrainGround ground,
                                            std::vector<TerrainCorridor> corridors,
                                            std::vector<TerrainHole> holes) {
  if (ground != TerrainGround::kSolid && ground != TerrainGround::kCorridors) {
    throw SimulationValidationError{SimulationValidationCode::kTerrainDefinitionInvalid,
                                    "terrain_definition.ground",
                                    "terrain ground must be solid or corridors"};
  }
  if ((ground == TerrainGround::kSolid && !corridors.empty()) ||
      (ground == TerrainGround::kCorridors && corridors.empty())) {
    throw SimulationValidationError{
        SimulationValidationCode::kTerrainDefinitionInvalid, "terrain_definition.corridors",
        "solid ground forbids corridors and corridor ground requires at least one corridor"};
  }
  require_terrain_count(corridors.size(), kMaximumTerrainCorridorCount,
                        "terrain_definition.corridors");
  require_terrain_count(holes.size(), kMaximumTerrainHoleCount, "terrain_definition.holes");

  std::size_t point_count = 0;
  std::size_t segment_count = 0;
  for (std::size_t index = 0; index < corridors.size(); ++index) {
    const TerrainCorridor& corridor = corridors[index];
    point_count += corridor.points().size();
    segment_count += corridor.points().size() - 1;
    require_terrain_count(point_count, kMaximumTerrainPointCount, "terrain_definition.points");
    require_terrain_count(segment_count, kMaximumTerrainSegmentCount,
                          "terrain_definition.segments");
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (corridors[earlier].name() == corridor.name()) {
        throw SimulationValidationError{SimulationValidationCode::kTerrainDefinitionInvalid,
                                        "terrain_definition.corridors",
                                        "duplicate corridor name " + std::string{corridor.name()}};
      }
    }
    for (std::size_t point_index = 0; point_index < corridor.points().size(); ++point_index) {
      if (!bounds.contains(corridor.points()[point_index])) {
        throw SimulationValidationError{SimulationValidationCode::kTerrainGeometryOutOfBounds,
                                        "terrain_definition.corridors[" + std::to_string(index) +
                                            "].points[" + std::to_string(point_index) + "]",
                                        "corridor point must lie inside the closed arena envelope"};
      }
    }
  }
  for (std::size_t index = 0; index < holes.size(); ++index) {
    const TerrainHole& hole = holes[index];
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (holes[earlier].name() == hole.name()) {
        throw SimulationValidationError{SimulationValidationCode::kTerrainDefinitionInvalid,
                                        "terrain_definition.holes",
                                        "duplicate hole name " + std::string{hole.name()}};
      }
    }
    if (!bounds.contains(hole.center())) {
      throw SimulationValidationError{SimulationValidationCode::kTerrainGeometryOutOfBounds,
                                      "terrain_definition.holes[" + std::to_string(index) +
                                          "].center",
                                      "hole center must lie inside the closed arena envelope"};
    }
  }

  return TerrainDefinition{std::make_shared<const detail::TerrainStorage>(
      bounds, ground, std::move(corridors), std::move(holes))};
}

TerrainDefinition TerrainDefinition::solid(const ArenaBounds bounds) {
  return create(bounds, TerrainGround::kSolid, {}, {});
}

const ArenaBounds& TerrainDefinition::bounds() const& noexcept { return storage_->bounds; }

TerrainGround TerrainDefinition::ground() const noexcept { return storage_->ground; }

std::span<const TerrainCorridor> TerrainDefinition::corridors() const& noexcept {
  return storage_->corridors;
}

std::span<const TerrainHole> TerrainDefinition::holes() const& noexcept { return storage_->holes; }

const TerrainCorridor*
TerrainDefinition::find_corridor(const std::string_view name) const& noexcept {
  for (const TerrainCorridor& corridor : storage_->corridors) {
    if (corridor.name() == name) {
      return &corridor;
    }
  }
  return nullptr;
}

bool operator==(const TerrainDefinition& left, const TerrainDefinition& right) {
  return left.storage_->bounds == right.storage_->bounds &&
         left.storage_->ground == right.storage_->ground &&
         left.storage_->corridors == right.storage_->corridors &&
         left.storage_->holes == right.storage_->holes;
}

TerrainDefinition::TerrainDefinition(std::shared_ptr<const detail::TerrainStorage> storage) noexcept
    : storage_(std::move(storage)) {}

} // namespace blob_royale::simulation
