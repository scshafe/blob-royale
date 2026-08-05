#include "simulation_config.hpp"

#include "simulation_validation_error.hpp"

#include <cmath>

namespace blob_royale::simulation {
namespace {

void require_world_scalar(const double value, const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError{SimulationValidationCode::kConfigWorldScalarNotFinite,
                                    std::string{context}, "value must be finite"};
  }
  if (value <= 0.0 || value > SimulationConfig::kMaximumWorldDimension) {
    throw SimulationValidationError{SimulationValidationCode::kConfigWorldScalarOutOfRange,
                                    std::string{context},
                                    "value must be greater than zero and at most 1000000000"};
  }
}

} // namespace

SimulationConfig SimulationConfig::create(const double world_width, const double world_height,
                                          const double player_radius,
                                          const std::uint64_t ticks_per_second,
                                          const std::uint64_t spatial_grid_columns,
                                          const std::uint64_t spatial_grid_rows) {
  require_world_scalar(world_width, "world.width_world_units");
  require_world_scalar(world_height, "world.height_world_units");
  require_world_scalar(player_radius, "world.player_radius_world_units");

  if (world_width <= 2.0 * player_radius || world_height <= 2.0 * player_radius) {
    throw SimulationValidationError{
        SimulationValidationCode::kConfigWorldTooSmallForPlayer, "world",
        "width and height must each be greater than twice the player radius"};
  }

  if (ticks_per_second != kRequiredTicksPerSecond) {
    throw SimulationValidationError{
        SimulationValidationCode::kConfigTickRateUnsupported, "simulation.ticks_per_second",
        "the deterministic simulation contract requires exactly 400 ticks per second"};
  }

  if (spatial_grid_columns == 0 || spatial_grid_rows == 0 ||
      spatial_grid_columns > kMaximumSpatialGridCellCount ||
      spatial_grid_rows > kMaximumSpatialGridCellCount) {
    throw SimulationValidationError{SimulationValidationCode::kConfigSpatialGridDimensionOutOfRange,
                                    "spatial_grid",
                                    "columns and rows must each be between 1 and 1048576"};
  }

  const auto maximum_cell_count = static_cast<std::uint64_t>(kMaximumSpatialGridCellCount);
  if (spatial_grid_columns > maximum_cell_count / spatial_grid_rows) {
    throw SimulationValidationError{SimulationValidationCode::kConfigSpatialGridCellLimitExceeded,
                                    "spatial_grid",
                                    "columns multiplied by rows must not exceed 1048576 cells"};
  }

  const double cell_width = world_width / static_cast<double>(spatial_grid_columns);
  const double cell_height = world_height / static_cast<double>(spatial_grid_rows);
  if (!std::isfinite(cell_width) || cell_width <= 0.0 || !std::isfinite(cell_height) ||
      cell_height <= 0.0) {
    throw SimulationValidationError{
        SimulationValidationCode::kConfigSpatialGridDimensionOutOfRange, "spatial_grid",
        "configured world and grid dimensions must produce positive finite cell extents"};
  }

  const auto column_count = static_cast<std::size_t>(spatial_grid_columns);
  const auto row_count = static_cast<std::size_t>(spatial_grid_rows);
  const auto cell_count = static_cast<std::size_t>(spatial_grid_columns * spatial_grid_rows);
  return SimulationConfig{world_width,  world_height, player_radius,
                          column_count, row_count,    cell_count};
}

SimulationConfig::SimulationConfig(const double world_width, const double world_height,
                                   const double player_radius,
                                   const std::size_t spatial_grid_columns,
                                   const std::size_t spatial_grid_rows,
                                   const std::size_t spatial_grid_cell_count) noexcept
    : world_width_(world_width), world_height_(world_height), player_radius_(player_radius),
      spatial_grid_columns_(spatial_grid_columns), spatial_grid_rows_(spatial_grid_rows),
      spatial_grid_cell_count_(spatial_grid_cell_count) {}

bool SimulationConfig::contains_player_center(const Vector2& position) const noexcept {
  return position.x() >= player_radius_ && position.x() <= world_width_ - player_radius_ &&
         position.y() >= player_radius_ && position.y() <= world_height_ - player_radius_;
}

} // namespace blob_royale::simulation
