#include "component_store.hpp"
#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

using ScoreStore = simulation::ComponentStore<simulation::Score>;

[[nodiscard]] ScoreStore::Entry entry(const simulation::EntityId::Value id,
                                      const std::int64_t points) {
  return ScoreStore::Entry{simulation::EntityId::create(id), simulation::Score{points}};
}

[[nodiscard]] std::vector<simulation::EntityId::Value> entity_values(const ScoreStore& store) {
  std::vector<simulation::EntityId::Value> values;
  values.reserve(store.size());
  for (const ScoreStore::Entry& stored : store.entries()) {
    values.push_back(stored.entity.value());
  }
  return values;
}

} // namespace

TEST_CASE("ComponentStore canonicalizes caller order into ascending EntityId order",
          "[unit][simulation][component_store]") {
  const ScoreStore store = ScoreStore::create({entry(9, 90), entry(2, 20), entry(5, 50)});

  CHECK(entity_values(store) == std::vector<simulation::EntityId::Value>{2, 5, 9});
  CHECK(store.size() == 3);
  CHECK_FALSE(store.empty());
  CHECK(store.entries()[0].value == simulation::Score{20});
}

TEST_CASE("ComponentStore rejects a duplicate EntityId with a named validation code",
          "[unit][simulation][component_store][validation]") {
  try {
    static_cast<void>(ScoreStore::create({entry(4, 1), entry(4, 2)}));
    FAIL("duplicate component entry was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kComponentStoreDuplicateEntityId);
    CHECK(error.code() == std::string_view{"SIMULATION.COMPONENT_STORE_DUPLICATE_ENTITY_ID"});
    CHECK(error.context() == "component_store.entries.entity_id");
  }
}

TEST_CASE("ComponentStore rejects an entry count above the accepted world limit",
          "[unit][simulation][component_store][validation]") {
  std::vector<ScoreStore::Entry> too_many_entries;
  too_many_entries.reserve(simulation::kMaximumEntityCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumEntityCount; ++index) {
    too_many_entries.push_back(entry(static_cast<simulation::EntityId::Value>(index + 1), 0));
  }

  try {
    static_cast<void>(ScoreStore::create(std::move(too_many_entries)));
    FAIL("oversized component store was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kComponentStoreLimitExceeded);
    CHECK(error.code() == std::string_view{"SIMULATION.COMPONENT_STORE_LIMIT_EXCEEDED"});
  }
}

TEST_CASE("ComponentStore find returns the stored component and nullptr for an absent entity",
          "[unit][simulation][component_store]") {
  const ScoreStore store = ScoreStore::create({entry(2, 20), entry(9, 90)});

  REQUIRE(store.find(simulation::EntityId::create(9)) != nullptr);
  CHECK(store.find(simulation::EntityId::create(9))->points == 90);
  CHECK(store.find(simulation::EntityId::create(1)) == nullptr);
  CHECK(store.find(simulation::EntityId::create(5)) == nullptr);
  CHECK(store.find(simulation::EntityId::create(20)) == nullptr);
}

TEST_CASE("ComponentStore insert_or_assign replaces an existing entity without reordering",
          "[unit][simulation][component_store]") {
  ScoreStore store = ScoreStore::create({entry(2, 20), entry(9, 90)});

  store.insert_or_assign(simulation::EntityId::create(9), simulation::Score{99});

  CHECK(entity_values(store) == std::vector<simulation::EntityId::Value>{2, 9});
  REQUIRE(store.find(simulation::EntityId::create(9)) != nullptr);
  CHECK(store.find(simulation::EntityId::create(9))->points == 99);
}

TEST_CASE("ComponentStore insert_or_assign seats a new entity at its ascending position",
          "[unit][simulation][component_store]") {
  ScoreStore store = ScoreStore::create({entry(2, 20), entry(9, 90)});

  store.insert_or_assign(simulation::EntityId::create(5), simulation::Score{50});
  store.insert_or_assign(simulation::EntityId::create(1), simulation::Score{10});
  store.insert_or_assign(simulation::EntityId::create(12), simulation::Score{120});

  CHECK(entity_values(store) == std::vector<simulation::EntityId::Value>{1, 2, 5, 9, 12});
}

TEST_CASE("ComponentStore insert_or_assign rejects growth beyond the accepted world limit",
          "[unit][simulation][component_store][validation]") {
  std::vector<ScoreStore::Entry> maximum_entries;
  maximum_entries.reserve(simulation::kMaximumEntityCount);
  for (std::size_t index = 0; index < simulation::kMaximumEntityCount; ++index) {
    maximum_entries.push_back(entry(static_cast<simulation::EntityId::Value>(index + 1), 0));
  }
  ScoreStore store = ScoreStore::create(std::move(maximum_entries));

  CHECK_THROWS_AS(
      store.insert_or_assign(simulation::EntityId::create(simulation::kMaximumEntityCount + 1),
                             simulation::Score{1}),
      simulation::SimulationValidationError);
  CHECK_NOTHROW(store.insert_or_assign(simulation::EntityId::create(1), simulation::Score{1}));
}

TEST_CASE("ComponentStore erase removes one entity and leaves the remaining order intact",
          "[unit][simulation][component_store]") {
  ScoreStore store = ScoreStore::create({entry(2, 20), entry(5, 50), entry(9, 90)});

  store.erase(simulation::EntityId::create(5));

  CHECK(entity_values(store) == std::vector<simulation::EntityId::Value>{2, 9});
  CHECK(store.find(simulation::EntityId::create(5)) == nullptr);
}

TEST_CASE("ComponentStore erase of an absent entity leaves the store unchanged",
          "[unit][simulation][component_store]") {
  ScoreStore store = ScoreStore::create({entry(2, 20), entry(9, 90)});
  const ScoreStore unchanged = store;

  store.erase(simulation::EntityId::create(404));

  CHECK(store == unchanged);
}

TEST_CASE("ComponentStore equality compares entities and component values structurally",
          "[unit][simulation][component_store]") {
  const ScoreStore store = ScoreStore::create({entry(2, 20), entry(9, 90)});

  CHECK(store == ScoreStore::create({entry(9, 90), entry(2, 20)}));
  CHECK(store != ScoreStore::create({entry(2, 20), entry(9, 91)}));
  CHECK(store != ScoreStore::create({entry(2, 20)}));
  CHECK(ScoreStore{} == ScoreStore::create({}));
  CHECK(ScoreStore{}.empty());
}

TEST_CASE("ComponentStore mutable_values rewrites every value in ascending EntityId order",
          "[unit][simulation][component_store]") {
  ScoreStore store = ScoreStore::create({entry(9, 90), entry(2, 20), entry(5, 50)});

  std::int64_t visited_in_order = 0;
  for (simulation::Score& value : store.mutable_values()) {
    visited_in_order = (visited_in_order * 100) + value.points;
    value.points += 1;
  }

  // 20, then 50, then 90: the values arrive in the store's ascending key order without the caller
  // ever seeing a key.
  CHECK(visited_in_order == 205090);
  CHECK(store.entries()[0].value == simulation::Score{21});
  CHECK(store.entries()[1].value == simulation::Score{51});
  CHECK(store.entries()[2].value == simulation::Score{91});
  // The keys are untouched, which is the invariant `mutable_values` exists to make unbreakable:
  // there is no key to write (engine review finding 8).
  CHECK(entity_values(store) == std::vector<simulation::EntityId::Value>{2, 5, 9});
}

TEST_CASE("ComponentStore mutable_values over an empty store visits nothing",
          "[unit][simulation][component_store]") {
  ScoreStore store;

  std::size_t visits = 0;
  for (simulation::Score& value : store.mutable_values()) {
    static_cast<void>(value);
    ++visits;
  }

  CHECK(visits == 0);
}

TEST_CASE("ComponentStore mutable_find rewrites one value and leaves the order intact",
          "[unit][simulation][component_store]") {
  ScoreStore store = ScoreStore::create({entry(2, 20), entry(5, 50), entry(9, 90)});

  simulation::Score* found = store.mutable_find(simulation::EntityId::create(5));
  REQUIRE(found != nullptr);
  found->points = -7;

  CHECK(store.find(simulation::EntityId::create(5))->points == -7);
  CHECK(entity_values(store) == std::vector<simulation::EntityId::Value>{2, 5, 9});
  // Total: an id the store does not hold is a defined absence rather than a caller precondition.
  CHECK(store.mutable_find(simulation::EntityId::create(404)) == nullptr);
  CHECK(ScoreStore{}.mutable_find(simulation::EntityId::create(1)) == nullptr);
}

TEST_CASE("A value rewritten in place is still found by the binary search over the same keys",
          "[unit][simulation][component_store]") {
  // The point of finding 8: every phase's ordering and every `find` rests on the ascending keys, so
  // a full pass of value rewrites must leave every lookup answering exactly as before.
  ScoreStore store = ScoreStore::create({entry(1, 1), entry(4, 4), entry(6, 6), entry(11, 11)});

  for (simulation::Score& value : store.mutable_values()) {
    value.points *= -1;
  }

  CHECK(store.find(simulation::EntityId::create(1))->points == -1);
  CHECK(store.find(simulation::EntityId::create(4))->points == -4);
  CHECK(store.find(simulation::EntityId::create(6))->points == -6);
  CHECK(store.find(simulation::EntityId::create(11))->points == -11);
  CHECK(store.find(simulation::EntityId::create(5)) == nullptr);
}
