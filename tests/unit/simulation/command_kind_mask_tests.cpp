#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

namespace simulation = blob_royale::simulation;

TEST_CASE("CommandKindMask none contains no registered kind",
          "[unit][simulation][command_kind_mask]") {
  const simulation::CommandKindMask mask = simulation::CommandKindMask::none();

  CHECK(mask.empty());
  CHECK(mask.bits() == 0);
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    CHECK_FALSE(mask.contains(kind));
  }
}

TEST_CASE("CommandKindMask all contains every registered kind",
          "[unit][simulation][command_kind_mask]") {
  const simulation::CommandKindMask mask = simulation::CommandKindMask::all();

  CHECK_FALSE(mask.empty());
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    CHECK(mask.contains(kind));
  }
}

TEST_CASE("CommandKindMask with inserts one kind and leaves the source value unchanged",
          "[unit][simulation][command_kind_mask]") {
  const simulation::CommandKindMask empty = simulation::CommandKindMask::none();
  const simulation::CommandKindMask thrust_only = empty.with(simulation::CommandKind::kThrust);

  CHECK(thrust_only.contains(simulation::CommandKind::kThrust));
  CHECK_FALSE(thrust_only.contains(simulation::CommandKind::kSpawn));
  CHECK(empty == simulation::CommandKindMask::none());
}

TEST_CASE("CommandKindMask with is idempotent for a kind the mask already contains",
          "[unit][simulation][command_kind_mask]") {
  const simulation::CommandKindMask once =
      simulation::CommandKindMask::none().with(simulation::CommandKind::kSpawn);

  CHECK(once.with(simulation::CommandKind::kSpawn) == once);
}

TEST_CASE("CommandKindMask created from named kinds equals the same kinds inserted one at a time",
          "[unit][simulation][command_kind_mask]") {
  const simulation::CommandKindMask named = simulation::CommandKindMask::create(
      {simulation::CommandKind::kSpawn, simulation::CommandKind::kThrust});
  const simulation::CommandKindMask inserted = simulation::CommandKindMask::none()
                                                   .with(simulation::CommandKind::kThrust)
                                                   .with(simulation::CommandKind::kSpawn);

  CHECK(named == inserted);
  CHECK_FALSE(named.contains(simulation::CommandKind::kDespawn));
}

TEST_CASE("CommandKindMask created from raw bits round-trips through bits",
          "[unit][simulation][command_kind_mask]") {
  const simulation::CommandKindMask mask =
      simulation::CommandKindMask::create(simulation::CommandKindMask::all().bits());

  CHECK(mask == simulation::CommandKindMask::all());
}

TEST_CASE("CommandKindMask distinguishes different kind sets",
          "[unit][simulation][command_kind_mask]") {
  CHECK(simulation::CommandKindMask::none().with(simulation::CommandKind::kSpawn) !=
        simulation::CommandKindMask::none().with(simulation::CommandKind::kDespawn));
  CHECK(simulation::CommandKindMask::none() != simulation::CommandKindMask::all());
}

TEST_CASE("CommandKindMask rejects a bit belonging to no registered command kind",
          "[unit][simulation][command_kind_mask][validation]") {
  const auto unknown_bit =
      static_cast<simulation::CommandKindMask::Bits>(simulation::CommandKindMask::all().bits() + 1);

  try {
    static_cast<void>(simulation::CommandKindMask::create(unknown_bit));
    FAIL("mask with an unregistered bit was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kCommandKindMaskUnknownBit);
    CHECK(error.code() == std::string_view{"SIMULATION.COMMAND_KIND_MASK_UNKNOWN_BIT"});
    CHECK(error.context() == "command_kind_mask.bits");
  }
}

TEST_CASE("CommandKindMask all covers exactly the registered kind bits and no more",
          "[unit][simulation][command_kind_mask]") {
  simulation::CommandKindMask::Bits expected_bits = 0;
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    expected_bits |= static_cast<simulation::CommandKindMask::Bits>(kind);
  }

  CHECK(simulation::CommandKindMask::all().bits() == expected_bits);
}
