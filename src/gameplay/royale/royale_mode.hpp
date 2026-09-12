#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "contact_rule_table.hpp"
#include "game_mode.hpp"
#include "game_mode_configuration.hpp"
#include "map_definition.hpp"
#include "match_objective.hpp"
#include "royale/rotating_ring_spawn_policy.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_objective.hpp"
#include "shared/hazard_archetype.hpp"
#include "shared/lethal_hazard_contact_rule.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

// canonical: royale_mode -- thrust and drag inside a shrinking safe zone, last blob standing.
// @extension-point game_mode
//
// The second of the two games in `blob_gameplay`, and **a mode is still a declaration, not
// machinery**: everything below is eight answers. The rules are declared systems, a contact row and
// two components; none of them is a phase inside `GameSimulation` and none of them is a field on
// the world (`docs/architecture/0005-royale-mode.md` § "The mode declaration").
//
//   systems()               thrust_steering at kPreKernel; zone_shrink then zone_elimination at
//                           kPostKernel; placement_recorder, match_reset, lifetime_expiry,
//                           hazard_spawn then elimination_grace_publisher at kLifecycle
//   contact_rules()         lethal_hazard, then the built-in rows
//   motion_triggers()       ground-bound support loss while running
//   accepted_command_kinds  spawn, despawn, join, leave, thrust, movement tuning, four lobby kinds
//   spawn_policy()          RotatingRingSpawnPolicy
//   objective()             RoyaleObjective
//   validate_map()          an arena whose `R_full` is strictly greater than the configured
//                           zone minimum
//
// **Why `contact_rules()` declares one row above the built-in ones.** Royale still changes no
// collision *equation*: `lethal_hazard` computes no physics at all, returns both bodies verbatim,
// and its whole effect is one `EliminationEvent`. What it changes is which rule a pair reaches, and
// only for a pair the built-in rows were never written for.
//
// The declared order is `lethal_hazard`, then `variable_impulse`, then `elastic_disc`, then
// `reflect_static`, and each boundary earns its place. `lethal_hazard` is above the impulse rows
// because a hazard is a dynamic body with a non-baseline mass, so `variable_impulse` matches the
// same pair; declared second, `lethal_hazard` would never fire and a comet would shove a player
// aside instead of killing them. The three below it are `ContactRuleTable::built_in()`'s own rows
// in its own order, taken by calling it rather than by transcribing it, which is what
// `with_rows_above_built_in` exists for -- a second copy of the accepted baseline's predicates in
// this file could drift from the real ones without a test noticing.
//
// **Every accepted pair and wall fixture still passes untouched**, and the reason is unchanged in
// substance: `lethal_hazard`'s first predicate is `LethalOnContact` presence, and no ordinary blob,
// wall, or zone carries that kind. A world with no hazards in it never reaches the new row, exactly
// as a world of baseline blobs never reaches `variable_impulse`. The mode is still structurally
// incapable of reaching a different equation for a pair of ordinary blobs.
//
// The order of the two `kPostKernel` systems is load-bearing and comes from this declared list
// alone: elimination reads the radius this tick's `zone_shrink` wrote. The engine appends its own
// `MatchLifecycleSystem` last at `kLifecycle` and it is not removable, so `placement_recorder`
// always runs before this tick's phase transition is evaluated.
//
// The five `kLifecycle` systems are ordered for the same kind of reason, remove, reset, add, then
// publish. `placement_recorder` runs first because it *destroys* this tick's eliminated entities,
// `match_reset` second because the restart wipe is every ending mode's rule and not royale's
// (`shared/match_reset_system.hpp`),
// `lifetime_expiry` despawns whatever ran out, and `hazard_spawn` runs third so it sees the seats
// that freed and the entity ids that did not; all three create or destroy roster entries, which is
// what `kLifecycle` is for. `elimination_grace_publisher` runs last because it writes the
// mode-state block and running after the other writer of that block is what makes the published
// grace independent of how that writer happens to be implemented
// (`royale/elimination_grace_publisher_system.hpp`).
//
// The mode holds its validated `[royale]` configuration and hands it to the systems and policies it
// builds. Shared movement tuning instead lives on MatchState. Every declaration returns an
// independently owned value -- each system and the objective hold a *copy* of the configuration --
// so nothing a tick holds points back at the mode the engine destroys at construction.
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
// related: royale/royale_configuration.hpp -- the balance numbers it hands out.
class RoyaleMode final : public simulation::GameMode {
public:
  static constexpr std::string_view kModeName = "royale";

