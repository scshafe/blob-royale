#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_SCORING_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_SCORING_SYSTEM_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: hill_scoring -- the second kPostKernel system of king of the hill: presence, and
// points.
// @extension-point simulation_system
//
// It **evaluates only when the committed phase is `running`**, reads `PhysicsBody::position` and
// the `Hill` this tick's `hill_movement` wrote, and writes `HillPresence` and `Score`. A centre is
// inside when it is not outside by `shared/disc_geometry`'s predicate, so a centre exactly on the
// rim is inside. Per tick, with `I` the configured `point_interval_ticks`
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Scoring"):
//
//     inside_count = number of alive entities whose centre is inside
//     for every alive entity, ascending EntityId:
//         if not inside:                                        erase HillPresence
//         else if inside_count > 1 and not contested_hill_scores:  leave HillPresence as it is
//         else:
//             presence = (HillPresence or 0) + 1
//             if presence >= I:   Score += 1, erase HillPresence
//             else:               HillPresence = presence
//     for every entity eliminated this tick:  erase HillPresence
//     for every entity carrying HillPresence and no PhysicsBody, ascending:  erase HillPresence
//
// **Leaving the hill loses the partial point; a contested hill freezes it; being knocked out
// forgets it.** This mode owns its counter's hygiene both before lifecycle body removal and while
// bodyless, so even a zero-delay return forgets progress. Completed points stand on the knockout
// tick. `I = 0` awards a point on the first inside tick, because
// the increment precedes the test. `Score` sits on the player entity, absent reads as zero, and it
// survives a respawn because the entity does.
//
// Throws GameplayValidationError with `GAMEPLAY.KING_OF_THE_HILL_HILL_ABSENT` when the world
// carries no `Hill` while `running`, which means this system was declared without `hill_movement`
// ahead of it: scoring nobody because there is nothing to hold would be a match silently played by
// different rules, which is the rule `zone_elimination` set.
// related: hill_movement_system.hpp -- the system that writes the circle this tests against.
// related: ../../simulation/components/hill_presence_component.hpp -- the counter this owns.
class HillScoringSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "hill_scoring";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(KingOfTheHillConfiguration configuration);

  explicit HillScoringSystem(KingOfTheHillConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  KingOfTheHillConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
