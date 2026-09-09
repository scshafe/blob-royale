#include "king_of_the_hill/hill_scoring_system.hpp"

#include "component_join.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/disc_geometry.hpp"
#include "tick_context.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

} // namespace

std::unique_ptr<const simulation::SimulationSystem>
HillScoringSystem::create(KingOfTheHillConfiguration configuration) {
  return std::make_unique<const HillScoringSystem>(std::move(configuration));
}

HillScoringSystem::HillScoringSystem(KingOfTheHillConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

void HillScoringSystem::apply(simulation::GameWorld& world, const simulation::TickContext&) const {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }

  const std::span<const simulation::ComponentStore<simulation::Hill>::Entry> hills =
      world.store<simulation::Hill>().entries();
  if (hills.empty()) {
    throw GameplayValidationError(
        GameplayValidationCode::kKingOfTheHillHillAbsent, "hill_scoring.hill",
        "no entity carries a Hill while the match is running; hill_scoring must be declared after "
        "hill_movement at kPostKernel");
  }
  const simulation::Hill hill = hills.front().value;
  const std::uint64_t point_interval_ticks = configuration_.point_interval_ticks();
  const bool contested_scores = configuration_.contested_hill_scores();

  // Who is inside is decided once, before anyone scores, so the contested test reads the same
  // count for every entity this tick. The join visits ascending EntityId.
  std::vector<std::pair<simulation::EntityId, bool>> inside_by_entity;
  std::size_t inside_count = 0;
  simulation::for_each_entity_with_both(
      world.store<simulation::PhysicsBody>(), world.store<simulation::Controllable>(),
      [&](const simulation::EntityId entity, const simulation::PhysicsBody& body,
          const simulation::Controllable&) {
        const bool inside = !center_is_outside(body.position(), hill.center, hill.radius);
        inside_count += inside ? 1 : 0;
        inside_by_entity.emplace_back(entity, inside);
      });

  const bool hill_scores = contested_scores || inside_count <= 1;
  for (const auto& [entity, inside] : inside_by_entity) {
    if (!inside) {
      // Leaving loses the partial point. Erasing rather than storing a zero keeps one world state
      // with one spelling, because an absent HillPresence already reads as zero.
      world.mutable_store<simulation::HillPresence>().erase(entity);
      continue;
    }
    if (!hill_scores) {
      // Contested: progress is frozen, neither gained nor lost.
      continue;
    }
    const simulation::HillPresence* presence = world.store<simulation::HillPresence>().find(entity);
    const std::uint64_t inside_ticks = (presence == nullptr ? 0 : presence->inside_ticks) + 1;
    // The increment precedes the test, so I = 0 scores on the first inside tick.
    if (inside_ticks >= point_interval_ticks) {
      const simulation::Score* score = world.store<simulation::Score>().find(entity);
      world.mutable_store<simulation::Score>().insert_or_assign(
          entity, simulation::Score{(score == nullptr ? 0 : score->points) + 1});
      world.mutable_store<simulation::HillPresence>().erase(entity);
      continue;
    }
    world.mutable_store<simulation::HillPresence>().insert_or_assign(
        entity, simulation::HillPresence{inside_ticks});
  }

  // Being knocked out forgets the partial point: a presence counter on an entity with no body is
  // the shape the shared respawn leaves behind, and this system owns its hygiene.
  std::vector<simulation::EntityId> bodiless;
  for (const simulation::ComponentStore<simulation::HillPresence>::Entry& entry :
       world.store<simulation::HillPresence>().entries()) {
    if (world.store<simulation::PhysicsBody>().find(entry.entity) == nullptr) {
      bodiless.push_back(entry.entity);
    }
  }
  for (const simulation::EntityId entity : bodiless) {
    world.mutable_store<simulation::HillPresence>().erase(entity);
  }
}

} // namespace blob_royale::gameplay
