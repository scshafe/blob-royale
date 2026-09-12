#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_RESPAWN_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_RESPAWN_SYSTEM_HPP

#include "simulation_system.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: respawn -- what an elimination means in a mode whose fallen come back.
// @extension-point simulation_system
//
// Royale's answer to this tick's `EliminationEvent`s is `placement_recorder`: rank and destroy.
// This is the other answer, and `elimination_event.hpp` names it as one the kind was written to
// admit -- "a respawn timer". Per tick, at `kLifecycle`, in this order:
//
//     for every entity carrying a RespawnTimer, ascending EntityId:
//         ticks_remaining -= 1; at zero the timer is erased and the entity awaits a body
//     for every distinct eliminated entity, ascending, that carries a Controllable and a
//     PhysicsBody:
//         erase the PhysicsBody
//         if respawn_delay_ticks > 0: attach RespawnTimer { respawn_delay_ticks }
//     erase every body-bound component whose entity has no PhysicsBody
//
// **The written order is the contract**: an entity eliminated this tick is not decremented this
// tick. An entity eliminated on tick `N` with delay `D` therefore awaits a body from tick `N + D`
// and is offered to the mode's spawn policy at phase 0 of tick `N + D + 1`; with `D = 0` it is
// offered on `N + 1`. A mode's policy must defer an entity that still carries a timer
// (`shared/next_free_spawn_point_policy.hpp` does), which is one predicate.
//
// **Persistent entity state survives.** Score, race progress, and the controller link stay on the
// entity, which is the whole point of not destroying it: a hill match remembers what the fallen
// had scored and a race remembers which gate they had reached. A mode that pairs this with a
// body-bound counter declares ComponentLifetime beside that kind. One registry-generated sweep
// removes bound state after body erasure, including stale state on previously bodyless entities;
// this system knows no individual bound kind. Royale does not declare it and keeps destroying the
// eliminated: attrition is its game.
//
// The guard on the second loop is what makes the rule total. An elimination naming an entity with
// no body -- one already out of play, or a producer naming something that was never playing -- is
// nothing to erase and starts no timer, so a second producer in the same tick cannot double a
// delay, and an entity is named at most once whatever the producers emitted, because the set is
// sorted and de-duplicated the way `placement_recorder` sorts its own.
//
// It runs at `kLifecycle` because a body leaving the field is roster bookkeeping, which is what
// that stage is for and where the elimination's producers -- `zone_elimination`, the guarded
// composition's lethal branch (which still reports the `lethal_hazard` diagnostic name), and the
// shared support-loss trigger -- have all already run
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Where the framework has to
// move").
// related: ../../simulation/components/respawn_timer_component.hpp -- the counter this owns.
// related: ../../simulation/events/elimination_event.hpp -- the event this consumes.
// related: ../../simulation/component_lifetime.hpp -- the per-kind lifetime declaration.
// related: ../royale/placement_recorder_system.hpp -- the other consumer, for attrition.
class RespawnSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "respawn";

  // Takes the tick count rather than a whole configuration: a system captures only the immutable
  // configuration it needs. Zero is legal and means the eliminated
  // are offered a seat on the very next tick.
  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(std::uint64_t respawn_delay_ticks);

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Never throws: every write is to a store that admits it, and an event naming nothing it can act
  // on is a no-op by the rule above.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

  explicit RespawnSystem(std::uint64_t respawn_delay_ticks) noexcept
      : respawn_delay_ticks_(respawn_delay_ticks) {}

private:
  std::uint64_t respawn_delay_ticks_;
};

} // namespace blob_royale::gameplay

#endif
