#include "observation.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"
#include "fixtures/racer_observation_fixture.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

template <typename Value>
concept RvalueTerrainReadable = requires(Value&& value) { std::move(value).terrain(); };

static_assert(std::is_same_v<decltype(std::declval<const controllers::Observation&>().terrain()),
                             const simulation::TerrainDefinition&>);
static_assert(!RvalueTerrainReadable<controllers::Observation>);
static_assert(!RvalueTerrainReadable<const controllers::Observation>);

constexpr std::uint64_t kFirstController = simulation::kMinimumControllerId;
constexpr std::uint64_t kSecondController = simulation::kMinimumControllerId + 1;
constexpr std::uint64_t kUnseatedController = 900;

} // namespace

TEST_CASE("Observation resolves the body a controller drives from the published controller link",
          "[unit][controllers][observation]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);

  const controllers::Observation first = fixture.observation_for(kFirstController);
  const controllers::Observation second = fixture.observation_for(kSecondController);

  REQUIRE(first.has_live_entity());
  REQUIRE(second.has_live_entity());
  CHECK(first.controller() == simulation::ControllerId::create(kFirstController));
  CHECK(*first.entity() != *second.entity());

  // The resolution is the published `Controllable` link and nothing else, so the entity the
  // observation names is the entity whose component carries this controller's id.
  const std::shared_ptr<const simulation::WorldSnapshot> snapshot = fixture.snapshot();
  std::optional<simulation::ControllerId> published_link;
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       snapshot->components<simulation::Controllable>()) {
    if (entry.entity == *first.entity()) {
      published_link = entry.value.controller_id;
    }
  }
  REQUIRE(published_link.has_value());
  CHECK(*published_link == simulation::ControllerId::create(kFirstController));
}

TEST_CASE("Observation reports no entity for a controller that carries no body",
          "[unit][controllers][observation]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);

  const controllers::Observation pending = fixture.observation_for(kUnseatedController);

  // Pending or eliminated is a defined state, not a lookup failure: nothing threw and the answer is
  // an empty optional.
  CHECK_FALSE(pending.has_live_entity());
  CHECK(pending.entity() == std::nullopt);
  CHECK(pending.controller() == simulation::ControllerId::create(kUnseatedController));
}

TEST_CASE("Observation carries the published tick sequence of the world it observes",
          "[unit][controllers][observation]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);

  const controllers::Observation observation = fixture.observation_for(kFirstController);

  CHECK(observation.tick_sequence() == observation.snapshot().tick_sequence());
  CHECK(observation.tick_sequence() == simulation::TickSequence::create(1));
}

TEST_CASE("Observation retains the exact published snapshot rather than a copy of it",
          "[unit][controllers][observation]") {
  const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const std::shared_ptr<const simulation::WorldSnapshot> published = fixture.publication().latest();

  const controllers::Observation observation = controllers::Observation::create(
      published, simulation::ControllerId::create(kFirstController));

  // Pointer identity, not value equality: an asynchronous controller may carry the observation to
  // another thread, and what keeps that world alive underneath it is the shared handle.
  CHECK(observation.retained_snapshot() == published);
  CHECK(&observation.snapshot() == published.get());
}

TEST_CASE("Observation retains the actual terrain after the publisher and simulation are destroyed",
          "[unit][controllers][observation][terrain]") {
  const simulation::TerrainDefinition* original_terrain = nullptr;
  const controllers::Observation observation = [&original_terrain] {
    const testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
    original_terrain = &fixture.game().map().terrain();
    return fixture.observation_for(kFirstController);
  }();

  CHECK(&observation.terrain() == original_terrain);
  CHECK(&observation.terrain() == &observation.snapshot().terrain());
  const simulation::MapDefinition expected = testing::controllers_map_of(4);
  CHECK(observation.terrain() == expected.terrain());
}

TEST_CASE("Racer observation fixtures publish their exact authored straight and bent roads",
          "[unit][controllers][observation][terrain][racer]") {
  for (const simulation::RaceModeState& course :
       {testing::straight_racer_course(), testing::bent_racer_course()}) {
    const controllers::Observation observation = testing::racer_observation(
        testing::racer_observation_world(simulation::Vector2::create(200.0, 320.0), 0, course));
    REQUIRE(observation.terrain().ground() == simulation::TerrainGround::kCorridors);
    REQUIRE(observation.terrain().corridors().size() == 1);
    const simulation::TerrainCorridor& road = observation.terrain().corridors().front();
    CHECK(road.name() == "road");
    CHECK(road.half_width() == 80.0);
    CHECK(road.half_width() == course.track_half_width);
    CHECK(std::ranges::equal(road.points(), course.track));
    CHECK(observation.terrain().holes().empty());
    CHECK(std::get<simulation::RaceModeState>(observation.snapshot().match().mode_state()) ==
          course);
    CHECK(observation.tick_sequence() == simulation::TickSequence::zero());
  }
}

TEST_CASE("Observation refuses an absent snapshot with a named code",
          "[unit][controllers][observation]") {
  try {
    static_cast<void>(controllers::Observation::create(
        nullptr, simulation::ControllerId::create(kFirstController)));
    FAIL("Observation::create must refuse a null snapshot");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kObservationSnapshotAbsent);
    CHECK(error.code() == "CONTROLLERS.OBSERVATION_SNAPSHOT_ABSENT");
    CHECK(error.context() == "observation.snapshot");
  }
}
