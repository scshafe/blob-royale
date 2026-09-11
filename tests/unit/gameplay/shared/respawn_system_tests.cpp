#include "shared/respawn_system.hpp"

#include "fixtures/respawn_component_lifetime_fixture.hpp"
#include "gameplay_test_fixture.hpp"

#include "component_lifetime.hpp"
#include "components/controllable_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_system.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace lifetime_fixture = testing::component_lifetime_fixture;
namespace respawn_lifetime_fixture = testing::respawn_component_lifetime_fixture;

namespace {

[[nodiscard]] simulation::EntityId entity(const std::uint64_t id) {
  return simulation::EntityId::create(id);
}

[[nodiscard]] simulation::GameWorld world_with_players(const std::uint64_t player_count) {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (std::uint64_t index = 0; index < player_count; ++index) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        entity(index + 1),
        simulation::PhysicsBody::create(
            simulation::Vector2::create(60.0 * static_cast<double>(index + 1), 60.0),
            simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(0.0, 0.0)),
        simulation::ControllerId::create(index + 1)));
  }
  return simulation::GameWorld::create(std::move(seeds));
}

void apply(const simulation::SimulationSystem& system, simulation::GameWorld& world) {
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  system.apply(world, harness.context());
}

[[nodiscard]] bool has_body(const simulation::GameWorld& world, const std::uint64_t id) {
  return world.store<simulation::PhysicsBody>().find(entity(id)) != nullptr;
}

[[nodiscard]] const simulation::RespawnTimer* timer_of(const simulation::GameWorld& world,
                                                       const std::uint64_t id) {
  return world.store<simulation::RespawnTimer>().find(entity(id));
}

} // namespace

TEST_CASE("an eliminated entity loses its body, keeps its controller, and starts the timer",
          "[unit][gameplay][shared][respawn]") {
  const std::unique_ptr<const simulation::SimulationSystem> respawn =
      gameplay::RespawnSystem::create(3);
  simulation::GameWorld world = world_with_players(2);
  world.emit(simulation::EliminationEvent{entity(2)});

  apply(*respawn, world);

  CHECK_FALSE(has_body(world, 2));
  CHECK(world.store<simulation::Controllable>().find(entity(2)) != nullptr);
  REQUIRE(timer_of(world, 2) != nullptr);
  CHECK(timer_of(world, 2)->ticks_remaining == 3);
  // The survivor is untouched.
  CHECK(has_body(world, 1));
  CHECK(timer_of(world, 1) == nullptr);
}

TEST_CASE("the timer counts down on the ticks after the elimination and is erased at zero",
          "[unit][gameplay][shared][respawn]") {
  // Eliminated on tick N with D = 3: the timer reads 3 on N, 2 on N + 1, 1 on N + 2, and is gone
  // on N + 3, when the entity awaits a body and the policy is offered it on N + 4 = N + D + 1.
  const std::unique_ptr<const simulation::SimulationSystem> respawn =
      gameplay::RespawnSystem::create(3);
  simulation::GameWorld world = world_with_players(1);
  world.emit(simulation::EliminationEvent{entity(1)});

  apply(*respawn, world);
  REQUIRE(timer_of(world, 1) != nullptr);
  CHECK(timer_of(world, 1)->ticks_remaining == 3);

  // The event list of a hand-built world is never cleared, so every later application still sees
  // the elimination; the guard on the second loop is what keeps it from restarting the timer.
  apply(*respawn, world);
  REQUIRE(timer_of(world, 1) != nullptr);
  CHECK(timer_of(world, 1)->ticks_remaining == 2);
  apply(*respawn, world);
  REQUIRE(timer_of(world, 1) != nullptr);
  CHECK(timer_of(world, 1)->ticks_remaining == 1);
  apply(*respawn, world);
  CHECK(timer_of(world, 1) == nullptr);
  CHECK_FALSE(has_body(world, 1));
  CHECK(world.store<simulation::Controllable>().find(entity(1)) != nullptr);
}

TEST_CASE("a zero delay erases the body and attaches no timer",
          "[unit][gameplay][shared][respawn]") {
  const std::unique_ptr<const simulation::SimulationSystem> respawn =
      gameplay::RespawnSystem::create(0);
  simulation::GameWorld world = world_with_players(1);
  world.emit(simulation::EliminationEvent{entity(1)});

  apply(*respawn, world);

  CHECK_FALSE(has_body(world, 1));
  CHECK(timer_of(world, 1) == nullptr);
  CHECK(world.store<simulation::RespawnTimer>().entries().empty());
}

TEST_CASE("an entity named twice in one tick is eliminated once, and one without a body or a "
          "controller is ignored",
          "[unit][gameplay][shared][respawn]") {
  const std::unique_ptr<const simulation::SimulationSystem> respawn =
      gameplay::RespawnSystem::create(5);
  simulation::GameWorld world = world_with_players(1);
  // A wall carries a body and no controller; a pending joiner carries a controller and no body.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(30),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(300.0, 300.0)));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(40), simulation::Controllable{simulation::ControllerId::create(40)});
  world.emit(simulation::EliminationEvent{entity(1)});
  world.emit(simulation::EliminationEvent{entity(1)});
  world.emit(simulation::EliminationEvent{entity(30)});
  world.emit(simulation::EliminationEvent{entity(40)});

  apply(*respawn, world);

  REQUIRE(timer_of(world, 1) != nullptr);
  CHECK(timer_of(world, 1)->ticks_remaining == 5);
  CHECK(has_body(world, 30));
  CHECK(timer_of(world, 30) == nullptr);
  CHECK(timer_of(world, 40) == nullptr);
  CHECK(world.store<simulation::RespawnTimer>().entries().size() == 1);
}

