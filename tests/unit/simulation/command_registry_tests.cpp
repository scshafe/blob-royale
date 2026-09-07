#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::Command spawn_command(const simulation::ControllerId::Value controller) {
  return simulation::Command{
      simulation::SpawnCommand{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] simulation::Command despawn_command(const simulation::EntityId::Value entity) {
  return simulation::Command{simulation::DespawnCommand{simulation::EntityId::create(entity)}};
}

[[nodiscard]] simulation::Command thrust_command(const simulation::EntityId::Value entity,
                                                 const double x, const double y) {
  return simulation::Command{simulation::ThrustCommand{simulation::EntityId::create(entity),
                                                       simulation::Vector2::create(x, y)}};
}

} // namespace

TEST_CASE("CommandRegistry declares the engine command kinds in a closed ordered variant",
          "[unit][simulation][command_registry]") {
  STATIC_REQUIRE(std::variant_size_v<simulation::Command> == 3);
  STATIC_REQUIRE(simulation::kCommandKindCount == 3);
  STATIC_REQUIRE(std::is_same_v<simulation::Command,
                                std::variant<simulation::SpawnCommand, simulation::DespawnCommand,
                                             simulation::ThrustCommand>>);
}

TEST_CASE("Every command kind occupies its own bit so a set of kinds is one integer",
          "[unit][simulation][command_registry]") {
  STATIC_REQUIRE(static_cast<std::uint32_t>(simulation::CommandKind::kSpawn) == 1u);
  STATIC_REQUIRE(static_cast<std::uint32_t>(simulation::CommandKind::kDespawn) == 2u);
  STATIC_REQUIRE(static_cast<std::uint32_t>(simulation::CommandKind::kThrust) == 4u);
}

TEST_CASE("Every registered command kind declares its own wire name",
          "[unit][simulation][command_registry]") {
  STATIC_REQUIRE(simulation::command_kind_name<simulation::SpawnCommand> ==
                 std::string_view{"spawn"});
  STATIC_REQUIRE(simulation::command_kind_name<simulation::DespawnCommand> ==
                 std::string_view{"despawn"});
  STATIC_REQUIRE(simulation::command_kind_name<simulation::ThrustCommand> ==
                 std::string_view{"thrust"});

  std::vector<std::string_view> names;
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    names.push_back(simulation::command_kind_name_of(kind));
  }

  CHECK(names == std::vector<std::string_view>{"spawn", "despawn", "thrust"});
}

TEST_CASE("command_kind_of maps every command value to its own declared kind",
          "[unit][simulation][command_registry]") {
  CHECK(simulation::command_kind_of(spawn_command(4)) == simulation::CommandKind::kSpawn);
  CHECK(simulation::command_kind_of(despawn_command(4)) == simulation::CommandKind::kDespawn);
  CHECK(simulation::command_kind_of(thrust_command(4, 0.0, 0.0)) ==
        simulation::CommandKind::kThrust);
}

TEST_CASE("Command application ranks are the phase 0 order of despawn, spawn, then remaining kinds",
          "[unit][simulation][command_registry]") {
  STATIC_REQUIRE(simulation::command_kind_application_rank(simulation::CommandKind::kDespawn) <
                 simulation::command_kind_application_rank(simulation::CommandKind::kSpawn));
  STATIC_REQUIRE(simulation::command_kind_application_rank(simulation::CommandKind::kSpawn) <
                 simulation::command_kind_application_rank(simulation::CommandKind::kThrust));
}

TEST_CASE("Every command kind is a comparable value struct",
          "[unit][simulation][command_registry]") {
  CHECK(spawn_command(4) == spawn_command(4));
  CHECK(spawn_command(4) != spawn_command(5));
  CHECK(despawn_command(4) == despawn_command(4));
  CHECK(despawn_command(4) != despawn_command(5));
  CHECK(thrust_command(4, 0.25, -0.5) == thrust_command(4, 0.25, -0.5));
  CHECK(thrust_command(4, 0.25, -0.5) != thrust_command(4, 0.25, 0.5));
  CHECK(thrust_command(4, 0.25, -0.5) != thrust_command(5, 0.25, -0.5));
}

TEST_CASE("A spawn addresses its controller and every other kind addresses its entity",
          "[unit][simulation][command_registry]") {
  const simulation::SpawnCommand spawn{simulation::ControllerId::create(7)};
  const simulation::DespawnCommand despawn{simulation::EntityId::create(9)};
  const simulation::ThrustCommand thrust{simulation::EntityId::create(11),
                                         simulation::Vector2::create(1.0, 0.0)};

  CHECK(spawn.controller.value() == 7);
  CHECK(despawn.entity.value() == 9);
  CHECK(thrust.entity.value() == 11);
  CHECK(thrust.direction == simulation::Vector2::create(1.0, 0.0));
}

