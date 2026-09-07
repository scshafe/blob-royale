#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_MODE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "contact_rule_table.hpp"
#include "game_mode.hpp"
#include "map_definition.hpp"
#include "match_objective.hpp"
#include "royale/rotating_ring_spawn_policy.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_objective.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"

#include <memory>
#include <string_view>
#include <utility>

namespace blob_royale::gameplay {

// canonical: royale_mode -- thrust and drag inside a shrinking safe zone, last blob standing.
// @extension-point game_mode
//
// The second of the two games in `blob_gameplay`, and **a mode is still a declaration, not
// machinery**: everything below is seven answers. The rules are four declared systems and two
// components; none of them is a phase inside `GameSimulation` and none of them is a field on the
// world (`docs/architecture/0005-royale-mode.md` § "The mode declaration").
//
//   systems()               thrust_steering at kPreKernel; zone_shrink then zone_elimination at
//                           kPostKernel; placement_recorder at kLifecycle
//   contact_rules()         ContactRuleTable::built_in(), with no royale row added
//   accepted_command_kinds  spawn, despawn, thrust
//   spawn_policy()          RotatingRingSpawnPolicy
//   objective()             RoyaleObjective
//   validate_map()          at least `lobby_minimum_players` spawn markers, and an arena whose
//                           `R_full` is strictly greater than the configured zone minimum
//
// **Why `contact_rules()` is the built-in table verbatim.** Royale changes no collision equation.
// Blob meets blob is the accepted equal-mass exchange and blob meets wall or static body is the
// accepted reflection. That one line is why every accepted pair and wall fixture stays valid
// without regeneration: the mode is structurally incapable of reaching those equations.
//
// The order of the two `kPostKernel` systems is load-bearing and comes from this declared list
// alone: elimination reads the radius this tick's `zone_shrink` wrote. The engine appends its own
// `MatchLifecycleSystem` last at `kLifecycle` and it is not removable, so `placement_recorder`
// always runs before this tick's phase transition is evaluated.
//
// The mode holds its validated `[royale]` configuration and hands it to the systems and policies it
// builds, which is the only way configuration reaches a tick. Every declaration returns an
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

  // The registry's factory shape today: the mode's own proposed balance values. Plan Step 25 hands
  // a parsed `[royale]` section to the two-argument form instead, which changes this registry row
  // and no rule in this file.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(RoyaleConfiguration configuration);

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
    return std::make_unique<const RotatingRingSpawnPolicy>();
  }

  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const RoyaleObjective>(configuration_);
  }

  // Rejects a map royale cannot play, at startup, naming the map and the cause. Fewer spawn markers
  // than `lobby_minimum_players` can never satisfy `can_start` and would hold every match in
  // `lobby` forever; an arena whose `R_full` is not strictly greater than the configured zone
  // minimum starts already shrunk to its floor, so the zone would never contract and the game would
  // never end (`docs/architecture/0005-royale-mode.md` § "Mode configuration").
  void validate_map(const simulation::MapDefinition& map) const override;

  // Public because `create` hands the mode over as a `std::unique_ptr<const GameMode>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the entry point**; the
  // configuration arrives already validated by `RoyaleConfiguration::create`.
  explicit RoyaleMode(RoyaleConfiguration configuration) noexcept
      : configuration_(std::move(configuration)) {}

private:
  RoyaleConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
