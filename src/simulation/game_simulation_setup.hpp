#ifndef BLOB_ROYALE_SIMULATION_GAME_SIMULATION_SETUP_HPP
#define BLOB_ROYALE_SIMULATION_GAME_SIMULATION_SETUP_HPP

#include "contact_rule_table.hpp"
#include "game_mode.hpp"
#include "map_definition.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <optional>

namespace blob_royale::simulation {

class GameSimulation;

// canonical: game_simulation_setup -- everything a simulation is declared with, as one value.
//
// This replaces the four `GameSimulation::create` overloads the engine review flagged as future
// pain. `GameSimulation::create` now has exactly **one** signature -- the configuration, the
// initial world, and this value -- and every declaration a mode makes arrives through here rather
// than through a positional argument, so a sixth declaration is a member on this class and never a
// new overload.
//
// The configuration and the initial world stay positional because every simulation has exactly
// one of each and neither is optional; a simulation with no configuration or no world is not a
// thing that can be built. Everything here is a *declaration*, and every declaration has an engine
// default, which is what makes `create(configuration, world)` -- the shape every caller written
// before modes existed uses, including `blob_application` -- still mean what it always meant.
//
// **A mode declares everything.** `of_mode` stores it; GameSimulation::create reads its eight
// declarations once and destroys the mode. Declaring a mode *and* an explicit system pipeline,
// contact table, or trigger table is rejected, because two declarations of one thing is exactly
// the ambiguity collapsing the overloads was meant to remove.
//
// Without a mode the engine's own declarations apply: the mode name `idle`, the built-in contact
// rules, every command kind accepted, a spawn policy that never seats, and an objective that never
// starts a match, and an empty motion-trigger table. Resource limits are lower-only engine
// correctness settings, not a ninth mode declaration.
// related: game_mode.hpp -- the eight declarations GameSimulation::create reads.
// related: game_simulation.hpp -- the one factory that consumes this.
class GameSimulationSetup final {
public:
  // The engine's own declarations: no mode, no map, no system, the built-in contact rules.
  [[nodiscard]] static GameSimulationSetup engine_defaults();

  // The production shape, and the value form of `create(configuration, map, mode)`.
  [[nodiscard]] static GameSimulationSetup of_mode(MapDefinition map,
                                                   std::unique_ptr<const GameMode> mode);

  GameSimulationSetup(const GameSimulationSetup&) = delete;
  GameSimulationSetup(GameSimulationSetup&&) noexcept = default;
  GameSimulationSetup& operator=(const GameSimulationSetup&) = delete;
  GameSimulationSetup& operator=(GameSimulationSetup&&) noexcept = default;
  ~GameSimulationSetup() = default;

  // Each setter returns a new value rather than a reference, so a setup is built in one expression
  // and a chained call can never leave a reference to a destroyed temporary.
  [[nodiscard]] GameSimulationSetup with_map(MapDefinition map) &&;
  [[nodiscard]] GameSimulationSetup with_mode(std::unique_ptr<const GameMode> mode) &&;

  // The kernel-test declarations: an explicit pipeline or table with no mode behind it. A mode
  // that wants these declares them through its corresponding independently owned declarations.
  [[nodiscard]] GameSimulationSetup with_systems(SystemPipeline systems) &&;
  [[nodiscard]] GameSimulationSetup with_contact_rules(ContactRuleTable contact_rules) &&;
  [[nodiscard]] GameSimulationSetup with_motion_triggers(MotionTriggerTable motion_triggers) &&;

  // Lower-only correctness budgets, validated by the solver's canonical validator. These are
  // not authored gameplay configuration or another mode declaration; excessive limits fail.
  [[nodiscard]] GameSimulationSetup with_motion_limits(MotionLimits motion_limits) &&;

  [[nodiscard]] bool has_map() const noexcept { return map_.has_value(); }
  [[nodiscard]] bool has_mode() const noexcept { return mode_ != nullptr; }
  [[nodiscard]] bool has_systems() const noexcept { return systems_.has_value(); }
  [[nodiscard]] bool has_contact_rules() const noexcept { return contact_rules_.has_value(); }
  [[nodiscard]] bool has_motion_triggers() const noexcept { return motion_triggers_.has_value(); }

private:
  friend class GameSimulation;

  GameSimulationSetup() = default;

  std::optional<MapDefinition> map_;
  std::unique_ptr<const GameMode> mode_;
  std::optional<SystemPipeline> systems_;
  std::optional<ContactRuleTable> contact_rules_;
  std::optional<MotionTriggerTable> motion_triggers_;
  MotionLimits motion_limits_;
};

} // namespace blob_royale::simulation

#endif
