#ifndef BLOB_ROYALE_TESTING_VELOCITY_CONTROL_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_VELOCITY_CONTROL_FIXTURE_HPP

#include "commands/charge_command.hpp"
#include "commands/rotate_velocity_command.hpp"
#include "components/charge_component.hpp"
#include "components/stun_component.hpp"
#include "fixtures/thrust_steering_fixture.hpp"
#include "sandbox/sandbox_mode.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace blob_royale::testing::velocity_control_fixture {

inline constexpr std::uint64_t kActivationTick = 7;
inline constexpr std::uint64_t kStunDuration = 3;
inline constexpr std::uint64_t kAfterStunTick = kActivationTick + kStunDuration;
inline constexpr std::uint64_t kChargeCooldownTicks = 400;
inline constexpr std::uint64_t kChargeActiveTicks = 200;
inline constexpr std::uint64_t kChargeHitStunTicks = 240;
inline constexpr std::uint64_t kBrakeObservationTicks = 128;
inline constexpr double kInitialX = 480.0;
inline constexpr double kInitialY = 320.0;
inline constexpr double kNormalSpeed = 600.0;
inline constexpr double kChargeFraction = 0.75;
inline constexpr double kChargeGain = kNormalSpeed * kChargeFraction;
inline constexpr std::array kDragRates{0.0, 2.0, 400.0, 800.0, std::numeric_limits<double>::max()};
inline constexpr std::array<std::array<double, 2>, 5> kBrakingVelocities{
    std::array{0.0, 0.0}, std::array{3.0, 4.0}, std::array{6.0, 0.0}, std::array{-120.0, 160.0},
    std::array{300.0, -400.0}};

[[nodiscard]] inline simulation::EntityId entity() { return thrust_steering_fixture::entity(); }
[[nodiscard]] inline simulation::TickSequence tick(const std::uint64_t value = kActivationTick) {
  return simulation::TickSequence::create(value);
}
[[nodiscard]] inline simulation::Vector2 zero() { return thrust_steering_fixture::zero(); }
[[nodiscard]] inline simulation::Vector2 east() { return simulation::Vector2::create(1.0, 0.0); }
[[nodiscard]] inline simulation::Vector2 velocity() {
  return simulation::Vector2::create(30.0, 40.0);
}

[[nodiscard]] inline simulation::GameWorld world(const simulation::Vector2 initial = velocity()) {
  auto result = thrust_steering_fixture::world();
  result.mutable_match().phase = simulation::MatchPhase::kRunning;
  const auto tuning = simulation::MovementTuning::create(400.0, kNormalSpeed, kChargeFraction);
  result.mutable_match().movement = simulation::MovementTuningState{tuning, tuning};
  auto* body = result.mutable_store<simulation::PhysicsBody>().mutable_find(entity());
  *body = body->with_position(simulation::Vector2::create(kInitialX, kInitialY))
              .with_velocity(initial)
              .with_acceleration(zero());
  return result;
}

[[nodiscard]] inline simulation::ThrustCommand
brake(const bool held = true, const std::optional<simulation::TickSequence> generation = {},
      const simulation::Vector2 direction = zero()) {
  return {entity(), direction, generation, held};
}

[[nodiscard]] inline simulation::RotateVelocityCommand
rotate(const bool clockwise, const std::optional<simulation::TickSequence> generation = {}) {
  return {entity(), clockwise, generation};
}

[[nodiscard]] inline simulation::Charge active_charge() {
  return simulation::Charge::activate(tick(), kChargeCooldownTicks, kChargeActiveTicks,
                                      kChargeHitStunTicks);
}

// Actual mode declarations own ordering and phase-0 admission. Configured drag reaches the real
// kernel, while the centrally seated single body avoids wall/contact impulses obscuring controls.
[[nodiscard]] inline simulation::GameSimulation game(const simulation::Vector2 initial = velocity(),
                                                     const double drag_per_second = 0.0) {
  const auto configuration =
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 12, 8, drag_per_second);
  return simulation::GameSimulation::create(
      configuration, world(initial),
      simulation::GameSimulationSetup::of_mode(gameplay_map(4), gameplay::SandboxMode::create()));
}

inline void step(simulation::GameSimulation& target,
                 std::vector<simulation::Command> commands = {}) {
  target.step(kGameplayFixedDelta,
              simulation::InputBatch::create(std::move(commands), target.accepted_command_kinds(),
                                             simulation::EntityIdReservation::none()));
}

} // namespace blob_royale::testing::velocity_control_fixture

#endif
