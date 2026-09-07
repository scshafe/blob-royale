#include "component_join.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/score_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "physics_body.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

using ScoreStore = simulation::ComponentStore<simulation::Score>;
using LifetimeStore = simulation::ComponentStore<simulation::Controllable>;
using BodyStore = simulation::ComponentStore<simulation::PhysicsBody>;

[[nodiscard]] ScoreStore scores(const std::vector<simulation::EntityId::Value>& ids) {
  std::vector<ScoreStore::Entry> entries;
  entries.reserve(ids.size());
  for (const simulation::EntityId::Value id : ids) {
    entries.push_back(ScoreStore::Entry{simulation::EntityId::create(id),
                                        simulation::Score{static_cast<std::int64_t>(id) * 10}});
  }
  return ScoreStore::create(std::move(entries));
}

[[nodiscard]] LifetimeStore controllables(const std::vector<simulation::EntityId::Value>& ids) {
  std::vector<LifetimeStore::Entry> entries;
  entries.reserve(ids.size());
  for (const simulation::EntityId::Value id : ids) {
    entries.push_back(
        LifetimeStore::Entry{simulation::EntityId::create(id),
                             simulation::Controllable{simulation::ControllerId::create(id + 100)}});
  }
  return LifetimeStore::create(std::move(entries));
}

[[nodiscard]] std::vector<simulation::EntityId::Value> joined_ids(const ScoreStore& first,
                                                                  const LifetimeStore& second) {
  std::vector<simulation::EntityId::Value> visited;
  simulation::for_each_entity_with_both(
      first, second,
      [&visited](const simulation::EntityId entity, const simulation::Score&,
                 const simulation::Controllable&) { visited.push_back(entity.value()); });
  return visited;
}

} // namespace

TEST_CASE("the two-store join visits exactly the entities carrying both kinds, ascending",
          "[unit][simulation][component_join]") {
  const ScoreStore first = scores({1, 3, 4, 7, 9});
  const LifetimeStore second = controllables({2, 3, 4, 8, 9});

  CHECK(joined_ids(first, second) == std::vector<simulation::EntityId::Value>{3, 4, 9});
}

TEST_CASE("the two-store join hands each visitor both values for the entity it names",
          "[unit][simulation][component_join]") {
  const ScoreStore first = scores({2, 5});
  const LifetimeStore second = controllables({5, 6});

  std::size_t visits = 0;
  simulation::for_each_entity_with_both(first, second,
                                        [&visits](const simulation::EntityId entity,
                                                  const simulation::Score& score,
                                                  const simulation::Controllable& controllable) {
                                          ++visits;
                                          CHECK(entity.value() == 5);
                                          CHECK(score.points == 50);
                                          CHECK(controllable.controller_id.value() == 105);
                                        });

  CHECK(visits == 1);
}

TEST_CASE("the two-store join is total over disjoint and empty stores",
          "[unit][simulation][component_join]") {
  CHECK(joined_ids(scores({1, 2, 3}), controllables({4, 5, 6})).empty());
  CHECK(joined_ids(scores({}), controllables({1, 2})).empty());
  CHECK(joined_ids(scores({1, 2}), controllables({})).empty());
  CHECK(joined_ids(ScoreStore{}, LifetimeStore{}).empty());
}

TEST_CASE("the two-store join visits every entity when one store contains the other",
          "[unit][simulation][component_join]") {
  CHECK(joined_ids(scores({2, 4}), controllables({1, 2, 3, 4, 5})) ==
        std::vector<simulation::EntityId::Value>{2, 4});
  CHECK(joined_ids(scores({1, 2, 3, 4, 5}), controllables({2, 4})) ==
        std::vector<simulation::EntityId::Value>{2, 4});
  CHECK(joined_ids(scores({1, 2, 3}), controllables({1, 2, 3})) ==
        std::vector<simulation::EntityId::Value>{1, 2, 3});
}

TEST_CASE("the two-store join answers the same question in either argument order",
          "[unit][simulation][component_join]") {
  const ScoreStore first = scores({1, 3, 4, 7, 9});
  const LifetimeStore second = controllables({2, 3, 4, 8, 9});

  std::vector<simulation::EntityId::Value> reversed;
  simulation::for_each_entity_with_both(
      second, first,
      [&reversed](const simulation::EntityId entity, const simulation::Controllable&,
                  const simulation::Score&) { reversed.push_back(entity.value()); });

  CHECK(reversed == joined_ids(first, second));
}

TEST_CASE("count_entities_with_both is the counting form of the same join",
          "[unit][simulation][component_join]") {
  CHECK(simulation::count_entities_with_both(scores({1, 3, 4, 7, 9}),
                                             controllables({2, 3, 4, 8, 9})) == 3);
  CHECK(simulation::count_entities_with_both(scores({1}), controllables({2})) == 0);
  CHECK(simulation::count_entities_with_both(ScoreStore{}, LifetimeStore{}) == 0);
}

TEST_CASE("the two-store join is the protocol v1 player projection",
          "[unit][simulation][component_join]") {
  // A player is an entity carrying both a PhysicsBody and a Controllable, which is the join
  // `WorldSnapshot::players()` performs and the join `thrust_steering` performs. Written here over
  // the two real kinds so the named helper is demonstrably the same answer both callers need.
  std::vector<BodyStore::Entry> bodies;
  for (const simulation::EntityId::Value id : {1U, 2U, 5U}) {
    bodies.push_back(BodyStore::Entry{
        simulation::EntityId::create(id),
        simulation::PhysicsBody::create(simulation::Vector2::create(10.0 * id, 20.0),
                                        simulation::Vector2::create(0.0, 0.0),
                                        simulation::Vector2::create(0.0, 0.0))});
  }
  const BodyStore body_store = BodyStore::create(std::move(bodies));
  const LifetimeStore controllable_store = controllables({2, 5, 8});

  std::vector<simulation::EntityId::Value> players;
  simulation::for_each_entity_with_both(
      body_store, controllable_store,
      [&players](const simulation::EntityId entity, const simulation::PhysicsBody& body,
                 const simulation::Controllable&) {
        CHECK(body.position().x() == 10.0 * static_cast<double>(entity.value()));
        players.push_back(entity.value());
      });

  CHECK(players == std::vector<simulation::EntityId::Value>{2, 5});
}
