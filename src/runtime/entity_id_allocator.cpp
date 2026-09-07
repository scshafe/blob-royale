#include "entity_id_allocator.hpp"

#include "simulation_limits.hpp"
#include "world_snapshot.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>

namespace blob_royale::runtime {

EntityIdAllocator::EntityIdAllocator(const simulation::EntityId first_issuable_entity_id) noexcept
    : first_issuable_entity_id_(first_issuable_entity_id.value()),
      next_entity_id_(first_issuable_entity_id.value()) {}

EntityIdAllocator EntityIdAllocator::create(const simulation::EntityId first_issuable_entity_id) {
  return EntityIdAllocator(first_issuable_entity_id);
}

EntityIdAllocator
EntityIdAllocator::above_committed_state(const simulation::GameSimulation& game_simulation) {
  // The map's static-body block, whose ids `GameWorld::create(configuration, map, seed)` owns.
  std::uint64_t floor = simulation::kMinimumEntityId +
                        static_cast<std::uint64_t>(game_simulation.map().static_bodies().size());

  // Everything actually committed, which additionally covers the scenario loader's explicitly
  // seeded ids. The roster is ascending and distinct, so its last element is its highest id.
  const simulation::WorldSnapshot committed = game_simulation.snapshot();
  const std::span<const simulation::EntityId> entities = committed.entities();
  if (!entities.empty()) {
    floor = std::max(floor, entities.back().value() + 1);
  }

  return create(simulation::EntityId::create(floor));
}

simulation::EntityIdReservation
EntityIdAllocator::reserve_for_tick(const std::uint64_t spawn_count) {
  const std::uint64_t cursor = next_entity_id_.load(std::memory_order_relaxed);
  const std::uint64_t width = spawn_count + simulation::kSystemCreatedEntityHeadroom;
  // Validation, including the overflow past kMaximumEntityId, belongs to the reservation value and
  // is not restated here: one rule, one implementation, one named validation code.
  const simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(cursor), width);
  next_entity_id_.store(cursor + width, std::memory_order_release);
  return reservation;
}

simulation::EntityId EntityIdAllocator::next_entity_id() const noexcept {
  return simulation::EntityId::create(next_entity_id_.load(std::memory_order_acquire));
}

std::uint64_t EntityIdAllocator::reserved_entity_id_count() const noexcept {
  return next_entity_id_.load(std::memory_order_acquire) - first_issuable_entity_id_;
}

} // namespace blob_royale::runtime
