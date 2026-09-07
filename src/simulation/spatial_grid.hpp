#ifndef BLOB_ROYALE_SIMULATION_SPATIAL_GRID_HPP
#define BLOB_ROYALE_SIMULATION_SPATIAL_GRID_HPP

#include "candidate_pair.hpp"
#include "cell_coord.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace blob_royale::simulation {

// canonical: spatial_grid -- deterministic broad-phase membership and candidate formation.
//
// The arena comes from the map, not from the configuration: `ArenaBounds` is what the cells
// partition and what a body's coverage is clamped to. The configuration still supplies the cell
// counts and the one common disc radius.
//
// A **static** body is indexed like any other: it must be a broad-phase member for
// `reflect_static` to see it, and it is exempt only from the disc-centre interval, because a
// wall's centre legitimately sits on the arena edge. Its centre must still lie inside the closed
// arena rectangle, which is what keeps every coverage box non-empty.
class SpatialGrid final {
public:
  // Builds complete membership and canonical candidate pairs from an ID-ordered world over the
  // map's arena.
  [[nodiscard]] static SpatialGrid create(SimulationConfig configuration, ArenaBounds bounds,
                                          const GameWorld& world);

  // The same over the bare rectangular arena the configuration still publishes. This is the
  // pre-map call every caller written before ADR 0004 uses, and it is exactly what
  // `GameSimulation::create(configuration, world)` synthesizes.
  [[nodiscard]] static SpatialGrid create(SimulationConfig configuration, const GameWorld& world);

  SpatialGrid(const SpatialGrid&) = default;
  SpatialGrid(SpatialGrid&&) noexcept = default;
  SpatialGrid& operator=(const SpatialGrid&) = default;
  SpatialGrid& operator=(SpatialGrid&&) noexcept = default;
  ~SpatialGrid() = default;

  [[nodiscard]] std::size_t row_count() const noexcept {
    return configuration_.spatial_grid_rows();
  }
  [[nodiscard]] std::size_t column_count() const noexcept {
    return configuration_.spatial_grid_columns();
  }
  [[nodiscard]] double cell_width() const noexcept {
    return bounds_.width() / static_cast<double>(column_count());
  }
  [[nodiscard]] double cell_height() const noexcept {
    return bounds_.height() / static_cast<double>(row_count());
  }

  [[nodiscard]] const ArenaBounds& bounds() const& noexcept { return bounds_; }
  [[nodiscard]] const ArenaBounds& bounds() const&& = delete;

  // Returns the positive-side home cell for a point in the closed world rectangle.
  [[nodiscard]] CellCoord home_cell(const Vector2& point) const;

  // Returns ascending IDs in one cell or throws for a coordinate outside this grid.
  [[nodiscard]] std::span<const EntityId> cell_members(CellCoord coordinate) const&;
  [[nodiscard]] std::span<const EntityId> cell_members(CellCoord coordinate) const&& = delete;

  // Returns distinct canonical pairs in lexicographic EntityId order.
  [[nodiscard]] std::span<const CandidatePair> candidate_pairs() const& noexcept {
    return candidate_pairs_;
  }
  [[nodiscard]] std::span<const CandidatePair> candidate_pairs() const&& = delete;

  // Produces a complete replacement while retaining the already-validated geometry.
  [[nodiscard]] SpatialGrid rebuilt(const GameWorld& world) const&;
  [[nodiscard]] SpatialGrid rebuilt(const GameWorld& world) const&& = delete;

  friend bool operator==(const SpatialGrid&, const SpatialGrid&) = default;

private:
  using Cell = std::vector<EntityId>;

  SpatialGrid(SimulationConfig configuration, ArenaBounds bounds, std::vector<Cell> cells,
              std::vector<CandidatePair> candidate_pairs) noexcept;

  SimulationConfig configuration_;
  ArenaBounds bounds_;
  std::vector<Cell> cells_;
  std::vector<CandidatePair> candidate_pairs_;
};

} // namespace blob_royale::simulation

#endif
