#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace simulation = blob_royale::simulation;

TEST_CASE("EntityIdReservation carries the contiguous half-open block it was created with",
          "[unit][simulation][entity_id_reservation]") {
  const simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(10), 3);

  CHECK(reservation.count() == 3);
  CHECK_FALSE(reservation.empty());
  CHECK(reservation.first_entity_id() == simulation::EntityId::create(10));
  CHECK(reservation.contains(simulation::EntityId::create(10)));
  CHECK(reservation.contains(simulation::EntityId::create(12)));
  CHECK_FALSE(reservation.contains(simulation::EntityId::create(9)));
  CHECK_FALSE(reservation.contains(simulation::EntityId::create(13)));
}

TEST_CASE("EntityIdReservation none is the empty block that contains no id",
          "[unit][simulation][entity_id_reservation]") {
  const simulation::EntityIdReservation reservation = simulation::EntityIdReservation::none();

  CHECK(reservation.empty());
  CHECK(reservation.count() == 0);
  CHECK_FALSE(reservation.contains(simulation::EntityId::create(simulation::kMinimumEntityId)));
}

TEST_CASE("EntityIdReservation draws its ids in ascending order and never reissues one",
          "[unit][simulation][entity_id_reservation]") {
  simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(10), 3);

  std::vector<simulation::EntityId::Value> drawn;
  while (!reservation.empty()) {
    drawn.push_back(reservation.draw_next().value());
  }

  CHECK(drawn == std::vector<simulation::EntityId::Value>{10, 11, 12});
  CHECK(reservation.count() == 0);
}

TEST_CASE("EntityIdReservation shrinks by exactly one id per draw",
          "[unit][simulation][entity_id_reservation]") {
  simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(4), 2);

  CHECK(reservation.draw_next() == simulation::EntityId::create(4));
  CHECK(reservation.count() == 1);
  CHECK(reservation.first_entity_id() == simulation::EntityId::create(5));
  CHECK_FALSE(reservation.contains(simulation::EntityId::create(4)));
  CHECK(reservation.contains(simulation::EntityId::create(5)));
}

TEST_CASE("EntityIdReservation exhausted by drawing equals the empty reservation",
          "[unit][simulation][entity_id_reservation]") {
  simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(4), 1);
  static_cast<void>(reservation.draw_next());

  CHECK(reservation == simulation::EntityIdReservation::none());
}

TEST_CASE("EntityIdReservation exhaustion is a hard failure rather than a wrap or a reuse",
          "[unit][simulation][entity_id_reservation][validation]") {
  simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(4), 1);
  const simulation::EntityId drawn = reservation.draw_next();

  try {
    static_cast<void>(reservation.draw_next());
    FAIL("an exhausted reservation issued an id");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kEntityIdReservationExhausted);
    CHECK(error.code() == std::string_view{"SIMULATION.ENTITY_ID_RESERVATION_EXHAUSTED"});
    CHECK(error.context() == "entity_id_reservation.draw_next");
  }

  CHECK(drawn == simulation::EntityId::create(4));
  CHECK(reservation.empty());
}

TEST_CASE("EntityIdReservation none refuses to issue an id at all",
          "[unit][simulation][entity_id_reservation][validation]") {
  simulation::EntityIdReservation reservation = simulation::EntityIdReservation::none();

  try {
    static_cast<void>(reservation.draw_next());
    FAIL("the empty reservation issued an id");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kEntityIdReservationExhausted);
  }
}

TEST_CASE("EntityIdReservation rejects a block running past the maximum EntityId",
          "[unit][simulation][entity_id_reservation][validation]") {
  try {
    static_cast<void>(simulation::EntityIdReservation::create(
        simulation::EntityId::create(simulation::kMaximumEntityId), 2));
    FAIL("a reservation running past the maximum EntityId was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kEntityIdReservationOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.ENTITY_ID_RESERVATION_OUT_OF_RANGE"});
    CHECK(error.context() == "entity_id_reservation.first");
  }
}

TEST_CASE("EntityIdReservation accepts a block ending exactly at the maximum EntityId",
          "[unit][simulation][entity_id_reservation]") {
  simulation::EntityIdReservation reservation = simulation::EntityIdReservation::create(
      simulation::EntityId::create(simulation::kMaximumEntityId), 1);

  CHECK(reservation.draw_next() == simulation::EntityId::create(simulation::kMaximumEntityId));
  CHECK(reservation.empty());
}

TEST_CASE("EntityIdReservation rejects a count above the world's seat limit",
          "[unit][simulation][entity_id_reservation][validation]") {
  try {
    static_cast<void>(simulation::EntityIdReservation::create(
        simulation::EntityId::create(1), simulation::kMaximumEntityIdReservationCount + 1));
    FAIL("an oversized reservation was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kEntityIdReservationLimitExceeded);
    CHECK(error.code() == std::string_view{"SIMULATION.ENTITY_ID_RESERVATION_LIMIT_EXCEEDED"});
    CHECK(error.context() == "entity_id_reservation.count");
  }
}

TEST_CASE("EntityIdReservation created with a zero count is the empty reservation",
          "[unit][simulation][entity_id_reservation]") {
  const simulation::EntityIdReservation reservation =
      simulation::EntityIdReservation::create(simulation::EntityId::create(100), 0);

  CHECK(reservation == simulation::EntityIdReservation::none());
}
