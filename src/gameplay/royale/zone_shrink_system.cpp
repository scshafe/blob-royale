#include "royale/zone_shrink_system.hpp"

#include "component_store.hpp"
#include "components/zone_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// Committed ticks between two sequences this simulation has already committed or is committing, so
// the subtraction never underflows. It is the same saturating shape `match_lifecycle_system.cpp`
// uses for the phase clock, for the same reason.
[[nodiscard]] std::uint64_t ticks_since(const simulation::TickSequence now,
                                        const simulation::TickSequence started) noexcept {
  return now.value() >= started.value() ? now.value() - started.value() : 0;
}

// The zone entity, created on the first tick no entity carries `Zone`.
//
// A tick whose reservation is empty may create nothing at all, which for royale means a match with
// no zone and therefore no elimination -- a silently different game. It fails hard naming the cause
// instead, because the reservation is part of every tick's declared input and an empty one is the
// caller handing this mode a tick it cannot play (`entity_id_reservation.hpp`).
[[nodiscard]] simulation::EntityId create_zone_entity(simulation::GameWorld& world) {
  if (world.entity_id_reservation().empty()) {
    throw GameplayValidationError(
        GameplayValidationCode::kRoyaleZoneEntityUnreserved, "zone_shrink.zone_entity",
        "royale draws one EntityId from the tick's reservation to create the zone entity, and this "
        "tick's reservation is empty");
  }
  return world.create_entity();
}

} // namespace

simulation::Vector2 zone_center(const simulation::ArenaBounds& bounds) {
  return simulation::Vector2::create(bounds.width() / 2.0, bounds.height() / 2.0);
}

double zone_full_radius(const simulation::ArenaBounds& bounds) {
  const double half_width = bounds.width() / 2.0;
  const double half_height = bounds.height() / 2.0;
  // Written out rather than delegated to `std::hypot`, which computes a different binary64 value
  // for the same inputs, exactly as `steered_acceleration` writes its magnitude out.
  return std::sqrt((half_width * half_width) + (half_height * half_height));
}

double zone_radius(const double full_radius, const double minimum_radius,
                   const std::uint64_t elapsed_running_ticks, const std::uint64_t shrink_ticks) {
  if (shrink_ticks == 0 || elapsed_running_ticks >= shrink_ticks) {
    // By assignment, not by arithmetic, so the held radius is exactly `R_min`.
    return minimum_radius;
  }
  const double elapsed_fraction =
      static_cast<double>(elapsed_running_ticks) / static_cast<double>(shrink_ticks);
  return full_radius - ((full_radius - minimum_radius) * elapsed_fraction);
}

std::unique_ptr<const simulation::SimulationSystem>
ZoneShrinkSystem::create(RoyaleConfiguration configuration) {
  return std::make_unique<const ZoneShrinkSystem>(std::move(configuration));
}

ZoneShrinkSystem::ZoneShrinkSystem(RoyaleConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

void ZoneShrinkSystem::apply(simulation::GameWorld& world,
                             const simulation::TickContext& context) const {
  // Bounds come from the map, so the same mode plays a different arena with no configuration
  // change.
  const simulation::Vector2 center = zone_center(context.map().bounds());
  const double full_radius = zone_full_radius(context.map().bounds());
  const simulation::MatchState& match = world.match();

  double radius = full_radius;
  switch (match.phase) {
  case simulation::MatchPhase::kLobby:
  case simulation::MatchPhase::kCountdown:
    radius = full_radius;
    break;
  case simulation::MatchPhase::kRunning:
    radius = zone_radius(full_radius, configuration_.zone_minimum_radius(),
                         ticks_since(context.tick_sequence(), match.running_started_tick),
                         configuration_.zone_shrink_ticks());
    break;
  case simulation::MatchPhase::kEnded:
    radius = zone_radius(full_radius, configuration_.zone_minimum_radius(),
                         ticks_since(match.phase_started_tick, match.running_started_tick),
                         configuration_.zone_shrink_ticks());
    break;
  }

  const std::span<const simulation::ComponentStore<simulation::Zone>::Entry> zones =
      world.store<simulation::Zone>().entries();
  // The store is ascending by construction, so `front()` is the lowest EntityId carrying a `Zone`,
  // which is royale's tie policy applied to a world that somehow carries two.
  const simulation::EntityId zone_entity =
      zones.empty() ? create_zone_entity(world) : zones.front().entity;
  world.mutable_store<simulation::Zone>().insert_or_assign(zone_entity,
                                                           simulation::Zone{center, radius});
}

} // namespace blob_royale::gameplay