  // The registry's factory shape: royale reads `configuration.royale`, which the application
  // parsed from the `[royale]` INI section, and reads nothing else from it.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(const GameModeConfiguration& configuration);

  // The mode's own proposed balance values, for a test or a diagnostic that does not configure it.
  // Both overloads field **no hazards**, which is the honest default: a hazard kind exists only
  // because a `[hazard.<kind>]` section declared one, so a mode built without a configuration file
  // has none to declare.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(RoyaleConfiguration configuration);
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(RoyaleConfiguration configuration, std::vector<HazardArchetype> hazards);

  [[nodiscard]] std::string_view name() const noexcept override { return kModeName; }

  [[nodiscard]] simulation::SystemPipeline systems() const override;
  [[nodiscard]] simulation::MotionTriggerTable motion_triggers() const override;

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    // One royale row, above everything the engine ships. See the note on precedence above.
    return simulation::ContactRuleTable::with_rows_above_built_in({lethal_hazard_contact_rule()});
  }

  // Seven kinds: the three every mode needs, and the four that operate the pre-match lobby.
  //
  // **The four lobby kinds are declared by the mode rather than by the engine**, even though the
  // roster they write is engine state, because the mask is what a mode uses to say which decisions
  // its game admits and `sandbox` genuinely admits none of them -- free play has no match to start,
  // so a `start_match` there would be a capability advertised to a client that could never use it
  // (`src/gameplay/sandbox/sandbox_mode.hpp`). A mode that wanted a lobby it could not operate
  // would simply omit them, and the `welcome` frame would tell every client so.
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create(
        {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
         simulation::CommandKind::kThrust, simulation::CommandKind::kSetMovementTuning,
         simulation::CommandKind::kSetSeatCount, simulation::CommandKind::kClearSeat,
         simulation::CommandKind::kSeatNpc, simulation::CommandKind::kStartMatch,
         simulation::CommandKind::kLeave, simulation::CommandKind::kJoin});
  }

  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const RotatingRingSpawnPolicy>();
  }

  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const RoyaleObjective>(configuration_);
  }

  // Rejects a map royale cannot play, at startup, naming the map and the cause: an arena whose
  // `R_full` is not strictly greater than the configured zone minimum starts already shrunk to its
  // floor, so the zone would never contract and the game would never end
  // (`docs/architecture/0005-royale-mode.md` § "Mode configuration"). A spawn marker per lobby seat
  // is every lobby mode's rule and is the application's (`match_startup_validation.hpp`).
  void validate_map(const simulation::MapDefinition& map) const override;

  // Public because `create` hands the mode over as a `std::unique_ptr<const GameMode>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the entry point**; the
  // configuration arrives already validated by `RoyaleConfiguration::create`, and the hazard table
  // by `HazardArchetype::create`.
  RoyaleMode(RoyaleConfiguration configuration, std::vector<HazardArchetype> hazards) noexcept
      : configuration_(std::move(configuration)), hazards_(std::move(hazards)) {}

  // Royale with no hazards, which is what the mode was before hazards existed and what a test or a
  // diagnostic about anything else wants. It delegates rather than repeating the member list, so
  // there is one place a royale mode is assembled.
  explicit RoyaleMode(RoyaleConfiguration configuration) noexcept
      : RoyaleMode(std::move(configuration), {}) {}

private:
  RoyaleConfiguration configuration_;
  // Copied rather than referenced, for the reason stated above: every declaration returns an
  // independently owned value, so the system built from this table outlives the mode the engine
  // destroys at construction.
  std::vector<HazardArchetype> hazards_;
};

} // namespace blob_royale::gameplay

#endif
