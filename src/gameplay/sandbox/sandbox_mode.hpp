#ifndef BLOB_ROYALE_GAMEPLAY_SANDBOX_SANDBOX_MODE_HPP
#define BLOB_ROYALE_GAMEPLAY_SANDBOX_SANDBOX_MODE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "contact_rule_table.hpp"
#include "game_mode.hpp"
#include "game_mode_configuration.hpp"
#include "map_definition.hpp"
#include "match_objective.hpp"
#include "sandbox/free_play_objective.hpp"
#include "shared/next_free_spawn_point_policy.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: sandbox_mode -- free play: thrust, bump, and nothing ever ends.
// @extension-point game_mode
//
// Free play automatically advances through countdown into running and never ends. Shared
// steering/status, support-loss triggers and configured shared respawn stay active in every phase;
// seating uses the next supported free marker.
// Contact responses are the engine's built-ins. No Sandbox-specific component or mode-state block.
// Every declaration returns an independently owned value, so nothing a tick holds points
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

  // Binds the explicit [sandbox] return delay. Shared movement is seeded on the world by the
  // caller.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(const GameModeConfiguration& configuration);

  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();

  [[nodiscard]] std::string_view name() const noexcept override { return kModeName; }

  [[nodiscard]] simulation::SystemPipeline systems() const override;
  [[nodiscard]] simulation::MotionTriggerTable motion_triggers() const override;

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::built_in();
  }

  // The four kinds free play has any use for. Lobby/tuning kinds are deliberately absent:
  // FreePlayObjective starts automatically without a seated roster or a start_match command.
  // The seat roster remains inert (`src/simulation/seat_roster.hpp`).
  // Movement tuning requires seated authority, so sandbox
  // does not advertise it or invent an unseated exception; authored tuning still drives steering.
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create(
        {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
         simulation::CommandKind::kLeave, simulation::CommandKind::kThrust});
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
  // `std::make_unique` needs an accessible constructor. The declaration has no mutable state.
  explicit SandboxMode(SandboxConfiguration configuration = SandboxConfiguration::defaults())
      : configuration_(configuration) {}

private:
  SandboxConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
