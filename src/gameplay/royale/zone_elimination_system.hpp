#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ZONE_ELIMINATION_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ZONE_ELIMINATION_SYSTEM_HPP

#include "royale/royale_configuration.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: zone_elimination -- the second kPostKernel system: grace, and who is out.
// @extension-point simulation_system
//
// It **evaluates only when the committed phase is `running`**, reads `PhysicsBody::position` and
// the `Zone` this tick's `zone_shrink` wrote, writes `ZoneExposure`, and emits `EliminationEvent`s.
// It removes nothing from the roster: a `kPostKernel` system writes consequences, and roster
// bookkeeping is `kLifecycle` work (`docs/architecture/0004-gameplay-architecture.md` § "The tick:
// one fixed kernel, three named stages").
//
// It reads the player's **centre, not its disc**, so a player may overlap the boundary and remain
// safe:
//
//     distance = sqrt((p.x - zone_center.x)^2 + (p.y - zone_center.y)^2)
//     outside  = distance > radius + kPositionTolerance
//
// A centre exactly on the boundary is inside, consistent with the baseline's inclusive contact
// rule.
//
// Per tick, for every alive entity in ascending `EntityId` order, with `G =
// elimination_grace_ticks`: an entity that is inside has its `ZoneExposure` erased, which is how
// "sets `outside_ticks = 0`" is spelled when an absent counter reads as zero; an entity that is
// outside increments its counter and, when the counter reaches `G`, is named in one
// `EliminationEvent`. **The increment precedes the test**, so `G = 0` eliminates on the first tick
// a centre is outside and `G = 1,200` eliminates on the 1,200th consecutive outside tick. An entity
// that is inside is never tested, so `G = 0` does not eliminate a safe player
// (`docs/architecture/0005-royale-mode.md` § "Elimination and placement").
//
// Every counter is zero when `running` is entered and no rule has to reset one: the zone covers the
// whole arena during `lobby` and `countdown`, an eliminated entity's counter dies with the entity,
// and every survivor of a finished match is destroyed on the first `lobby` tick.
// related: royale/zone_shrink_system.hpp -- the system that writes the radius this tests against.
// related: royale/placement_recorder_system.hpp -- the kLifecycle consumer of what this emits.
class ZoneEliminationSystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by.
  static constexpr std::string_view kSystemName = "zone_elimination";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RoyaleConfiguration configuration);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor.
  explicit ZoneEliminationSystem(RoyaleConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Throws GameplayValidationError with `GAMEPLAY.ROYALE_ZONE_ABSENT` when the world carries no
  // `Zone` while `running`, which means this system was declared without `zone_shrink` ahead of it:
  // eliminating nobody because there is nothing to test against would be a match silently played by
  // different rules.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  RoyaleConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
