#include "shared/respawn_system.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "entity_id.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "tick_context.hpp"
#include "world_event_registry.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// This tick's eliminated entities, ascending and distinct, exactly as `placement_recorder` reads
// them: the rule is stated over the set, so no producer's emission order can change what happens.
[[nodiscard]] std::vector<simulation::EntityId>
eliminated_entities_of(const simulation::GameWorld& world) {
  std::vector<simulation::EntityId> eliminated;
  for (const simulation::WorldEvent& event : world.events()) {
    if (const auto* elimination = std::get_if<simulation::EliminationEvent>(&event);
        elimination != nullptr) {
      eliminated.push_back(elimination->entity);
    }
  }
  std::sort(eliminated.begin(), eliminated.end());
  eliminated.erase(std::unique(eliminated.begin(), eliminated.end()), eliminated.end());
  return eliminated;
}

} // namespace

std::unique_ptr<const simulation::SimulationSystem>
RespawnSystem::create(const std::uint64_t respawn_delay_ticks) {
  return std::make_unique<const RespawnSystem>(respawn_delay_ticks);
}

void RespawnSystem::apply(simulation::GameWorld& world, const simulation::TickContext&) const {
  // Step 1. Timers that existed before this tick count down. Collected first because the writes
  // below erase from and assign to the very store being walked.
  std::vector<std::pair<simulation::EntityId, std::uint64_t>> timers;
  for (const simulation::ComponentStore<simulation::RespawnTimer>::Entry& entry :
       world.store<simulation::RespawnTimer>().entries()) {
    timers.emplace_back(entry.entity, entry.value.ticks_remaining);
  }
  for (const auto& [entity, ticks_remaining] : timers) {
    if (ticks_remaining <= 1) {
      // Erased rather than stored at zero, so a committed world never holds a timer that has run
      // out and "awaiting a body" has one spelling: a Controllable with neither body nor timer.
      world.mutable_store<simulation::RespawnTimer>().erase(entity);
      continue;
    }
    world.mutable_store<simulation::RespawnTimer>().insert_or_assign(
        entity, simulation::RespawnTimer{ticks_remaining - 1});
  }

  // Step 2. This tick's eliminations leave the field. The guard is the totality rule: an entity
  // with no body has nothing to erase and starts no timer.
  for (const simulation::EntityId entity : eliminated_entities_of(world)) {
    if (world.store<simulation::Controllable>().find(entity) == nullptr ||
        world.store<simulation::PhysicsBody>().find(entity) == nullptr) {
      continue;
    }
    world.mutable_store<simulation::PhysicsBody>().erase(entity);
    if (respawn_delay_ticks_ > 0) {
      world.mutable_store<simulation::RespawnTimer>().insert_or_assign(
          entity, simulation::RespawnTimer{respawn_delay_ticks_});
    }
  }
  // One registry sweep covers both this tick's removals and previously bodyless entities.
  // It precedes next-tick seating even for a zero delay and names no mode-owned component.
  world.erase_body_bound_components_without_body();
}

} // namespace blob_royale::gameplay
