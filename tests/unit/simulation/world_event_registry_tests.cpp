#include "candidate_pair.hpp"
#include "contact_rule_name.hpp"
#include "entity_id.hpp"
#include "kind_registry.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
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
  return {contact_event(), simulation::DespawnEvent{entity(5)},
          simulation::EliminationEvent{entity(6)}, simulation::StunRequest{entity(7), 3},
          simulation::RaceCheckpointEvent{entity(8), 1, simulation::MotionTime::create(0.25)}};
}

} // namespace

TEST_CASE("the WorldEvent variant carries exactly the declared event kinds",
          "[unit][simulation][world_event_registry]") {
  // StunRequest is the Step 14 foundation exception, with an injected in-tick test producer.
  STATIC_REQUIRE(std::variant_size_v<simulation::WorldEvent> == simulation::kWorldEventKindCount);
  STATIC_REQUIRE(simulation::kWorldEventKindCount == 5);
  CHECK(simulation::kWorldEventKinds[0] == simulation::WorldEventKind::kContact);
  CHECK(simulation::kWorldEventKinds[1] == simulation::WorldEventKind::kDespawn);
  CHECK(simulation::kWorldEventKinds[2] == simulation::WorldEventKind::kElimination);
  CHECK(simulation::kWorldEventKinds[3] == simulation::WorldEventKind::kStunRequest);
  CHECK(simulation::kWorldEventKinds[4] == simulation::WorldEventKind::kRaceCheckpoint);
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
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kDespawn) == "despawn");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kElimination) ==
        "elimination");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kStunRequest) ==
        "stun_request");
  CHECK(simulation::world_event_kind_name_of(simulation::WorldEventKind::kRaceCheckpoint) ==
        "race_checkpoint");
}

TEST_CASE("race checkpoint events retain the certified normalized time as part of value equality",
          "[unit][simulation][world_event_registry]") {
  const auto time = simulation::MotionTime::create(0.25);
  const simulation::RaceCheckpointEvent first{entity(8), 1, time};
  CHECK(first.tick_offset == time);
  CHECK(first == simulation::RaceCheckpointEvent{entity(8), 1, time});
  CHECK_FALSE(first ==
              simulation::RaceCheckpointEvent{entity(8), 1, simulation::MotionTime::create(0.5)});
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::RaceCheckpointEvent>);
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

TEST_CASE("an EliminationEvent names the entity and nothing else, so a consumer ranks the set",
          "[unit][simulation][world_event_registry]") {
  // Carrying only the entity is what lets `placement_recorder` compute one shared placement over
  // the whole tick's set rather than one per entity
  // (`docs/architecture/0005-royale-mode.md` § "Elimination and placement").
  const simulation::EliminationEvent first{entity(3)};
  const simulation::EliminationEvent same{entity(3)};
  const simulation::EliminationEvent other{entity(4)};

  CHECK(first == same);
  CHECK_FALSE(first == other);
}

TEST_CASE("The world event kind list is derived from the variant rather than typed beside it",
          "[unit][simulation][world_event_registry]") {
  // Derivation, not maintenance: `kWorldEventKinds` reads WorldEventKindOf over every variant
  // alternative, so it can neither omit a kind nor carry a duplicate (engine review finding 7).
  STATIC_REQUIRE(simulation::kWorldEventKinds.size() ==
                 std::variant_size_v<simulation::WorldEvent>);
  STATIC_REQUIRE(simulation::values_are_distinct(simulation::kWorldEventKinds));
  STATIC_REQUIRE(
      simulation::kWorldEventKinds ==
      simulation::kinds_of_variant<simulation::WorldEvent, simulation::WorldEventKindOf>());

  for (const simulation::WorldEventKind kind : simulation::kWorldEventKinds) {
    CHECK(simulation::world_event_kind_name_of(kind) !=
          std::string_view{"world_event_kind_invalid"});
  }
}

TEST_CASE("A derived kind list rejects a duplicated entry and accepts a distinct one",
          "[unit][simulation][world_event_registry]") {
  // The property the two registries rest on, stated over the helper itself: a list with a repeated
  // value is not distinct, which is what makes the static_assert beside each registry fire when a
  // new alternative copies a neighbour's enumerator.
  STATIC_REQUIRE(simulation::values_are_distinct(std::array<int, 3>{1, 2, 3}));
  STATIC_REQUIRE_FALSE(simulation::values_are_distinct(std::array<int, 3>{1, 2, 1}));
  STATIC_REQUIRE(simulation::values_are_distinct(std::array<int, 0>{}));
  STATIC_REQUIRE(simulation::values_are_distinct(std::array<int, 1>{7}));
  STATIC_REQUIRE(simulation::projected_values(std::array<int, 3>{1, 2, 3}, [](const int value) {
                   return value * 2;
                 }) == std::array<int, 3>{2, 4, 6});
}
