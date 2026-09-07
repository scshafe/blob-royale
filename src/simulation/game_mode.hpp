#ifndef BLOB_ROYALE_SIMULATION_GAME_MODE_HPP
#define BLOB_ROYALE_SIMULATION_GAME_MODE_HPP

#include "command_kind_mask.hpp"
#include "contact_rule_table.hpp"
#include "map_definition.hpp"
#include "match_objective.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::simulation {

// canonical: game_mode -- the complete declared ruleset of one playable game.
// @extension-point game_mode
//
// This is the one accepted inheritance hierarchy in the framework, and inheritance is honest here:
// every mode genuinely *is* a GameMode, the simulation calls the same operations on each, and a
// mode is substitutable at composition without any other target changing
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// **GameSimulation calls each declaration exactly once, at construction, and stores the results.
// Nothing calls into the mode during a tick.** That is what makes the tick's determinism a
// property of the engine rather than of every mode author's discipline: a mode can participate in
// a tick only through the systems, rules, policy, and objective it declared.
//
// **The engine destroys the mode once it has read the seven declarations.** "Nothing calls into
// the mode during a tick" is therefore structurally true rather than a rule to be remembered, and
// it makes one obligation on an implementor explicit: every value a declaration returns must be
// **independently owned**. A system, policy, or objective may not hold a pointer or reference back
// into the mode object. This costs nothing, because mode configuration is already constructor
// state of the systems a mode builds -- `ZoneShrinkSystem` holds a copy of the validated royale
// configuration as a const member -- and it is what lets the kernel hold declarations rather than
// a mode.
//
// `validate_map` is the seventh member and earns its place by turning "capture the flag needs two
// flag homes" from a runtime surprise into a startup rejection with a named cause.
//
// Adding a game:
//
//   new  src/gameplay/<mode>/  the mode class, its systems, rules, and policies
//   edit game_mode_registry.hpp (one line), match configuration
//   do not touch               blob_simulation, blob_runtime, blob_server
//
// Two implementations of this seam: `sandbox` in Step 20 and `royale` in Step 21.
// related: match_objective.hpp -- the lifecycle half of a mode's declaration.
// related: spawn_policy.hpp -- the seating half.
// related: game_simulation_setup.hpp -- the value a mode is injected through.
class GameMode {
public:
  virtual ~GameMode() = default;
  GameMode(const GameMode&) = delete;
  GameMode& operator=(const GameMode&) = delete;

  // Stable, unique, snake_case identity. It is the `[match] mode=` key, the registry key, and the
  // name a snapshot's match section publishes.
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // The mode's ordered, staged systems. The engine appends its own MatchLifecycleSystem last at
  // kLifecycle and it is not removable.
  [[nodiscard]] virtual SystemPipeline systems() const = 0;

  // The complete contact table, in declared row order. A mode that wants the defaults returns
  // `ContactRuleTable::built_in()`; the engine never appends a row a mode did not list.
  [[nodiscard]] virtual ContactRuleTable contact_rules() const = 0;

  // The command kinds this mode accepts. Published in the protocol `welcome` and enforced at the
  // boundary and again in `InputBatch::create`.
  [[nodiscard]] virtual CommandKindMask accepted_command_kinds() const noexcept = 0;

  [[nodiscard]] virtual std::unique_ptr<const SpawnPolicy> spawn_policy() const = 0;

  [[nodiscard]] virtual std::unique_ptr<const MatchObjective> objective() const = 0;

  // Rejects a map this mode cannot play, naming the missing marker kind or spawn point. Throws
  // SimulationValidationError; returning a bool would let a caller ignore the answer.
  virtual void validate_map(const MapDefinition& map) const = 0;

protected:
  GameMode() = default;
};

} // namespace blob_royale::simulation

#endif
