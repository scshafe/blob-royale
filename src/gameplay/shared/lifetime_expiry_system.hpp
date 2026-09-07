#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_LIFETIME_EXPIRY_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_LIFETIME_EXPIRY_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: lifetime_expiry -- the one system that makes `Lifetime` mean anything.
// @extension-point simulation_system
//
// It decrements every `Lifetime::ticks_remaining` by one and emits a `DespawnEvent` for each entity
// whose count runs out, which phase 10's `apply_despawn_events` then applies.
//
// **`Lifetime` was a registered, wire-published component that nothing read.** The kind, its
// encoder, and its schema all shipped, and no system in `src/` ever decremented the counter or
// removed an expired entity: the only writers in the tree were tests asserting values they had
// written themselves. An entity given a lifetime therefore lived forever, silently, which is the
// worst shape a bug can take -- the declaration looks satisfied at every call site. This system is
// what turns the component from a promise into a mechanic, and it is a prerequisite for anything
// that spawns on a timer, because such a spawner without it fills the world's 4,096 seats and
// stops the match with a component-store overflow.
//
// **It runs at `kLifecycle`.** An entity leaving the world is roster bookkeeping, which is what
// that stage is for and the same reason `placement_recorder` sits there
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"; `simulation/events/elimination_event.hpp`). The two stages below it are the wrong
// answers for reasons worth stating: at `kPreKernel` an entity would be removed *before* the
// kernel moved it, so its last tick of motion would never be simulated and a hazard would vanish a
// tick early; at `kPostKernel` it would race the mode's own rules -- royale's `zone_elimination`
// runs there and would be reading positions of entities this system had already condemned. Phase 10
// applies `DespawnEvent` after every stage regardless, so `kLifecycle` is also the last stage at
// which emitting one still costs nothing extra.
//
// **Adding it changes no existing behavior**, because no entity in the tree carries `Lifetime`
// today: no spawn path, no map loader, and no mode attaches one. A mode that declares this system
// and seats no lifetime-bearing entity runs an empty walk, and every accepted fixture and the
// baseline oracle are untouched for exactly that reason.
//
// It is in `shared/` rather than `royale/` for the reason `thrust_steering_system.hpp` gives: a
// self-expiring entity is a mechanic any mode may field. It is a declared system rather than an
// engine-appended one because the engine appends only `MatchLifecycleSystem`, and widening that
// list would make every mode pay for a component it may never use.
// related: ../../simulation/components/lifetime_component.hpp -- the counter this reads.
// related: ../../simulation/events/despawn_event.hpp -- the roster removal it emits.
// related: hazard_spawn_system.hpp -- the first producer of lifetime-bearing entities.
class LifetimeExpirySystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "lifetime_expiry";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

  LifetimeExpirySystem() = default;
};

} // namespace blob_royale::gameplay

#endif
