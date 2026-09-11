#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_MOVEMENT_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_MOVEMENT_SYSTEM_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: hill_movement -- the first kPostKernel system of king of the hill: it writes `Hill`.
// @extension-point simulation_system
//
// Marker-tour policy writes only `Hill`, with no random draws. Its centre is `hill_center` of
// `hill_geometry.hpp` over the map's `hill` markers, evaluated at an elapsed tick count that is a
// pure function of the committed phase and the committed tick counters, exactly as `zone_shrink`
// evaluates the zone's radius:
//
//     lobby      center(0), which is the first marker
//     countdown  center(0)
//     running    center(tick_sequence - running_started_tick)
//     ended      center(phase_started_tick - running_started_tick), frozen at the final `running`
//                value
//
// The radius is the configured `hill_radius_world_units` in every phase.
// Random-roam policy also commits HillMotion. Lobby/countdown reset to the first marker with no
// schedule; running retargets from the named hill stream when due and advances before scoring;
// ended freezes the last committed values. The center may cross dangerous terrain, but outer-map
// outward axis displacement and velocity are canceled independently, without bounce or steering.
// Scheduled retargets continue during boundary rests; no finite escape time is promised.
// Direct initialization in running may create the hill/motion with an empty schedule; ended
// requires existing committed state. A foreign stream identity or unrepresentable future deadline
// raises GameplayValidationError before any retarget draws. Match lifecycle transitions occur
// later in the tick: the countdown-to-running commit still holds, then the next system pass moves.
//
// **It creates the hill entity from the tick's `EntityIdReservation` on the first tick it observes
// no entity carrying `Hill`**, and assigns the component on every tick after that. The hill entity
// owns no `PhysicsBody` and no `Controllable`, so it never enters a contact pair or kernel
// integration, is never a participant, and is never wiped. A tick whose reservation is empty cannot
// create anything at all, so that is a hard failure naming the cause rather than a match played
// with no hill and therefore no scoring, which is the rule `zone_shrink` set
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "The hill").
//
// A map with no `hill` marker is refused by the mode's `validate_map` at construction; reaching
// this system with one is a composition error and fails hard for the same reason.
// related: hill_geometry.hpp -- the tour this evaluates.
// related: hill_roaming.hpp -- the random selection and outer-boundary motion policy.
// related: hill_scoring_system.hpp -- the system that reads what this writes.
// related: ../../simulation/components/hill_component.hpp -- the value it writes.
class HillMovementSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "hill_movement";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(KingOfTheHillConfiguration configuration);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor. The configuration is held **by value**.
  explicit HillMovementSystem(KingOfTheHillConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Throws GameplayValidationError with `GAMEPLAY.KING_OF_THE_HILL_MAP_WITHOUT_HILL` when the map
  // carries no `hill` marker, and with `GAMEPLAY.KING_OF_THE_HILL_HILL_ENTITY_UNRESERVED` when the
  // hill entity is needed and the tick may create nothing.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  KingOfTheHillConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
