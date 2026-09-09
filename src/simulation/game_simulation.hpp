#ifndef BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP
#define BLOB_ROYALE_SIMULATION_GAME_SIMULATION_HPP

#include "command_kind_mask.hpp"
#include "contact_rule_table.hpp"
#include "fixed_delta.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "spatial_grid.hpp"
#include "spawn_system.hpp"
#include "system_pipeline.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: game_simulation -- the sole mutable owner of one deterministic world timeline.
//
// A tick is one fixed kernel with three named hook stages cut into it. The numbered phases are
// kernel mechanism -- no mode may reorder, skip, replace, or add one -- and the stages hold the
// mode's declared systems in the order the mode wrote them
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick";
// `docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"):
//
//   phase 0            despawns, spawn creation, spawn seating (SpawnPolicy), lobby commands,
//                      leaves, command recording
//   ---- kPreKernel -- the mode's systems, declared order
//   phase 1            stored acceleration, then drag
//   phase 2            canonical candidate pairs
//   phase 3            contact resolution through the ContactRuleTable
//   phase 4            world bounds
//   phase 5            position integration
//   phase 6            spatial reindex
//   ---- kPostKernel - the mode's systems, declared order
//   ---- kLifecycle -- the mode's systems, declared order, then MatchLifecycleSystem
//   phase 10           apply DespawnEvent removals, validate, reindex survivors, clear tick-local
//                      state, publish
//
// A `GameWorld&` exists only inside `step`, so ADR 0002's single-writer property is unchanged:
// systems receive the mutable reference the tick already holds and nothing outside a tick can
// obtain one.
//
// The kernel has exactly two policy sockets, both evaluated at a fixed point against declared
// data: the mode's SpawnPolicy in phase 0 and its ContactRuleTable in phase 3. There is no third;
// a mode that wants to change anything else changes it with a system at a stage.
//
// The map is the arena source. Phase 4's fold, the commit-time bounds validation, and the spatial
// index all read `MapDefinition::bounds()`; SimulationConfig's world scalars stay the published
// geometry protocol v1 serves and are no longer read by any phase.
// related: game_simulation_setup.hpp -- the one value every declaration arrives through.
// related: system_pipeline.hpp -- the ordered, staged systems this kernel runs.
// related: contact_rule_table.hpp -- the ordered chain phase 3 walks per admitted contact.
// related: spawn_system.hpp -- the phase 0 mechanism that seats entities awaiting a body.
// related: input_batch.hpp -- the one validated input value a tick reads.
class GameSimulation final {
public:
  // The mode name a simulation constructed with no declared mode publishes. The engine's own
  // declarations -- a spawn policy that never seats and an objective that never starts a match --
  // are mechanism rather than a game, so this is a reserved name and not a mode in
  // `game_mode_registry.hpp`.
  static constexpr std::string_view kEngineDefaultModeName = "idle";

  // The one factory. Validates the complete loaded state, reads every declaration the setup names
  // exactly once, builds the initial spatial index, and commits tick zero.
  //
  // The default setup is the engine's own declarations, so `create(configuration, world)` is the
  // accepted seven-phase baseline: with no declared system, zero drag, an empty batch, and a map
  // with no static bodies, the staged kernel commits exactly the bodies the baseline committed at
  // the same tick horizons. The production shape is
  // `create(configuration, world, GameSimulationSetup::of_mode(map, std::move(mode)))`.
  //
  // Throws SimulationValidationError when the setup declares a mode together with an explicit
  // system pipeline or contact table, when the mode rejects the map, and when the map's spawn
  // points cannot seat a disc of the configured radius.
  [[nodiscard]] static GameSimulation
  create(SimulationConfig configuration, GameWorld initial_world,
         GameSimulationSetup setup = GameSimulationSetup::engine_defaults());

  GameSimulation(const GameSimulation&) = delete;
  GameSimulation(GameSimulation&&) noexcept = default;
  GameSimulation& operator=(const GameSimulation&) = delete;
  GameSimulation& operator=(GameSimulation&&) noexcept = default;
  ~GameSimulation() = default;

  [[nodiscard]] const SimulationConfig& configuration() const& noexcept { return configuration_; }
  [[nodiscard]] const SimulationConfig& configuration() const&& = delete;
  [[nodiscard]] const MapDefinition& map() const& noexcept { return map_; }
  [[nodiscard]] const MapDefinition& map() const&& = delete;
  [[nodiscard]] const ContactRuleTable& contact_rules() const& noexcept { return contact_rules_; }
  [[nodiscard]] const ContactRuleTable& contact_rules() const&& = delete;
  [[nodiscard]] TickSequence tick_sequence() const noexcept { return tick_sequence_; }

  // The running mode's declared name, read once at construction. `idle` when no mode was declared.
  [[nodiscard]] std::string_view mode_name() const& noexcept { return mode_name_; }
  [[nodiscard]] std::string_view mode_name() const&& = delete;

  // The command kinds the running mode accepts, read once at construction. This is what the
  // runtime hands `InputBatch::create` and what the protocol `welcome` publishes.
  [[nodiscard]] CommandKindMask accepted_command_kinds() const noexcept {
    return accepted_command_kinds_;
  }

  // Advances exactly one canonical fixed step against exactly one input value. Every phase and
  // every stage is evaluated against transaction-local values; a failure leaves the previously
  // committed world, grid, and sequence unchanged. A tick with no commands is this same call with
  // `InputBatch::empty()`, not a different code path.
  void step(FixedDelta fixed_delta, const InputBatch& input_batch);

  // Copies one complete committed state. The returned value never aliases the mutable world.
  [[nodiscard]] WorldSnapshot snapshot() const;

private:
  GameSimulation(SimulationConfig configuration, MapDefinition map, GameWorld world,
                 SpatialGrid grid, SystemPipeline system_pipeline, ContactRuleTable contact_rules,
                 SpawnSystem spawn_system, std::string mode_name,
                 CommandKindMask accepted_command_kinds, TickSequence tick_sequence) noexcept;

  SimulationConfig configuration_;
  MapDefinition map_;
  GameWorld world_;
  SpatialGrid grid_;
  // Holds the mode's declared systems with the engine's MatchLifecycleSystem appended last at
  // kLifecycle, so the kernel runs one list and the engine's own system is not a special case.
  SystemPipeline system_pipeline_;
  ContactRuleTable contact_rules_;
  SpawnSystem spawn_system_;
  std::string mode_name_;
  CommandKindMask accepted_command_kinds_;
  TickSequence tick_sequence_;
};

} // namespace blob_royale::simulation

#endif
