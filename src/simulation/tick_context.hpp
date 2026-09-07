#ifndef BLOB_ROYALE_SIMULATION_TICK_CONTEXT_HPP
#define BLOB_ROYALE_SIMULATION_TICK_CONTEXT_HPP

#include "fixed_delta.hpp"
#include "simulation_config.hpp"
#include "tick_sequence.hpp"

#include <utility>

namespace blob_royale::simulation {

// canonical: tick_context -- everything a system may read that is not game-world state.
//
// The context is mode-agnostic and tiny by construction. It deliberately exposes **no clock** and
// **no InputBatch**: commands reach a system only as `Controllable::commands_this_tick` and as
// world events, so no system can observe a half-applied batch or reinterpret the intake order, and
// no system can read a wall time
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages").
//
// `tick_sequence()` is the sequence **this tick commits** -- one greater than the last committed
// sequence -- so a duration comparison or a recorded tick names the tick that publishes its result
// and the snapshot for tick N is internally consistent
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// ADR 0004 also declares `map()`, returning the tick's `const MapDefinition&`. It is deliberately
// absent here: `MapDefinition` does not exist until Step 18 replaces the world-size configuration
// with it, and an accessor to a type nothing can construct would be untested structure. Step 18
// adds the member and the field it reads together.
//
// The configuration is held by value rather than by reference: it is seven scalars, a tick
// constructs exactly one context, and a value member cannot dangle behind a system that outlives a
// simulation.
// related: simulation_system.hpp -- the interface that receives this value.
class TickContext final {
public:
  [[nodiscard]] static TickContext create(const TickSequence tick_sequence,
                                          const FixedDelta fixed_delta,
                                          SimulationConfig simulation_config) noexcept {
    return TickContext(tick_sequence, fixed_delta, std::move(simulation_config));
  }

  TickContext(const TickContext&) = default;
  TickContext(TickContext&&) noexcept = default;
  TickContext& operator=(const TickContext&) = default;
  TickContext& operator=(TickContext&&) noexcept = default;
  ~TickContext() = default;

  // The sequence this tick commits at phase 10.
  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  [[nodiscard]] FixedDelta fixed_delta() const noexcept { return fixed_delta_; }

  [[nodiscard]] const SimulationConfig& simulation_config() const& noexcept {
    return simulation_config_;
  }
  [[nodiscard]] const SimulationConfig& simulation_config() const&& = delete;

  friend bool operator==(const TickContext&, const TickContext&) = default;

private:
  TickContext(const TickSequence tick_sequence, const FixedDelta fixed_delta,
              SimulationConfig simulation_config) noexcept
      : tick_sequence_(tick_sequence), fixed_delta_(fixed_delta),
        simulation_config_(std::move(simulation_config)) {}

  TickSequence tick_sequence_;
  FixedDelta fixed_delta_;
  SimulationConfig simulation_config_;
};

} // namespace blob_royale::simulation

#endif
