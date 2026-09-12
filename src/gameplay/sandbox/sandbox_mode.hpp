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
#include "shared/ability_configuration.hpp"
#include "shared/guarded_pair_contact_rule.hpp"
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
// steering/status/ability, support-loss triggers and configured shared respawn stay active in every
// phase; seating uses the next supported free marker.
//
// **Contact responses are the shared `guarded_pair` row, not the engine's built-ins any more.**
// Sandbox used to return `ContactRuleTable::built_in()` verbatim, and that was the honest
// declaration while free play had no ability to defend with. ADR 0008's mode/state matrix answers
// "Sandbox, running with a body" with *Enabled*, so a blob in free play may raise a shield, and a
// shield that only worked in the three competitive modes would make the one mode people experiment
// in the one mode where the mechanic is a lie. Declaring the row makes all three built-in rows
// unreachable here exactly as it does elsewhere; an unguarded pair still resolves to the same
// accepted equation bits, and what actually changes is the diagnostic name and the dynamic/static
// case, which now reflects only while the contact is closing
// (`shared/guarded_pair_contact_rule.hpp`).
//
// No Sandbox-specific component or mode-state block. Every declaration returns an independently
// owned value, so nothing a tick holds points back at the mode the engine destroys at
// construction.
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
// related: shared/thrust_steering_system.hpp -- the first of the four systems this declares.
// related: shared/guarded_pair_contact_rule.hpp -- the row that replaced the built-ins here.
class SandboxMode final : public simulation::GameMode {
public:
  static constexpr std::string_view kModeName = "sandbox";

  // Binds the explicit [sandbox] return delay and the shared [abilities] tuning. Shared movement is
  // seeded on the world by the caller.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(const GameModeConfiguration& configuration);

  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();

  [[nodiscard]] std::string_view name() const noexcept override { return kModeName; }

  [[nodiscard]] simulation::SystemPipeline systems() const override;
  [[nodiscard]] simulation::MotionTriggerTable motion_triggers() const override;

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::with_rows_above_built_in({guarded_pair_contact_rule()});
  }

  // The five kinds free play has any use for. Lobby/tuning kinds are deliberately absent:
  // FreePlayObjective starts automatically without a seated roster or a start_match command.
  // The seat roster remains inert (`src/simulation/seat_roster.hpp`).
  // Movement tuning requires seated authority, so sandbox
  // does not advertise it or invent an unseated exception; authored tuning still drives steering.
  // `shield` is here because free play declares the `ability` system that admits it, which is what
  // makes advertising it honest rather than a capability with no handler behind it.
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create(
        {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
         simulation::CommandKind::kLeave, simulation::CommandKind::kThrust,
         simulation::CommandKind::kShield});
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
  // Both parameters keep sandbox's defaulted-argument style rather than growing an overload chain:
  // free play has exactly one section of its own, and the ability tuning defaults to the same
  // authored values a configuration file would supply.
  explicit SandboxMode(SandboxConfiguration configuration = SandboxConfiguration::defaults(),
                       AbilityConfiguration abilities = AbilityConfiguration::defaults())
      : configuration_(configuration), abilities_(abilities) {}

private:
  SandboxConfiguration configuration_;
  AbilityConfiguration abilities_;
};

} // namespace blob_royale::gameplay

#endif
