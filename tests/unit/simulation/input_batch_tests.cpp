#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/despawn_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "input_batch.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::Command spawn(const simulation::ControllerId::Value controller) {
  return simulation::Command{simulation::SpawnCommand{simulation::ControllerId::create(controller)}};
}

[[nodiscard]] simulation::Command despawn(const simulation::EntityId::Value entity) {
  return simulation::Command{simulation::DespawnCommand{simulation::EntityId::create(entity)}};
}

[[nodiscard]] simulation::Command thrust(const simulation::EntityId::Value entity, const double x,
                                         const double y) {
  return simulation::Command{simulation::ThrustCommand{simulation::EntityId::create(entity),
                                                       simulation::Vector2::create(x, y)}};
}

// "<wire name>:<addressed identity>", which is exactly what the canonical order is stated over.
[[nodiscard]] std::string describe(const simulation::Command& command) {
  const std::string name{
      simulation::command_kind_name_of(simulation::command_kind_of(command))};
  if (const auto* spawn_command = std::get_if<simulation::SpawnCommand>(&command);
      spawn_command != nullptr) {
    return name + ":" + std::to_string(spawn_command->controller.value());
  }
  if (const auto* despawn_command = std::get_if<simulation::DespawnCommand>(&command);
      despawn_command != nullptr) {
    return name + ":" + std::to_string(despawn_command->entity.value());
  }
  return name + ":" +
         std::to_string(std::get<simulation::ThrustCommand>(command).entity.value());
}

[[nodiscard]] std::vector<std::string> describe(const simulation::InputBatch& batch) {
  std::vector<std::string> described;
  described.reserve(batch.commands().size());
  for (const simulation::Command& command : batch.commands()) {
    described.push_back(describe(command));
  }
  return described;
}

// A reservation far above every entity id these tests despawn, so only the tests that mean to
// address an unissued id collide with it.
[[nodiscard]] simulation::EntityIdReservation reservation() {
  return simulation::EntityIdReservation::create(simulation::EntityId::create(1'000), 4);
}

} // namespace

TEST_CASE("InputBatch canonicalizes submitted order into despawns, spawns, then remaining kinds",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(7, 0.0, 1.0), spawn(3), despawn(5)}, simulation::CommandKindMask::all(),
      reservation());

  CHECK(describe(batch) == std::vector<std::string>{"despawn:5", "spawn:3", "thrust:7"});
}

TEST_CASE("InputBatch orders each kind ascending by the entity it addresses",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(9, 0.0, 1.0), despawn(8), thrust(2, 0.0, 1.0), despawn(4), thrust(5, 0.0, 1.0)},
      simulation::CommandKindMask::all(), reservation());

  CHECK(describe(batch) == std::vector<std::string>{"despawn:4", "despawn:8", "thrust:2",
                                                    "thrust:5", "thrust:9"});
}

TEST_CASE("InputBatch orders spawns ascending by ControllerId as one group after the despawns",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {spawn(9), thrust(6, 1.0, 0.0), spawn(2), despawn(3), spawn(5)},
      simulation::CommandKindMask::all(), reservation());

  CHECK(describe(batch) ==
        std::vector<std::string>{"despawn:3", "spawn:2", "spawn:5", "spawn:9", "thrust:6"});
}

TEST_CASE("InputBatch keeps the last submitted command of a kind for an entity",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(5, 1.0, 0.0), thrust(5, 0.0, 1.0), thrust(5, -1.0, 0.0)},
      simulation::CommandKindMask::all(), reservation());

  REQUIRE(batch.commands().size() == 1);
  CHECK(batch.commands()[0] == thrust(5, -1.0, 0.0));
}

TEST_CASE("InputBatch keeps the last submitted spawn for a controller",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch =
      simulation::InputBatch::create({spawn(4), spawn(4)}, simulation::CommandKindMask::all(),
                                     reservation());

  CHECK(describe(batch) == std::vector<std::string>{"spawn:4"});
}

