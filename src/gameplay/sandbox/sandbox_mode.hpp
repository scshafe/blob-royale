#ifndef BLOB_ROYALE_GAMEPLAY_SANDBOX_SANDBOX_MODE_HPP
#define BLOB_ROYALE_GAMEPLAY_SANDBOX_SANDBOX_MODE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "contact_rule_table.hpp"
#include "game_mode.hpp"
#include "map_definition.hpp"
#include "match_objective.hpp"
#include "sandbox/free_play_objective.hpp"
#include "sandbox/next_free_spawn_point_policy.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: sandbox_mode -- free play: thrust, bump, and nothing ever ends.
// @extension-point game_mode
//
// The first of the two games in `blob_gameplay`, and **a mode is a declaration, not machinery**:
// everything below is seven answers and one declared system. Steering is `thrust_steering`, which
// `royale` declares too and which therefore lives in `src/gameplay/shared/`; seating is "the next
// free point, in every phase"; the lifecycle is "always startable, never decided"; interactions are
// the engine's own two contact rows. Sandbox contributes no component kind, no contact rule, no
// world event, no mode-state block, and no `kPostKernel` or `kLifecycle` system at all.
//
// The one balance number it owns is the thrust maximum, held here and handed to the system it
// builds, which is the only way configuration reaches a tick: a system's members are immutable
// configuration and `TickContext` is mode-agnostic
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"). Every declaration returns an independently owned value, so nothing a tick holds points
// back at the mode the engine destroys at construction.
//
// Adding a game:
//
//   new  src/gameplay/<mode>/                the mode class, its systems, rules, and policies
//   edit src/gameplay/game_mode_registry.hpp one row in kGameModeRegistrations
//   edit match configuration                 `[match] mode=`
//   do not touch                             blob_simulation, blob_runtime, blob_server, or any
//                                            other mode
//
// related: game_mode_registry.hpp -- the name that resolves to `create`.
// related: shared/thrust_steering_system.hpp -- the one system this declares.
class SandboxMode final : public simulation::GameMode {
public:
  static constexpr std::string_view kModeName = "sandbox";

  // The proposed balance value of `docs/architecture/0005-royale-mode.md` § "Mode configuration",
  // shared so free play and royale accelerate identically until a playtest says otherwise.
  static constexpr double kDefaultThrustMaximumWorldUnitsPerSecondSquared = 400.0;

  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(double thrust_maximum_world_units_per_second_squared);

  [[nodiscard]] std::string_view name() const noexcept override { return kModeName; }

  [[nodiscard]] simulation::SystemPipeline systems() const override;

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::built_in();
  }

  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create({simulation::CommandKind::kSpawn,
                                                simulation::CommandKind::kDespawn,
                                                simulation::CommandKind::kThrust});
  }

  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const NextFreeSpawnPointPolicy>();
  }

  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const FreePlayObjective>();
  }

  // Free play needs somewhere to put a joiner, so a map with no `spawn` marker is rejected at
  // startup naming the map rather than silently deferring every spawn command forever.
  void validate_map(const simulation::MapDefinition& map) const override;

  // Public because `create` hands the mode over as a `std::unique_ptr<const GameMode>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the validating entry point**;
  // this one takes the number as given.
  explicit SandboxMode(double thrust_maximum_world_units_per_second_squared) noexcept;

private:
  double thrust_maximum_;
};

} // namespace blob_royale::gameplay

#endif
