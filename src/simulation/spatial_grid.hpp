#ifndef BLOB_ROYALE_SIMULATION_SPATIAL_GRID_HPP
#define BLOB_ROYALE_SIMULATION_SPATIAL_GRID_HPP

#include "candidate_pair.hpp"
#include "cell_coord.hpp"
#include "game_world.hpp"
#include "simulation_config.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace blob_royale::simulation {

// canonical: spatial_grid -- deterministic broad-phase membership and candidate formation.
class SpatialGrid final {
public:
  // Builds complete membership and canonical candidate pairs from an ID-ordered world.
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
    return configuration_.world_width() / static_cast<double>(column_count());
  }
  [[nodiscard]] double cell_height() const noexcept {
    return configuration_.world_height() / static_cast<double>(row_count());
  }

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

  SpatialGrid(SimulationConfig configuration, std::vector<Cell> cells,
              std::vector<CandidatePair> candidate_pairs) noexcept;

  SimulationConfig configuration_;
  std::vector<Cell> cells_;
  std::vector<CandidatePair> candidate_pairs_;
};

} // namespace blob_royale::simulation

#endif
