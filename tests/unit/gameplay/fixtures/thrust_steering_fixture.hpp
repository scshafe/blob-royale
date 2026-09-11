#ifndef BLOB_ROYALE_TESTING_THRUST_STEERING_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_THRUST_STEERING_FIXTURE_HPP

#include "commands/set_movement_tuning_command.hpp"
#include "gameplay_test_fixture.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace blob_royale::testing::thrust_steering_fixture {

inline constexpr std::uint64_t kEntity = 1;
inline constexpr std::uint64_t kController = 7;
inline constexpr double kRetunedAcceleration = 800.0;
inline constexpr double kInactiveCeiling = 10'000.0;
inline constexpr double kAuthoredAccelerationX = 12.0;
inline constexpr double kAuthoredAccelerationY = -7.0;
inline constexpr double kAnalogX = 0.25;
inline constexpr double kAnalogY = -0.75;
inline constexpr double kReplacementX = 300.0;
inline constexpr double kReplacementY = 320.0;
inline constexpr double kExternalSpeed = 1'200.0;
inline constexpr double kLoweredCeiling = 150.0;
inline constexpr std::array kPhases{
    simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
    simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded};

[[nodiscard]] inline simulation::EntityId entity() { return simulation::EntityId::create(kEntity); }

[[nodiscard]] inline simulation::Vector2 zero() { return simulation::Vector2::create(0.0, 0.0); }

[[nodiscard]] inline simulation::Vector2 authored_acceleration() {
  return simulation::Vector2::create(kAuthoredAccelerationX, kAuthoredAccelerationY);
}

[[nodiscard]] inline simulation::Vector2 analog() {
  return simulation::Vector2::create(kAnalogX, kAnalogY);
}

[[nodiscard]] inline simulation::MovementTuning retuned() {
  return simulation::MovementTuning::create(kRetunedAcceleration, kInactiveCeiling);
}

// Direct system worlds keep controller state observable: public snapshots intentionally strip
// both commands and normalized intent. The actual-kernel test separately proves effective-tick use.
[[nodiscard]] inline simulation::GameWorld world() {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      entity(),
      simulation::PhysicsBody::create(simulation::Vector2::create(100.0, 320.0), zero(),
                                      authored_acceleration())
          .with_radius(10.0),
      simulation::ControllerId::create(kController)));
  auto result = simulation::GameWorld::create(std::move(seeds));
  const auto tuning = gameplay_movement_tuning();
  result.mutable_match().movement = simulation::MovementTuningState{tuning, tuning};
  return result;
}

[[nodiscard]] inline simulation::Command analog_command() {
  return thrust_command(kEntity, kAnalogX, kAnalogY);
}

[[nodiscard]] inline simulation::Command retuning_command() {
  return simulation::SetMovementTuningCommand{simulation::ControllerId::create(kController), 1, 0,
                                              retuned()};
}

[[nodiscard]] inline simulation::SeatRoster seated_lobby() {
  auto roster = simulation::SeatRoster::of_size(1);
  roster.assign_seat(0, simulation::Seat{simulation::ControllerSeat{
                            simulation::ControllerId::create(kController)}});
  return roster;
}

} // namespace blob_royale::testing::thrust_steering_fixture

#endif
