#include "shared/lifetime_expiry_system.hpp"

#include "component_store.hpp"
#include "components/lifetime_component.hpp"
#include "entity_id.hpp"
#include "events/despawn_event.hpp"
#include "game_world.hpp"
#include "world_event_registry.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace blob_royale::gameplay {

namespace simulation = blob_royale::simulation;

std::unique_ptr<const simulation::SimulationSystem> LifetimeExpirySystem::create() {
  return std::make_unique<const LifetimeExpirySystem>();
}

void LifetimeExpirySystem::apply(simulation::GameWorld& world,
                                 const simulation::TickContext&) const {
  // The expired ids are collected from the read-only span first and emitted afterwards, so the
  // event list is never appended to while the store it names is being rewritten. `entries()` is
  // strict ascending EntityId order by construction, so the collected list is ascending and this
  // tick's DespawnEvents are appended in ascending order without a sort
  // (`simulation/component_store.hpp`).
  std::vector<simulation::EntityId> expired;
  const std::span<const simulation::ComponentStore<simulation::Lifetime>::Entry> entries =
      world.store<simulation::Lifetime>().entries();
  for (const auto& entry : entries) {
    // `<= 1` rather than `== 1` so the two ways a counter can be spent share one branch: a count of
    // one is spent by this tick, and a count of zero was already spent by whoever wrote it. Zero is
    // reachable -- nothing forbids seating an entity with `Lifetime{0}` -- and treating it as
    // "expires now" is the only reading that does not either underflow to 2^64-1 or leave the
    // entity alive forever, which are the two failure modes an unsigned countdown invites.
    if (entry.value.ticks_remaining <= 1) {
      expired.push_back(entry.entity);
    }
  }

  // The survivors are decremented in place. `mutable_values()` hands out no EntityId, so this
  // cannot disturb the ascending key order the collection above depends on, and the guard repeats
  // the predicate rather than sharing an index so the two walks stay independent of each other's
  // iteration state.
  for (simulation::Lifetime& lifetime :
       world.mutable_store<simulation::Lifetime>().mutable_values()) {
    if (lifetime.ticks_remaining > 1) {
      --lifetime.ticks_remaining;
    }
  }

  // The entity is not destroyed here. Emitting is enough: phase 10's `apply_despawn_events`
  // destroys everything a DespawnEvent names, and doing it there keeps removal in the one place
  // that also decides whether the spatial index must be rebuilt. Destroying directly would leave
  // that decision reading a roster that had already changed underneath it.
  for (const simulation::EntityId entity : expired) {
    world.emit(simulation::DespawnEvent{entity});
  }
}

} // namespace blob_royale::gameplay