TEST_CASE("InputBatch keeps one command of each kind for the same entity",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(5, 1.0, 0.0), despawn(5)}, simulation::CommandKindMask::all(), reservation());

  CHECK(describe(batch) == std::vector<std::string>{"despawn:5", "thrust:5"});
}

TEST_CASE("InputBatch de-duplicates one entity without disturbing another entity's command",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(5, 1.0, 0.0), thrust(2, 0.5, 0.0), thrust(5, 0.0, -1.0)},
      simulation::CommandKindMask::all(), reservation());

  REQUIRE(batch.commands().size() == 2);
  CHECK(batch.commands()[0] == thrust(2, 0.5, 0.0));
  CHECK(batch.commands()[1] == thrust(5, 0.0, -1.0));
}

TEST_CASE("InputBatch rejects a despawn naming an id inside the tick's own reservation",
          "[unit][simulation][input_batch][validation]") {
  try {
    static_cast<void>(simulation::InputBatch::create({spawn(3), despawn(1'001)},
                                                     simulation::CommandKindMask::all(),
                                                     reservation()));
    FAIL("a batch that both spawns and despawns in one tick was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kInputBatchSpawnAndDespawnConflict);
    CHECK(error.code() == std::string_view{"SIMULATION.INPUT_BATCH_SPAWN_AND_DESPAWN_CONFLICT"});
    CHECK(error.context() == "input_batch.commands.despawn.entity");
  }
}

TEST_CASE("InputBatch accepts a despawn naming an id the tick's reservation would never issue",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {spawn(3), despawn(999)}, simulation::CommandKindMask::all(), reservation());

  CHECK(describe(batch) == std::vector<std::string>{"despawn:999", "spawn:3"});
}

TEST_CASE("InputBatch rejects a command whose kind the mode does not accept",
          "[unit][simulation][input_batch][validation]") {
  const simulation::CommandKindMask thrust_only =
      simulation::CommandKindMask::none().with(simulation::CommandKind::kThrust);

  try {
    static_cast<void>(simulation::InputBatch::create({thrust(5, 1.0, 0.0), spawn(3)}, thrust_only,
                                                     reservation()));
    FAIL("a command kind absent from the accepted set was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kInputBatchCommandKindNotAccepted);
    CHECK(error.code() == std::string_view{"SIMULATION.INPUT_BATCH_COMMAND_KIND_NOT_ACCEPTED"});
    CHECK(error.context() == "input_batch.commands.kind");
  }
}

TEST_CASE("InputBatch accepts every command whose kind the mode does accept",
          "[unit][simulation][input_batch]") {
  const simulation::CommandKindMask thrust_only =
      simulation::CommandKindMask::none().with(simulation::CommandKind::kThrust);
  const simulation::InputBatch batch =
      simulation::InputBatch::create({thrust(5, 1.0, 0.0)}, thrust_only, reservation());

  CHECK(describe(batch) == std::vector<std::string>{"thrust:5"});
}

TEST_CASE("InputBatch rejects a thrust direction component outside the unit interval",
          "[unit][simulation][input_batch][validation]") {
  try {
    static_cast<void>(simulation::InputBatch::create(
        {thrust(5, 1.5, 0.0)}, simulation::CommandKindMask::all(), reservation()));
    FAIL("an out-of-range thrust direction was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kInputBatchThrustDirectionOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.INPUT_BATCH_THRUST_DIRECTION_OUT_OF_RANGE"});
    CHECK(error.context() == "input_batch.commands.thrust.direction");
  }
}

TEST_CASE("InputBatch rejects an out-of-range thrust direction on either component",
          "[unit][simulation][input_batch][validation]") {
  const auto rejects = [](const double x, const double y) {
    try {
      static_cast<void>(simulation::InputBatch::create(
          {thrust(5, x, y)}, simulation::CommandKindMask::all(), reservation()));
      return false;
    } catch (const simulation::SimulationValidationError& error) {
      return error.validation_code() ==
             simulation::SimulationValidationCode::kInputBatchThrustDirectionOutOfRange;
    }
  };

  CHECK(rejects(0.0, -1.000000001));
  CHECK(rejects(-2.0, 0.0));
  CHECK_FALSE(rejects(-1.0, 1.0));
}

TEST_CASE("A non-finite thrust direction cannot be built, so it never reaches InputBatch",
          "[unit][simulation][input_batch][validation]") {
  try {
    static_cast<void>(
        simulation::Vector2::create(std::numeric_limits<double>::quiet_NaN(), 0.0));
    FAIL("a non-finite thrust direction was built");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicalScalarNotFinite);
    CHECK(error.code() == std::string_view{"SIMULATION.PHYSICAL_SCALAR_NOT_FINITE"});
  }
}

TEST_CASE("InputBatch carries an over-long thrust direction verbatim for the steering system",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(5, 1.0, 1.0)}, simulation::CommandKindMask::all(), reservation());

  REQUIRE(batch.commands().size() == 1);
  CHECK(std::get<simulation::ThrustCommand>(batch.commands()[0]).direction ==
        simulation::Vector2::create(1.0, 1.0));
}

