#include "candidate_pair.hpp"
#include "contact_rule_name.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::EntityId entity(const simulation::EntityId::Value value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::ContactEvent contact_event() {
  return simulation::ContactEvent{simulation::CandidatePair::create(entity(2), entity(7)),
                                  simulation::Vector2::create(1.0, 0.0), -3.5,
                                  simulation::ContactRuleName::create("elastic_disc")};
}

[[nodiscard]] std::vector<simulation::WorldEvent> one_of_each_kind() {
  return {contact_event(), simulation::SpawnEvent{entity(4), simulation::ControllerId::create(11)},
          simulation::DespawnEvent{entity(5)}, simulation::EliminationEvent{entity(6)},
          simulation::ScoreEvent{entity(7), -2}};
}

} // namespace

TEST_CASE("the WorldEvent variant carries exactly the declared event kinds",
          "[unit][simulation][world_event_registry]") {
  STATIC_REQUIRE(std::variant_size_v<simulation::WorldEvent> == simulation::kWorldEventKindCount);
  STATIC_REQUIRE(simulation::kWorldEventKindCount == 5);
  CHECK(simulation::kWorldEventKinds[0] == simulation::WorldEventKind::kContact);
  CHECK(simulation::kWorldEventKinds[1] == simulation::WorldEventKind::kSpawn);
  CHECK(simulation::kWorldEventKinds[2] == simulation::WorldEventKind::kDespawn);
  CHECK(simulation::kWorldEventKinds[3] == simulation::WorldEventKind::kElimination);
  CHECK(simulation::kWorldEventKinds[4] == simulation::WorldEventKind::kScore);
}

TEST_CASE("every WorldEvent alternative answers with its own kind and name",
          "[unit][simulation][world_event_registry]") {
  const std::vector<simulation::WorldEvent> events = one_of_each_kind();
  REQUIRE(events.size() == simulation::kWorldEventKindCount);

  for (std::size_t index = 0; index < events.size(); ++index) {
    INFO("declared event kind " << index);
    CHECK(simulation::world_event_kind_of(events[index]) == simulation::kWorldEventKinds[index]);
  }
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kContact) == "contact");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kSpawn) == "spawn");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kDespawn) == "despawn");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kElimination) ==
        "elimination");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kScore) == "score");
}

TEST_CASE("a WorldEvent is a comparable value that is never valueless by exception",
          "[unit][simulation][world_event_registry]") {
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::WorldEvent>);

  const simulation::WorldEvent first = contact_event();
  const simulation::WorldEvent same = contact_event();
  const simulation::WorldEvent other = simulation::DespawnEvent{entity(5)};

  CHECK(first == same);
  CHECK_FALSE(first == other);
  CHECK_FALSE(first.valueless_by_exception());
}

TEST_CASE("ContactEvent carries the canonical pair, the normal, the speed, and the matched rule",
          "[unit][simulation][world_event_registry]") {
  const simulation::ContactEvent event = contact_event();

  CHECK(event.pair.lower_id() == entity(2));
  CHECK(event.pair.higher_id() == entity(7));
  CHECK(event.normal == simulation::Vector2::create(1.0, 0.0));
  CHECK(event.relative_normal_speed == -3.5);
  // The name is owned rather than borrowed: the event outlives the row that produced it within a
  // tick, and a mode may build a row from a temporary string.
  CHECK(event.rule_name == std::string_view("elastic_disc"));
  CHECK(event.rule_name.value() == "elastic_disc");
  CHECK_FALSE(event.rule_name == simulation::ContactRuleName::create("reflect_static"));
  CHECK(simulation::ContactRuleName{}.empty());
}

TEST_CASE("a ScoreEvent is a signed delta against the entity owning the scoreboard cell",
          "[unit][simulation][world_event_registry]") {
  const simulation::ScoreEvent penalty{entity(3), -5};
  const simulation::ScoreEvent award{entity(3), 5};

  CHECK(penalty.entity == award.entity);
  CHECK(penalty.points == -5);
  CHECK_FALSE(penalty == award);
}
