#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_MODE_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_MODE_HPP

#include "command_registry.hpp"
#include "game_mode.hpp"
#include "game_mode_configuration.hpp"
#include "race/grid_spawn_policy.hpp"
#include "race/race_configuration.hpp"
#include "race/race_course.hpp"
#include "race/race_objective.hpp"
#include "shared/hazard_archetype.hpp"
#include "shared/lethal_hazard_contact_rule.hpp"

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

// canonical: race_mode -- a closed field on an ordered point-to-point course.
// @extension-point game_mode
//
// The fourth mode supplies the same seven declarations. The map-bearing declaration,
// validate_map, binds RaceCourse once during GameSimulation construction, before systems() is
// called. Every system receives its own immutable value; destruction of the mode cannot affect
// a tick. No course is built, cached, or looked up during simulation.
//
// kPreKernel: thrust_steering. kPostKernel: checkpoint_progress, track_bounds.
// kLifecycle: standings_recorder, checkpoint_respawn, respawn, match_reset, lifetime_expiry,
// hazard_spawn, course_publisher. Record before returning; return before timer expiry; publish
// after every other mode system. The engine appends its lifecycle transition last.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md section "The mode
// declaration".
class RaceMode final : public simulation::GameMode {
public:
  static constexpr std::string_view kModeName = "race";

  // Registry factory: consumes only configuration.race and the shared hazard table.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(const GameModeConfiguration& configuration);
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create();
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(RaceConfiguration configuration);
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(RaceConfiguration configuration, std::vector<HazardArchetype> hazards);

  [[nodiscard]] std::string_view name() const noexcept override { return kModeName; }

  // Requires successful validate_map first; otherwise throws GAMEPLAY.RACE_COURSE_UNBOUND.
  [[nodiscard]] simulation::SystemPipeline systems() const override;

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::with_rows_above_built_in({lethal_hazard_contact_rule()});
  }
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create(
        {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
         simulation::CommandKind::kThrust, simulation::CommandKind::kSetMovementTuning,
         simulation::CommandKind::kSetSeatCount, simulation::CommandKind::kClearSeat,
         simulation::CommandKind::kSeatNpc, simulation::CommandKind::kStartMatch,
         simulation::CommandKind::kLeave, simulation::CommandKind::kJoin});
  }
  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const GridSpawnPolicy>();
  }
  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const RaceObjective>(configuration_);
  }

  // Builds and validates the course, propagating RaceCourse's six GAMEPLAY.RACE_MAP_* errors.
  // Failed validation clears any earlier binding, so later systems() cannot use a stale map.
  void validate_map(const simulation::MapDefinition& map) const override;

  RaceMode(RaceConfiguration configuration, std::vector<HazardArchetype> hazards) noexcept
      : configuration_(std::move(configuration)), hazards_(std::move(hazards)) {}
  explicit RaceMode(RaceConfiguration configuration) noexcept
      : RaceMode(std::move(configuration), {}) {}

private:
  const RaceConfiguration configuration_;
  const std::vector<HazardArchetype> hazards_;
  // Setup-only binding through GameMode's existing const declaration interface. The engine
  // validates before reading systems and destroys this object before the first tick.
  mutable std::optional<RaceCourse> course_;
};

} // namespace blob_royale::gameplay

#endif
