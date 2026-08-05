#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_CONFIG_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_CONFIG_HPP

#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <cstddef>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: simulation_config -- validated immutable geometry, cadence, and grid shape.
class SimulationConfig final {
public:
  static constexpr std::uint64_t kRequiredTicksPerSecond = kSimulationTicksPerSecond;
  static constexpr double kFixedDeltaSeconds = blob_royale::simulation::kFixedDeltaSeconds;
  static constexpr double kMaximumWorldDimension = blob_royale::simulation::kMaximumWorldDimension;
  static constexpr std::size_t kMaximumSpatialGridCellCount =
      blob_royale::simulation::kMaximumSpatialGridCellCount;

  // Creates a complete configuration or throws SimulationValidationError.
  [[nodiscard]] static SimulationConfig create(double world_width, double world_height,
                                               double player_radius, std::uint64_t ticks_per_second,
                                               std::uint64_t spatial_grid_columns,
                                               std::uint64_t spatial_grid_rows);

  SimulationConfig(const SimulationConfig&) = default;
  SimulationConfig(SimulationConfig&&) noexcept = default;
  SimulationConfig& operator=(const SimulationConfig&) = default;
  SimulationConfig& operator=(SimulationConfig&&) noexcept = default;
  ~SimulationConfig() = default;

  [[nodiscard]] double world_width() const noexcept { return world_width_; }
  [[nodiscard]] double world_height() const noexcept { return world_height_; }
  [[nodiscard]] double player_radius() const noexcept { return player_radius_; }
  [[nodiscard]] std::uint64_t ticks_per_second() const noexcept { return kRequiredTicksPerSecond; }
  [[nodiscard]] double fixed_delta_seconds() const noexcept { return kFixedDeltaSeconds; }
  [[nodiscard]] std::size_t spatial_grid_columns() const noexcept { return spatial_grid_columns_; }
  [[nodiscard]] std::size_t spatial_grid_rows() const noexcept { return spatial_grid_rows_; }
  [[nodiscard]] std::size_t spatial_grid_cell_count() const noexcept {
    return spatial_grid_cell_count_;
  }

  // Tests whether a validated point is a legal center for the configured common player disc.
  [[nodiscard]] bool contains_player_center(const Vector2& position) const noexcept;

  friend bool operator==(const SimulationConfig&, const SimulationConfig&) = default;

private:
  SimulationConfig(double world_width, double world_height, double player_radius,
                   std::size_t spatial_grid_columns, std::size_t spatial_grid_rows,
                   std::size_t spatial_grid_cell_count) noexcept;

  double world_width_;
  double world_height_;
  double player_radius_;
  std::size_t spatial_grid_columns_;
  std::size_t spatial_grid_rows_;
  std::size_t spatial_grid_cell_count_;
};

} // namespace blob_royale::simulation

#endif