TEST_CASE("an entity eliminated this tick is not decremented this tick",
          "[unit][gameplay][shared][respawn]") {
  // Two respawning entities: 1 was eliminated on an earlier tick and counts down; 2 is eliminated
  // now and starts at the full delay, because existing timers are walked before new ones are set.
  const std::unique_ptr<const simulation::SimulationSystem> respawn =
      gameplay::RespawnSystem::create(2);
  simulation::GameWorld world = world_with_players(2);
  world.mutable_store<simulation::PhysicsBody>().erase(entity(1));
  world.mutable_store<simulation::RespawnTimer>().insert_or_assign(entity(1),
                                                                   simulation::RespawnTimer{2});
  world.emit(simulation::EliminationEvent{entity(2)});

  apply(*respawn, world);

  REQUIRE(timer_of(world, 1) != nullptr);
  CHECK(timer_of(world, 1)->ticks_remaining == 1);
  REQUIRE(timer_of(world, 2) != nullptr);
  CHECK(timer_of(world, 2)->ticks_remaining == 2);
}

TEST_CASE("respawn cleans all body-bound kinds after zero or delayed duplicate eliminations",
          "[unit][gameplay][shared][respawn][component_lifetime]") {
  for (const auto delay : respawn_lifetime_fixture::kRespawnDelays) {
    CAPTURE(delay);
    auto world = respawn_lifetime_fixture::world_with_duplicate_eliminations();
    const auto before = world;
    const auto respawn = gameplay::RespawnSystem::create(delay);
    apply(*respawn, world);

    const auto eliminated = lifetime_fixture::entity(respawn_lifetime_fixture::kEliminatedPlayer);
    CHECK(world.store<simulation::PhysicsBody>().find(eliminated) == nullptr);
    CHECK(world.store<simulation::Controllable>() == before.store<simulation::Controllable>());
    CHECK(world.store<simulation::Score>() == before.store<simulation::Score>());
    CHECK(world.store<simulation::RaceProgress>() == before.store<simulation::RaceProgress>());
    CHECK(world.store<simulation::Hill>() == before.store<simulation::Hill>());
    CHECK(world.store<simulation::HillMotion>() == before.store<simulation::HillMotion>());
    CHECK_FALSE(world.contains(
        lifetime_fixture::entity(respawn_lifetime_fixture::kBodylessNonparticipant)));
    simulation::ComponentRegistry::for_each_kind([&world]<typename Component>() {
      if constexpr (simulation::ComponentLifetime<Component>::bound_to_body) {
        for (const auto& entry : world.store<Component>().entries()) {
          CHECK(world.store<simulation::PhysicsBody>().find(entry.entity) != nullptr);
        }
      }
    });
    REQUIRE(world.store<simulation::HillPresence>().size() == 1);
    CHECK(world.store<simulation::HillPresence>().entries().front().entity ==
          lifetime_fixture::entity(lifetime_fixture::kNonparticipantBody));

    const auto* new_timer = world.store<simulation::RespawnTimer>().find(eliminated);
    if (delay == 0) {
      CHECK(new_timer == nullptr);
    } else {
      REQUIRE(new_timer != nullptr);
      CHECK(new_timer->ticks_remaining == delay);
    }
    const auto* old_timer = world.store<simulation::RespawnTimer>().find(
        lifetime_fixture::entity(respawn_lifetime_fixture::kExistingBodylessPlayer));
    REQUIRE(old_timer != nullptr);
    CHECK(old_timer->ticks_remaining == lifetime_fixture::kReturnTicks - 1);

    // Reapplying sees the retained duplicate event list, but starts no replacement timer and
    // cannot restore body-bound state. Existing timers still advance normally.
    apply(*respawn, world);
    CHECK(world.store<simulation::HillPresence>().find(eliminated) == nullptr);
    CHECK(world.store<simulation::ZoneExposure>().find(eliminated) == nullptr);
    const auto* following_timer = world.store<simulation::RespawnTimer>().find(eliminated);
    if (delay == 0) {
      CHECK(following_timer == nullptr);
    } else {
      REQUIRE(following_timer != nullptr);
      CHECK(following_timer->ticks_remaining == delay - 1);
    }
  }
}

TEST_CASE("respawn cleans previously bodyless counters even without an elimination event",
          "[unit][gameplay][shared][respawn][component_lifetime]") {
  auto world = lifetime_fixture::mixed_world();
  const auto respawn =
      gameplay::RespawnSystem::create(respawn_lifetime_fixture::kRespawnDelays.front());
  apply(*respawn, world);
  for (const auto id : lifetime_fixture::kMixedEntities) {
    const auto target = lifetime_fixture::entity(id);
    const bool has_live_body = world.store<simulation::PhysicsBody>().find(target) != nullptr;
    CHECK((world.store<simulation::HillPresence>().find(target) != nullptr) == has_live_body);
    CHECK((world.store<simulation::ZoneExposure>().find(target) != nullptr) == has_live_body);
  }
}
