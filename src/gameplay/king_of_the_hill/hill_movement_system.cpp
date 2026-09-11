#include "king_of_the_hill/hill_movement_system.hpp"

#include "component_store.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/hill_geometry.hpp"
#include "king_of_the_hill/hill_roaming.hpp"
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

void apply_random_roam(simulation::GameWorld& world, const simulation::TickContext& context,
                       const KingOfTheHillConfiguration& configuration,
                       const simulation::Vector2& first_marker) {
  const auto hills = world.store<simulation::Hill>().entries();
  const simulation::EntityId entity =
      hills.empty() ? create_hill_entity(world) : hills.front().entity;
  const simulation::Hill* existing_hill = world.store<simulation::Hill>().find(entity);
  const simulation::HillMotion* existing_motion =
      world.store<simulation::HillMotion>().find(entity);
  const simulation::MatchPhase phase = world.match().phase;
  if (phase == simulation::MatchPhase::kLobby || phase == simulation::MatchPhase::kCountdown) {
    // The hill is not a participant, so generic round cleanup does not reset it.
    world.mutable_store<simulation::Hill>().insert_or_assign(
        entity, simulation::Hill{first_marker, configuration.hill_radius()});
    world.mutable_store<simulation::HillMotion>().insert_or_assign(
        entity, simulation::HillMotion{simulation::Vector2::create(0.0, 0.0)});
    return;
  }
  if (phase == simulation::MatchPhase::kEnded) {
    if (existing_hill == nullptr || existing_motion == nullptr) {
      throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillMotionStateInvalid,
                                    "hill_movement.ended",
                                    "ended roaming hill has no committed motion state");
    }
    return;
  }
  simulation::Hill hill = existing_hill == nullptr
                              ? simulation::Hill{first_marker, configuration.hill_radius()}
                              : *existing_hill;
  simulation::HillMotion motion =
      existing_motion == nullptr ? simulation::HillMotion{simulation::Vector2::create(0.0, 0.0)}
                                 : *existing_motion;
  if (motion.schedule.has_value() &&
      motion.schedule->random_stream != simulation::RandomStreamKind::kHill) {
    throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillMotionStateInvalid,
                                  "hill_movement.random_stream",
                                  "roaming hill must use the named hill stream");
  }
  if (!motion.schedule.has_value() ||
      context.tick_sequence() >= motion.schedule->next_retarget_tick) {
    // Reserve room for every possible sampled interval before advancing the generator. Never
    // clamp or wrap a deadline, and do not bias the interval by sampling until one fits.
    if (configuration.hill_retarget_maximum_ticks() >
        simulation::TickSequence::kMaximumValue - context.tick_sequence().value()) {
      throw GameplayValidationError(GameplayValidationCode::kKingOfTheHillRetargetTickOverflow,
                                    "hill_movement.next_retarget_tick",
                                    "hill retarget deadline exceeds the tick sequence limit");
    }
    const HillRoamSelection selected =
        sample_hill_roam(world.random(simulation::RandomStreamKind::kHill), configuration);
    motion.velocity = selected.velocity;
    motion.schedule = simulation::HillMotionSchedule{
        simulation::TickSequence::create(context.tick_sequence().value() +
                                         selected.retarget_after_ticks),
        simulation::RandomStreamKind::kHill};
  }
  const HillRoamMotion advanced = advance_hill_roam(hill.center, motion.velocity,
                                                    context.map().bounds(), context.fixed_delta());
  hill.center = advanced.center;
  hill.radius = configuration.hill_radius();
  motion.velocity = advanced.velocity;
  world.mutable_store<simulation::Hill>().insert_or_assign(entity, hill);
  world.mutable_store<simulation::HillMotion>().insert_or_assign(entity, motion);
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

  if (configuration_.motion_policy() == KingOfTheHillConfiguration::HillMotionPolicy::kRandomRoam) {
    apply_random_roam(world, context, configuration_, markers.front());
    return;
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
