#include "spawn_system.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_seating.hpp"
#include "terrain_queries.hpp"
#include "tick_context.hpp"
#include "vector2.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::simulation {
namespace {

// Every entity carrying a Controllable and no PhysicsBody, ascending. The Controllable store is
// ascending by construction, so this is one forward pass and the order is the contract's.
[[nodiscard]] std::vector<EntityId> entities_awaiting_a_body(const GameWorld& world) {
  std::vector<EntityId> pending;
  for (const ComponentStore<Controllable>::Entry& entry : world.store<Controllable>().entries()) {
    if (world.store<PhysicsBody>().find(entry.entity) == nullptr) {
      pending.push_back(entry.entity);
    }
  }
  return pending;
}

} // namespace

SpawnSystem::SpawnSystem(std::unique_ptr<const SpawnPolicy> policy) noexcept
    : policy_(std::move(policy)) {}

std::size_t SpawnSystem::seat_pending_entities(GameWorld& world, const TickContext& context) const {
  const std::vector<EntityId> pending = entities_awaiting_a_body(world);
  if (pending.empty()) {
    return 0;
  }

  const std::span<const MapDefinition::Marker> spawn_points = context.map().spawn_points();
  const std::size_t point_count = spawn_points.size();
  const double player_radius = context.simulation_config().player_radius();

  // A contiguous array of `bool` rather than a `std::vector<bool>`, because the policy is handed a
  // `std::span<const bool>` and the vector specialization has no such storage. It is allocated
  // only when something is actually awaiting a body, which is no tick of any accepted fixture.
  const std::unique_ptr<bool[]> point_is_free = std::make_unique<bool[]>(point_count);
  const auto refresh_free_points = [&] {
    for (std::size_t index = 0; index < point_count; ++index) {
      point_is_free[index] = seat_is_supported_and_unoccupied(
          spawn_points[index].position, world.store<PhysicsBody>().entries(), player_radius,
          context.map().terrain());
    }
  };
  refresh_free_points();

  std::size_t seated_count = 0;
  for (const EntityId entity : pending) {
    const std::span<const bool> free_points(point_is_free.get(), point_count);
    const std::optional<std::size_t> chosen = policy_->choose_spawn_point(
        world, context, entity, static_cast<std::size_t>(world.match().spawn_rotation_counter),
        free_points);
    if (!chosen.has_value()) {
      // Deferred. The entity is offered again, in this same order, on a later tick.
      continue;
    }
    if (*chosen >= point_count) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpawnPolicyIndexOutOfRange,
          "spawn_system.spawn_points[entity_id=" + std::to_string(entity.value()) + "]",
          "the mode's spawn policy chose index " + std::to_string(*chosen) + " of " +
              std::to_string(point_count) + " spawn points");
    }
    if (!point_is_free[*chosen]) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpawnPolicyPointOccupied,
          "spawn_system.spawn_points[entity_id=" + std::to_string(entity.value()) + "]",
          "the mode's spawn policy chose occupied spawn point " + std::to_string(*chosen));
    }

    // The shared at-rest write (`spawn_seating.hpp`): zero velocity, zero stored acceleration, and
    // the configured radius, which is the only radius a seated body can truthfully publish.
    // `ScenarioLoader` seeds the same way, so seating and seeding agree.
    seat_body_at_rest(world, entity, spawn_points[*chosen].position, player_radius);
    // A new disc can occupy nearby markers as well as its chosen marker.
    refresh_free_points();
    // One past the index just used, so the next entity a forward-probing policy offers starts at
    // the following point rather than re-probing this one.
    world.mutable_match().spawn_rotation_counter =
        static_cast<std::uint64_t>((*chosen + 1) % point_count);
    ++seated_count;
  }
  return seated_count;
}

void require_spawn_points_are_seatable(const SimulationConfig& configuration,
                                       const MapDefinition& map) {
  const std::span<const MapDefinition::Marker> spawn_points = map.spawn_points();
  for (std::size_t index = 0; index < spawn_points.size(); ++index) {
    if (!map.bounds().contains_disc_center(spawn_points[index].position,
                                           configuration.player_radius())) {
      throw SimulationValidationError(
          SimulationValidationCode::kMapSpawnPointNotSeatable,
          "map." + std::string(map.name()) + ".spawn_points[" + std::to_string(index) +
              "].position",
          "a spawn point must keep the complete closed disc of the configured player radius inside "
          "the arena");
    }
    if (!terrain_supports_disc(map.terrain(), spawn_points[index].position,
                               configuration.player_radius())) {
      throw SimulationValidationError(
          SimulationValidationCode::kMapSpawnPointNotSeatable,
          "map." + std::string(map.name()) + ".spawn_points[" + std::to_string(index) +
              "].position",
          "a spawn point must keep the complete closed disc of the configured player radius on "
          "supported terrain");
    }
  }
}

} // namespace blob_royale::simulation
