#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_CONFIG_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_CONFIG_HPP

#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <cstddef>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: simulation_config -- validated immutable geometry, cadence, and grid shape.
//
// **The arena is no longer here.** `MapDefinition::bounds()` is what phase 4 folds against, what
// the commit-time bounds check validates, and what SpatialGrid partitions
// (`docs/architecture/0004-gameplay-architecture.md` § "Maps as data"). `world_width` and
// `world_height` are retained because two accepted consumers outside the kernel still read them:
// protocol v1's `/api/v1/config` publishes them through `PublicConfiguration`, and
// `ScenarioLoader` validates a seeded centre against `contains_player_center`. Authoring moved
// into the map directory with `src/application/map_loader.hpp`, so these are now the *published*
// view of an arena the map declares, and `src/application/match_startup_validation.hpp` rejects a
// configuration whose map and `[world]` scalars disagree. The overloads that take no map synthesize
// the map's bounds from exactly these scalars, so the two views cannot disagree there either.
class SimulationConfig final {
public:
  static constexpr std::uint64_t kRequiredTicksPerSecond = kSimulationTicksPerSecond;
  static constexpr double kFixedDeltaSeconds = blob_royale::simulation::kFixedDeltaSeconds;
  static constexpr double kMaximumWorldDimension = blob_royale::simulation::kMaximumWorldDimension;
  static constexpr std::size_t kMaximumSpatialGridCellCount =
      blob_royale::simulation::kMaximumSpatialGridCellCount;
  // The accepted baseline. At zero the phase 1 drag factor is exactly 1.0 and multiplication by
  // 1.0 is the identity on every finite binary64 value, so every fixture horizon accepted before
  // drag existed stays bit-identical
  // (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
  static constexpr double kDefaultDragPerSecond = 0.0;

  // Creates a complete configuration or throws SimulationValidationError.
  //
  // `drag_per_second` is a trailing defaulted parameter rather than a seventh positional argument
  // on every caller: it is a kernel parameter whose accepted value is zero, every fixture, test,
  // and benchmark runs at zero, and only the deployment configuration sets a nonzero value, so
  // defaulting keeps the one meaningful call site -- the configuration loader -- the only place
  // that has to name it. The loader key is `[simulation] drag_per_second`.
  [[nodiscard]] static SimulationConfig create(double world_width, double world_height,
                                               double player_radius, std::uint64_t ticks_per_second,
                                               std::uint64_t spatial_grid_columns,
                                               std::uint64_t spatial_grid_rows,
                                               double drag_per_second = kDefaultDragPerSecond);

  SimulationConfig(const SimulationConfig&) = default;
  SimulationConfig(SimulationConfig&&) noexcept = default;
  SimulationConfig& operator=(const SimulationConfig&) = default;
  SimulationConfig& operator=(SimulationConfig&&) noexcept = default;
  ~SimulationConfig() = default;

  // The published arena size, not the one the kernel folds against; see the class comment.
  [[nodiscard]] double world_width() const noexcept { return world_width_; }
  [[nodiscard]] double world_height() const noexcept { return world_height_; }
  [[nodiscard]] double player_radius() const noexcept { return player_radius_; }
  // The phase 1 velocity decay rate. Drag is kernel mechanism this configuration owns rather than
  // mode configuration: it applies identically under every mode and no system may reproduce or
  // bypass it.
  [[nodiscard]] double drag_per_second() const noexcept { return drag_per_second_; }
  [[nodiscard]] std::uint64_t ticks_per_second() const noexcept { return kRequiredTicksPerSecond; }
  [[nodiscard]] double fixed_delta_seconds() const noexcept { return kFixedDeltaSeconds; }
  [[nodiscard]] std::size_t spatial_grid_columns() const noexcept { return spatial_grid_columns_; }
  [[nodiscard]] std::size_t spatial_grid_rows() const noexcept { return spatial_grid_rows_; }
  [[nodiscard]] std::size_t spatial_grid_cell_count() const noexcept {
    return spatial_grid_cell_count_;
  }

  // Tests whether a validated point is a legal center for the configured common player disc, over
  // the published world rectangle. The kernel asks `ArenaBounds::contains_disc_center` instead;
  // this remains the loader's seeding check (`src/application/scenario_loader.cpp`).
  [[nodiscard]] bool contains_player_center(const Vector2& position) const noexcept;

  friend bool operator==(const SimulationConfig&, const SimulationConfig&) = default;

private:
  SimulationConfig(double world_width, double world_height, double player_radius,
                   double drag_per_second, std::size_t spatial_grid_columns,
                   std::size_t spatial_grid_rows, std::size_t spatial_grid_cell_count) noexcept;

  double world_width_;
  double world_height_;
  double player_radius_;
  double drag_per_second_;
  std::size_t spatial_grid_columns_;
  std::size_t spatial_grid_rows_;
  std::size_t spatial_grid_cell_count_;
};

} // namespace blob_royale::simulation

#endif