TEST_CASE("addressed_identity_of is the one answer to which identity a command addresses",
          "[unit][simulation][command_registry]") {
  // One capability, one implementation: `InputBatch::create` reads `ordering_key()` and kernel
  // phase 0 reads `entity()`, and both read this function (engine review finding 5).
  const simulation::AddressedIdentity spawn = simulation::addressed_identity_of(spawn_command(7));
  CHECK_FALSE(spawn.entity().has_value());
  CHECK(spawn.ordering_key() == 7);

  const simulation::AddressedIdentity despawn =
      simulation::addressed_identity_of(despawn_command(9));
  REQUIRE(despawn.entity().has_value());
  CHECK(despawn.entity()->value() == 9);
  CHECK(despawn.ordering_key() == 9);

  const simulation::AddressedIdentity thrust =
      simulation::addressed_identity_of(thrust_command(11, 1.0, 0.0));
  REQUIRE(thrust.entity().has_value());
  CHECK(thrust.entity()->value() == 11);
  CHECK(thrust.ordering_key() == 11);

  // Two commands of one kind for one identity carry the same identity value, which is what makes
  // the batch's de-duplication a run over equal keys.
  CHECK(simulation::addressed_identity_of(thrust_command(11, 1.0, 0.0)) ==
        simulation::addressed_identity_of(thrust_command(11, -1.0, 0.5)));
  CHECK(simulation::addressed_identity_of(thrust_command(11, 1.0, 0.0)) !=
        simulation::addressed_identity_of(thrust_command(12, 1.0, 0.0)));

  // Two *different* kinds naming one entity carry the same identity on purpose: the identity is
  // kind-agnostic, and it is the injective application rank that separates the two groups. That is
  // why the batch groups by (rank, identity) and never by identity alone.
  CHECK(simulation::addressed_identity_of(thrust_command(11, 1.0, 0.0)) ==
        simulation::addressed_identity_of(despawn_command(11)));
  CHECK(simulation::command_kind_application_rank(simulation::CommandKind::kThrust) !=
        simulation::command_kind_application_rank(simulation::CommandKind::kDespawn));

  // The two identity spaces are never compared with each other: a spawn naming controller 9 and a
  // despawn naming entity 9 share an ordering key and are kept apart by their ranks alone.
  CHECK(simulation::addressed_identity_of(spawn_command(9)).ordering_key() ==
        simulation::addressed_identity_of(despawn_command(9)).ordering_key());
  CHECK(simulation::addressed_identity_of(spawn_command(9)) !=
        simulation::addressed_identity_of(despawn_command(9)));
}

TEST_CASE("The command kind list is derived from the variant rather than typed beside it",
          "[unit][simulation][command_registry]") {
  // Derivation, not maintenance: `kCommandKinds` reads CommandKindOf over every variant
  // alternative, so it can neither omit a kind nor carry a duplicate (engine review finding 7).
  STATIC_REQUIRE(simulation::kCommandKinds.size() == std::variant_size_v<simulation::Command>);
  STATIC_REQUIRE(simulation::values_are_distinct(simulation::kCommandKinds));
  STATIC_REQUIRE(simulation::kCommandKinds ==
                 simulation::kinds_of_variant<simulation::Command, simulation::CommandKindOf>());

  // Every registered kind is in the complete mask, which is the property a duplicated hand-typed
  // entry used to be able to break silently.
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    CHECK(simulation::CommandKindMask::all().contains(kind));
    CHECK(simulation::command_kind_name_of(kind) != std::string_view{"command_kind_invalid"});
  }
  CHECK(simulation::CommandKindMask::all().bits() ==
        (static_cast<simulation::CommandKindMask::Bits>(simulation::CommandKind::kSpawn) |
         static_cast<simulation::CommandKindMask::Bits>(simulation::CommandKind::kDespawn) |
         static_cast<simulation::CommandKindMask::Bits>(simulation::CommandKind::kThrust)));
}

TEST_CASE("No two command kinds share a phase 0 application rank",
          "[unit][simulation][command_registry]") {
  // The injectivity `InputBatch` depends on, derived over the whole kind list rather than written
  // out as three pairwise comparisons that a fourth kind would silently outgrow.
  STATIC_REQUIRE(simulation::values_are_distinct(simulation::projected_values(
      simulation::kCommandKinds, simulation::command_kind_application_rank)));

  std::vector<std::uint32_t> ranks;
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    ranks.push_back(simulation::command_kind_application_rank(kind));
  }
  std::sort(ranks.begin(), ranks.end());
  CHECK(std::adjacent_find(ranks.cbegin(), ranks.cend()) == ranks.cend());
  CHECK(ranks.size() == simulation::kCommandKindCount);
}
