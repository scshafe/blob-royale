#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
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
