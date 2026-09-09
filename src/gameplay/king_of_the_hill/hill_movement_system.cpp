#include "king_of_the_hill/hill_movement_system.hpp"

#include "component_store.hpp"
#include "components/hill_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/hill_geometry.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// Committed ticks between two sequences this simulation has already committed or is committing, so
// the subtraction never underflows; the same saturating shape `zone_shrink` uses.
[[nodiscard]] std::uint64_t ticks_since(const simulation::TickSequence now,
                                        const simulation::TickSequence started) noexcept {
  return now.value() >= started.value() ? now.value() - started.value() : 0;
}

[[nodiscard]] simulation::EntityId create_hill_entity(simulation::GameWorld& world) {
  if (world.entity_id_reservation().empty()) {
    throw GameplayValidationError(
        GameplayValidationCode::kKingOfTheHillHillEntityUnreserved, "hill_movement.hill_entity",
        "king of the hill draws one EntityId from the tick's reservation to create the hill "
        "entity, and this tick's reservation is empty");
  }
  return world.create_entity();
}

} // namespace

std::unique_ptr<const simulation::SimulationSystem>
HillMovementSystem::create(KingOfTheHillConfiguration configuration) {
  return std::make_unique<const HillMovementSystem>(std::move(configuration));
}

HillMovementSystem::HillMovementSystem(KingOfTheHillConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

void HillMovementSystem::apply(simulation::GameWorld& world,
                               const simulation::TickContext& context) const {
  const std::vector<simulation::Vector2> markers = hill_markers(context.map());
  if (markers.empty()) {
    throw GameplayValidationError(
        GameplayValidationCode::kKingOfTheHillMapWithoutHill, "hill_movement.markers",
        "map " + std::string(context.map().name()) +
            " carries no hill marker; king of the hill's validate_map refuses such a map at "
            "construction");
  }

  const simulation::MatchState& match = world.match();
  std::uint64_t elapsed_running_ticks = 0;
  switch (match.phase) {
  case simulation::MatchPhase::kLobby:
  case simulation::MatchPhase::kCountdown:
    elapsed_running_ticks = 0;
    break;
  case simulation::MatchPhase::kRunning:
    elapsed_running_ticks = ticks_since(context.tick_sequence(), match.running_started_tick);
    break;
  case simulation::MatchPhase::kEnded:
    elapsed_running_ticks = ticks_since(match.phase_started_tick, match.running_started_tick);
    break;
  }
  const simulation::Vector2 center =
      hill_center(markers, configuration_.hill_dwell_ticks(), configuration_.hill_travel_ticks(),
                  elapsed_running_ticks);

  const std::span<const simulation::ComponentStore<simulation::Hill>::Entry> hills =
      world.store<simulation::Hill>().entries();
  // The store is ascending by construction, so `front()` is the lowest EntityId carrying a `Hill`.
  const simulation::EntityId hill_entity =
      hills.empty() ? create_hill_entity(world) : hills.front().entity;
  world.mutable_store<simulation::Hill>().insert_or_assign(
      hill_entity, simulation::Hill{center, configuration_.hill_radius()});
}

} // namespace blob_royale::gameplay
