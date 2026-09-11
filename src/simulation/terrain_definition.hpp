#ifndef BLOB_ROYALE_SIMULATION_TERRAIN_DEFINITION_HPP
#define BLOB_ROYALE_SIMULATION_TERRAIN_DEFINITION_HPP

#include "arena_bounds.hpp"
#include "vector2.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

namespace detail {
struct TerrainStorage;
struct TerrainQueryAccess;
} // namespace detail

// The positive ground is either the complete envelope or the union of authored corridors.
enum class TerrainGround { kSolid, kCorridors };

// A named polyline widened by half_width, with circular endpoint caps and round joins.
class TerrainCorridor final {
public:
  // Creates an owned corridor. Rejects non-snake_case/oversized names, non-positive/non-finite
  // width, fewer than two or too many points, and zero-length/unrepresentable segments with
  // SIMULATION.TERRAIN_DEFINITION_INVALID, TERRAIN_SHAPE_LIMIT_EXCEEDED, or
  // TERRAIN_GEOMETRY_PRECISION_LOST. Envelope containment is checked by TerrainDefinition.
  [[nodiscard]] static TerrainCorridor create(std::string name, double half_width,
                                              std::vector<Vector2> points);

  [[nodiscard]] std::string_view name() const& noexcept { return name_; }
  [[nodiscard]] std::string_view name() const&& = delete;
  [[nodiscard]] double half_width() const noexcept { return half_width_; }
  [[nodiscard]] std::span<const Vector2> points() const& noexcept { return points_; }
  [[nodiscard]] std::span<const Vector2> points() const&& = delete;

  friend bool operator==(const TerrainCorridor&, const TerrainCorridor&) = default;

private:
  TerrainCorridor(std::string name, double half_width, std::vector<Vector2> points) noexcept;

  std::string name_;
  double half_width_;
  std::vector<Vector2> points_;
};

// A named circular subtraction. Its open interior is unsupported; the exact rim stays ground.
class TerrainHole final {
public:
  // Creates an owned hole. Rejects invalid names and non-finite/non-positive/oversized radius
  // with SIMULATION.TERRAIN_DEFINITION_INVALID. The definition checks center containment.
  [[nodiscard]] static TerrainHole create(std::string name, Vector2 center, double radius);

  [[nodiscard]] std::string_view name() const& noexcept { return name_; }
  [[nodiscard]] std::string_view name() const&& = delete;
  [[nodiscard]] const Vector2& center() const& noexcept { return center_; }
  [[nodiscard]] const Vector2& center() const&& = delete;
  [[nodiscard]] double radius() const noexcept { return radius_; }

  friend bool operator==(const TerrainHole&, const TerrainHole&) = default;

private:
  TerrainHole(std::string name, Vector2 center, double radius) noexcept;

  std::string name_;
  Vector2 center_;
  double radius_;
};

// canonical: terrain_definition -- the one immutable, validated authored terrain value.
//
// Copying shares both authored content and its derived boundary arrangement, compiled exactly
// once. Equality compares only authored fields, never storage identity or cached geometry.
// related: terrain_queries.hpp -- all support, clearance, and sweep readers use that owner.
class TerrainDefinition final {
public:
  // Creates bounded terrain or throws SimulationValidationError. Solid ground forbids corridors;
  // corridor ground requires at least one. Names are unique within each family, authored centers
  // and polyline points must be inside bounds, and aggregate shape limits apply. Fully subtracted
  // terrain is valid content: startup separately rejects unsupported spawn points.
  // Codes: SIMULATION.TERRAIN_DEFINITION_INVALID, TERRAIN_SHAPE_LIMIT_EXCEEDED,
  // TERRAIN_GEOMETRY_OUT_OF_BOUNDS, TERRAIN_GEOMETRY_PRECISION_LOST,
  // TERRAIN_BOUNDARY_LIMIT_EXCEEDED.
  [[nodiscard]] static TerrainDefinition create(ArenaBounds bounds, TerrainGround ground,
                                                std::vector<TerrainCorridor> corridors,
                                                std::vector<TerrainHole> holes);

  // Explicit solid-ground value for callers authoring an unobstructed rectangle.
  [[nodiscard]] static TerrainDefinition solid(ArenaBounds bounds);

  TerrainDefinition(const TerrainDefinition&) = default;
  TerrainDefinition(TerrainDefinition&&) noexcept = default;
  TerrainDefinition& operator=(const TerrainDefinition&) = default;
  TerrainDefinition& operator=(TerrainDefinition&&) noexcept = default;
  ~TerrainDefinition() = default;

  [[nodiscard]] const ArenaBounds& bounds() const& noexcept;
  [[nodiscard]] const ArenaBounds& bounds() const&& = delete;
  [[nodiscard]] TerrainGround ground() const noexcept;
  [[nodiscard]] std::span<const TerrainCorridor> corridors() const& noexcept;
  [[nodiscard]] std::span<const TerrainCorridor> corridors() const&& = delete;
  [[nodiscard]] std::span<const TerrainHole> holes() const& noexcept;
  [[nodiscard]] std::span<const TerrainHole> holes() const&& = delete;

  // Named corridor in authored order, or nullptr for a name that was not declared.
  [[nodiscard]] const TerrainCorridor* find_corridor(std::string_view name) const& noexcept;
  [[nodiscard]] const TerrainCorridor* find_corridor(std::string_view name) const&& = delete;

  friend bool operator==(const TerrainDefinition& left, const TerrainDefinition& right);

private:
  friend struct detail::TerrainQueryAccess;
  explicit TerrainDefinition(std::shared_ptr<const detail::TerrainStorage> storage) noexcept;

  std::shared_ptr<const detail::TerrainStorage> storage_;
};

} // namespace blob_royale::simulation

#endif
