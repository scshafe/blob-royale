#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_KING_OF_THE_HILL_MODE_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_KING_OF_THE_HILL_MODE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "contact_rule_table.hpp"
#include "game_mode.hpp"
#include "game_mode_configuration.hpp"
#include "king_of_the_hill/hill_objective.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "map_definition.hpp"
#include "match_objective.hpp"
#include "shared/ability_configuration.hpp"
#include "shared/guarded_pair_contact_rule.hpp"
#include "shared/hazard_archetype.hpp"
#include "shared/next_free_spawn_point_policy.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

// canonical: king_of_the_hill_mode -- a hill that tours the map, scored by holding it.
// @extension-point game_mode
//
// The third game in `blob_gameplay`, and **a mode is still a declaration, not machinery**:
// everything below is eight answers (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`
// § "King of the hill").
//
//   systems()               thrust_steering then ability at kPreKernel; hill_movement,
//                           hill_scoring then status at kPostKernel; respawn, match_reset,
//                           lifetime_expiry, hazard_spawn then hill_rules_publisher at kLifecycle
//   contact_rules()         guarded_pair, then the built-in rows it makes unreachable
//   motion_triggers()       ground-bound support loss while running
//   accepted_command_kinds  royale's twelve, including shield, charge and seated movement tuning
//   spawn_policy()          NextFreeSpawnPointPolicy: the next free point, in every phase
//   objective()             HillObjective
//   validate_map()          at least one `hill` marker and at least one `spawn` marker
//
// The field is **open**: a joiner is seated in any phase, because a hill match loses nothing by
// someone arriving late -- they start on zero, like everyone did. The `kLifecycle` order is remove,
// reset, expire, add, publish: `respawn` first because it erases the bodies this tick's rules
// condemned, `match_reset` second so it acts on the roster as the tick leaves it, `lifetime_expiry`
// and `hazard_spawn` where royale has them, and `hill_rules_publisher` last because it is the sole
// writer of the mode-state block.
//
// `contact_rules()` is royale's, and for royale's reason: one `guarded_pair` row is the single live
// response path for every pair, so a defended contact, a lethal one and an ordinary one are one
// decision in one place rather than three rows whose precedence a reader reconstructs
// (`shared/guarded_pair_contact_rule.hpp`). Both of its predicates are "carries a `PhysicsBody`",
// so the three built-in rows below it are unreachable here; they stay declared because they are the
// engine's baseline for a mode declaring no rows at all.
//
// The mode holds its validated `[king_of_the_hill]` configuration and the shared `[abilities]` one
// and hands each to the systems and the objective it builds; every declaration returns an
// independently owned value.
// related: game_mode_registry.hpp -- the name that resolves to `create`.
// related: king_of_the_hill_configuration.hpp -- the balance numbers it hands out.
class KingOfTheHillMode final : public simulation::GameMode {
public:
  static constexpr std::string_view kModeName = "king_of_the_hill";

  // The registry's factory shape: reads `configuration.king_of_the_hill`, the hazard table, and the
  // shared ability tuning.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(const GameModeConfiguration& configuration);

  // The mode's own proposed balance values, no hazards, and the authored ability defaults: a hazard
  // kind exists only because a section declared one, while `[abilities]` is required of a real
  // configuration, so the mechanic is supplied rather than omitted.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(KingOfTheHillConfiguration configuration);
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(KingOfTheHillConfiguration configuration, std::vector<HazardArchetype> hazards);
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(KingOfTheHillConfiguration configuration, std::vector<HazardArchetype> hazards,
         AbilityConfiguration abilities);

  [[nodiscard]] std::string_view name() const noexcept override { return kModeName; }

  [[nodiscard]] simulation::SystemPipeline systems() const override;
  [[nodiscard]] simulation::MotionTriggerTable motion_triggers() const override;

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::with_rows_above_built_in({guarded_pair_contact_rule()});
  }

  // Royale's twelve. `shield` and `charge` are both advertised only because this mode declares the
  // one `ability` system that admits both of them, which is the wire rule: no command is offered
  // in `welcome` before its handler exists. The hill neither adds nor withholds an ability -- ADR
  // 0008's mode/state matrix enables both wherever a running body exists -- so this list stays
  // royale's list rather than becoming a per-mode capability negotiation.
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create(
        {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
         simulation::CommandKind::kThrust, simulation::CommandKind::kShield,
         simulation::CommandKind::kCharge, simulation::CommandKind::kSetMovementTuning,
         simulation::CommandKind::kSetSeatCount, simulation::CommandKind::kClearSeat,
         simulation::CommandKind::kSeatNpc, simulation::CommandKind::kStartMatch,
         simulation::CommandKind::kLeave, simulation::CommandKind::kJoin});
  }

  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const NextFreeSpawnPointPolicy>();
  }

  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const HillObjective>(configuration_);
  }

  // Rejects, naming the map and the missing kind, a map with no `hill` marker or no `spawn`
  // marker. A spawn marker per lobby seat is every lobby mode's rule and is the application's.
  void validate_map(const simulation::MapDefinition& map) const override;

  KingOfTheHillMode(KingOfTheHillConfiguration configuration, std::vector<HazardArchetype> hazards,
                    AbilityConfiguration abilities) noexcept
      : configuration_(std::move(configuration)), hazards_(std::move(hazards)),
        abilities_(abilities) {}

  // Neither delegate is `noexcept`: `AbilityConfiguration::defaults()` validates like any other
  // authored value, and a `noexcept` delegate would turn a rejected default into a terminate rather
  // than into the startup error every other configuration produces.
  KingOfTheHillMode(KingOfTheHillConfiguration configuration, std::vector<HazardArchetype> hazards)
      : KingOfTheHillMode(std::move(configuration), std::move(hazards),
                          AbilityConfiguration::defaults()) {}

  explicit KingOfTheHillMode(KingOfTheHillConfiguration configuration)
      : KingOfTheHillMode(std::move(configuration), {}) {}

private:
  KingOfTheHillConfiguration configuration_;
  std::vector<HazardArchetype> hazards_;
  AbilityConfiguration abilities_;
};

} // namespace blob_royale::gameplay

#endif
