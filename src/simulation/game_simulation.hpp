#ifndef BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP
#define BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP

#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "simulation_config.hpp"
#include "spatial_grid.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

namespace blob_royale::simulation {

// canonical: game_simulation -- the sole mutable owner of one deterministic world timeline.
class GameSimulation final {
public:
  // Validates the complete loaded state, builds its initial spatial index, and commits tick zero.
  [[nodiscard]] static GameSimulation create(SimulationConfig configuration,
                                             GameWorld initial_world);

  GameSimulation(const GameSimulation&) = delete;
  GameSimulation(GameSimulation&&) noexcept = default;
  GameSimulation& operator=(const GameSimulation&) = delete;
  GameSimulation& operator=(GameSimulation&&) noexcept = default;
  ~GameSimulation() = default;

  [[nodiscard]] const SimulationConfig& configuration() const& noexcept { return configuration_; }
  [[nodiscard]] const SimulationConfig& configuration() const&& = delete;
  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  // Advances exactly one canonical fixed step. Every phase is evaluated against transaction-local
  // values; a failure leaves the previously committed world, grid, and sequence unchanged.
  void step(FixedDelta fixed_delta);

  // Copies one complete committed state. The returned value never aliases the mutable world.
  [[nodiscard]] WorldSnapshot snapshot() const;

private:
  GameSimulation(SimulationConfig configuration, GameWorld world, SpatialGrid grid,
                 TickSequence tick_sequence) noexcept;

  SimulationConfig configuration_;
  GameWorld world_;
  SpatialGrid grid_;
  TickSequence tick_sequence_;
};

} // namespace blob_royale::simulation

#endif
