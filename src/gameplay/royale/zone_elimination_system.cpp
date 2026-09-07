#include "royale/zone_elimination_system.hpp"

#include "component_join.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "entity_id.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// The elimination predicate, written exactly as `docs/architecture/0005-royale-mode.md`
// § "Elimination and placement" states it. `sqrt(dx * dx + dy * dy)` is written out rather than
// delegated to `std::hypot`, which computes a different binary64 value for the same inputs.
[[nodiscard]] bool center_is_outside(const simulation::Vector2& position,
                                     const simulation::Zone& zone) noexcept {
  const double offset_x = position.x() - zone.center.x();
  const double offset_y = position.y() - zone.center.y();
  const double distance = std::sqrt((offset_x * offset_x) + (offset_y * offset_y));
  return distance > zone.radius + simulation::kPositionTolerance;
}

} // namespace

std::unique_ptr<const simulation::SimulationSystem>
ZoneEliminationSystem::create(RoyaleConfiguration configuration) {
  return std::make_unique<const ZoneEliminationSystem>(std::move(configuration));
}

ZoneEliminationSystem::ZoneEliminationSystem(RoyaleConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

void ZoneEliminationSystem::apply(simulation::GameWorld& world,
                                  const simulation::TickContext&) const {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }

  const std::span<const simulation::ComponentStore<simulation::Zone>::Entry> zones =
      world.store<simulation::Zone>().entries();
  if (zones.empty()) {
    throw GameplayValidationError(
        GameplayValidationCode::kRoyaleZoneAbsent, "zone_elimination.zone",
        "no entity carries a Zone while the match is running; zone_elimination must be declared "
        "after zone_shrink at kPostKernel");
  }
  const simulation::Zone zone = zones.front().value;
  const std::uint64_t grace_ticks = configuration_.elimination_grace_ticks();

  // The canonical two-store join visits every alive entity in ascending EntityId order, which is
  // the order this tick's EliminationEvents are therefore appended in. The stores written here --
  // ZoneExposure -- and the event list are neither of the two being walked, so the walk stays
  // valid.
  simulation::for_each_entity_with_both(
      world.store<simulation::PhysicsBody>(), world.store<simulation::Controllable>(),
      [&world, &zone, grace_ticks](const simulation::EntityId entity,
                                   const simulation::PhysicsBody& body,
                                   const simulation::Controllable&) {
        if (!center_is_outside(body.position(), zone)) {
          // Re-entering resets the counter and any partial grace is lost. Erasing rather than
          // storing an explicit zero keeps one world state with one spelling, because an absent
          // ZoneExposure already reads as zero.
          world.mutable_store<simulation::ZoneExposure>().erase(entity);
          return;
        }
        const simulation::ZoneExposure* exposure =
            world.store<simulation::ZoneExposure>().find(entity);
        const std::uint64_t outside_ticks = (exposure == nullptr ? 0 : exposure->outside_ticks) + 1;
        world.mutable_store<simulation::ZoneExposure>().insert_or_assign(
            entity, simulation::ZoneExposure{outside_ticks});
        // The increment precedes the test, so G = 0 eliminates on the first outside tick.
        if (outside_ticks >= grace_ticks) {
          world.emit(simulation::EliminationEvent{entity});
        }
      });
}

} // namespace blob_royale::gameplay
