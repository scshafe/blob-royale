#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_SPAWN_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_SPAWN_SYSTEM_HPP

#include "shared/hazard_archetype.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

// canonical: hazard_spawn -- one random crossing birth trial per eligible running tick.
// @extension-point simulation_system
//
// Current room tuning supplies aggregate lethal and nonlethal births/second while capacity is
// available. An absent class has rate zero. A single unit draw selects lethal, nonlethal, or no
// birth; within the selected class one further draw chooses an authored kind with weight
// 1/spawn_interval_ticks. Declaration order is the deterministic cumulative-weight order.
// create_crossing_hazard then draws speed, entry edge, entry point, and opposite exit point.
// A failed birth trial consumes one draw; a birth consumes exactly six, including the explicit
// kind and speed draws even when those choices have only one possible result.
//
// No draw occurs outside running, when eligible rates are zero, without an entity reservation,
// or at the crossing/motion/entity population cap. Capacity suppresses new births without changing
// existing objects. The CrossingHazard store is the population, never a mirrored mutable counter.
// kLifecycle follows zone creation, preserving the existing one-system-entity reservation and
// zone precedence. At most one hazard is created by one invocation, even with a wider reservation.
// related: hazard_crossing.hpp -- the canonical active crossing cap and lifetime geometry.
// related: create_crossing_hazard.hpp -- per-instance sampling, body, and marker creation.
// related: lifetime_expiry_system.hpp -- removal of complete crossing entities.
class HazardSpawnSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "hazard_spawn";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(std::vector<HazardArchetype> archetypes);

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

  explicit HazardSpawnSystem(std::vector<HazardArchetype> archetypes) noexcept
      : archetypes_(std::move(archetypes)) {}

private:
  std::vector<HazardArchetype> archetypes_;
};

} // namespace blob_royale::gameplay

#endif