TEST_CASE("InputBatch carries a thrust direction inside the unit disc unchanged",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {thrust(5, 0.25, -0.5)}, simulation::CommandKindMask::all(), reservation());

  REQUIRE(batch.commands().size() == 1);
  CHECK(std::get<simulation::ThrustCommand>(batch.commands()[0]).direction ==
        simulation::Vector2::create(0.25, -0.5));
}

TEST_CASE("InputBatch empty is the no-input tick with no command and no reservation",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::empty();

  CHECK(batch.commands().empty());
  CHECK(batch.entity_id_reservation() == simulation::EntityIdReservation::none());
}

TEST_CASE("InputBatch created from no commands equals the empty batch",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {}, simulation::CommandKindMask::all(), simulation::EntityIdReservation::none());

  CHECK(batch == simulation::InputBatch::empty());
}

TEST_CASE("InputBatch carries the tick's EntityIdReservation unchanged",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {spawn(3)}, simulation::CommandKindMask::all(), reservation());

  CHECK(batch.entity_id_reservation() == reservation());
  CHECK(batch.entity_id_reservation().count() == 4);
}

TEST_CASE("Drawing from the reservation a batch hands out leaves the batch unchanged",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch batch = simulation::InputBatch::create(
      {spawn(3)}, simulation::CommandKindMask::all(), reservation());

  simulation::EntityIdReservation drawn_from = batch.entity_id_reservation();
  static_cast<void>(drawn_from.draw_next());

  CHECK(batch.entity_id_reservation() == reservation());
}

TEST_CASE("InputBatch rejects a submitted command count above the accepted limit",
          "[unit][simulation][input_batch][validation]") {
  std::vector<simulation::Command> too_many_commands;
  too_many_commands.reserve(simulation::kMaximumInputBatchCommandCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumInputBatchCommandCount; ++index) {
    too_many_commands.push_back(thrust(1, 0.0, 0.0));
  }

  try {
    static_cast<void>(simulation::InputBatch::create(
        std::move(too_many_commands), simulation::CommandKindMask::all(), reservation()));
    FAIL("an oversized command batch was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kInputBatchCommandLimitExceeded);
    CHECK(error.code() == std::string_view{"SIMULATION.INPUT_BATCH_COMMAND_LIMIT_EXCEEDED"});
    CHECK(error.context() == "input_batch.commands");
  }
}

TEST_CASE("InputBatch canonicalization is a function of the submitted set, not its order",
          "[unit][simulation][input_batch]") {
  const simulation::InputBatch forward = simulation::InputBatch::create(
      {despawn(4), spawn(2), thrust(6, 1.0, 0.0), thrust(3, 0.0, 1.0)},
      simulation::CommandKindMask::all(), reservation());
  const simulation::InputBatch reversed = simulation::InputBatch::create(
      {thrust(3, 0.0, 1.0), thrust(6, 1.0, 0.0), spawn(2), despawn(4)},
      simulation::CommandKindMask::all(), reservation());

  CHECK(forward == reversed);
}
