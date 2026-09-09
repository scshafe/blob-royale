#include "royale/placement_recorder_system.hpp"

#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/despawn_event.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "royale/royale_mode_state.hpp"
#include "shared/roster.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"
#include "world_event_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// This tick's eliminated entities, ascending and distinct.
//
// `zone_elimination` already emits in ascending EntityId order and names an entity at most once, so
// the sort and the de-duplication are no-ops today. They are here because this rule is stated over
// *the set* of eliminations rather than over one producer's output order, and a second producer --
// a future last-team-standing rule, a scripted elimination in a fixture -- must not be able to
// change what placement an entity gets by emitting in a different order.
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

// The controller driving one entity about to be destroyed.
//
// This is read **before** step 2 destroys the entity, because it is the last instant the link
// exists: `destroy_entity` erases the `Controllable`, and the placement the entity earns must carry
// the controller for the whole rest of the match
// (`mode_states/royale_placements_mode_state.hpp`).
//
// An eliminated entity with no `Controllable` is an internal invariant violation and fails hard.
// "Alive" is defined as owning both a `PhysicsBody` and a `Controllable`
// (`docs/architecture/0005-royale-mode.md` § "Scope, vocabulary, and evaluation order") and
// `zone_elimination` only ever names an alive entity, so this can fire only for a future producer
// that eliminated something that was never playing -- which would produce a placement no wire
// encoding can represent. That is the fail-hard half of the ADR's rule: fail-soft at the untrusted
// command boundary, fail-hard on an internal invariant.
[[nodiscard]] simulation::ControllerId controller_of_eliminated(const simulation::GameWorld& world,
                                                                const simulation::EntityId entity) {
  const simulation::Controllable* controllable =
      world.store<simulation::Controllable>().find(entity);
  if (controllable == nullptr) {
    throw GameplayValidationError(
        GameplayValidationCode::kRoyaleEliminatedEntityWithoutController,
        "placement_recorder.placements[entity_id=" + std::to_string(entity.value()) + "]",
        "an eliminated entity must carry a Controllable; a placement without a controller cannot "
        "be published");
  }
  return controllable->controller_id;
}

} // namespace

std::unique_ptr<const simulation::SimulationSystem> PlacementRecorderSystem::create() {
  return std::make_unique<const PlacementRecorderSystem>();
}

void PlacementRecorderSystem::apply(simulation::GameWorld& world,
                                    const simulation::TickContext& context) const {
  const simulation::MatchPhase phase = world.match().phase;
  // The engine's record of what the tick before committed (`match_state.hpp`); the block's own
  // member is the published mirror of it and is read back by nothing.
  const simulation::MatchPhase previous_phase = world.match().previous_phase;
  simulation::RoyalePlacementsModeState mode_state = royale_mode_state_of(world);

  // Step 1. The first tick of a match, which is also the first tick that can append to the list.
  if (phase == simulation::MatchPhase::kRunning &&
      previous_phase == simulation::MatchPhase::kCountdown) {
    mode_state.placements.clear();
  }

  // Step 2. Destroy first, then count, then append: `alive_after` is by definition the count after
  // this tick's eliminations have left the roster.
  const std::vector<simulation::EntityId> eliminated = eliminated_entities_of(world);
  if (!eliminated.empty()) {
    // Resolved before the destruction below, in the same ascending order the placements are
    // appended in, because destroying the entity is what removes the only record of its controller.
    std::vector<simulation::ControllerId> eliminated_controllers;
    eliminated_controllers.reserve(eliminated.size());
    for (const simulation::EntityId entity : eliminated) {
      eliminated_controllers.push_back(controller_of_eliminated(world, entity));
    }
    for (const simulation::EntityId entity : eliminated) {
      world.destroy_entity(entity);
      world.emit(simulation::DespawnEvent{entity});
    }
    const std::uint64_t placement = static_cast<std::uint64_t>(alive_count(world)) + 1;
    if (mode_state.placements.size() + eliminated.size() > simulation::kMaximumPlayerCount) {
      throw GameplayValidationError(
          GameplayValidationCode::kRoyalePlacementLimitExceeded, "placement_recorder.placements",
          "one match's placement list would hold " +
              std::to_string(mode_state.placements.size() + eliminated.size()) +
              " entries, past the accepted limit " +
              std::to_string(simulation::kMaximumPlayerCount));
    }
    for (std::size_t index = 0; index < eliminated.size(); ++index) {
      mode_state.placements.push_back(simulation::RoyalePlacement{
          eliminated[index], eliminated_controllers[index], placement, context.tick_sequence()});
    }
  }

  // Step 3. The phase `MatchState` holds while this system runs is the phase the previous tick
  // committed; the engine's transition for this tick has not run yet. It is the same observation
  // the lifecycle system records into `MatchState::previous_phase` at the end of this tick, kept
  // here as a published mirror so `royale-mode-state.schema.json` is unchanged; no royale rule
  // reads it back.
  mode_state.previous_phase = phase;
  world.mutable_match().mode_state = std::move(mode_state);
}

} // namespace blob_royale::gameplay
