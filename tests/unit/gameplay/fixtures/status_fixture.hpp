#ifndef BLOB_ROYALE_TESTING_STATUS_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_STATUS_FIXTURE_HPP

#include "components/lifetime_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "events/stun_request_event.hpp"
#include "fixtures/thrust_steering_fixture.hpp"
#include "shared/lifetime_expiry_system.hpp"
#include "shared/status_system.hpp"
#include "shared/thrust_steering_system.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::testing::status_fixture {

inline constexpr std::uint64_t kActivation = 7;
inline constexpr std::uint64_t kDuration = 3;
inline constexpr std::uint64_t kExpiry = 10;
inline constexpr std::uint64_t kLongDuration = 10;
inline constexpr std::uint64_t kEarlierActivation = 2;
inline constexpr std::uint64_t kLaterActivation = 12;
inline constexpr std::uint64_t kOneTick = 1;
inline constexpr std::uint64_t kZeroDuration = 0;
inline constexpr std::uint64_t kOverflowDuration = std::numeric_limits<std::uint64_t>::max();
inline constexpr std::uint64_t kSecondEntity = 2;
inline constexpr std::uint64_t kBodylessEntity = 3;
inline constexpr std::uint64_t kStaticEntity = 4;
inline constexpr std::uint64_t kMissingEntity = 5;
inline constexpr std::uint64_t kHazardEntity = 6;
inline constexpr std::uint64_t kHazardLifetime = 4;
inline constexpr double kRadius = 10.0;
inline constexpr std::size_t kSpawnPointCount = 4;
inline constexpr std::uint64_t kAfterCollisionTick = 5;
inline constexpr std::array<std::uint64_t, 2> kRespawnDelays{0, 3};
// The authored ability tuning, so a status test cancels the same 160/32/360/240 guard a live
// activation writes rather than a shorter one invented for the test.
inline constexpr std::uint64_t kShieldDuration = 160;
inline constexpr std::uint64_t kPerfectDuration = 32;
inline constexpr std::uint64_t kCooldownDuration = 360;
inline constexpr std::uint64_t kParryStunDuration = 240;
// A guard raised on tick 2: protection [2, 162), perfect opening [2, 34), cooldown [2, 362).
inline constexpr std::uint64_t kGuardActivation = 2;
// Past the perfect opening and inside the protection, so "already-elapsed perfect history" is an
// unambiguous fact of the window rather than a claim about a window still open at cancellation.
inline constexpr std::uint64_t kAfterPerfectTick = 40;
// Past the protection and inside the cooldown, which is the state a cancellation must not touch.
inline constexpr std::uint64_t kAfterShieldTick = 170;

[[nodiscard]] inline simulation::EntityId
entity(const std::uint64_t value = thrust_steering_fixture::kEntity) {
  return simulation::EntityId::create(value);
}
[[nodiscard]] inline simulation::TickSequence tick(const std::uint64_t value = kActivation) {
  return simulation::TickSequence::create(value);
}
[[nodiscard]] inline simulation::Vector2 zero() { return thrust_steering_fixture::zero(); }
[[nodiscard]] inline simulation::Vector2 direction() { return thrust_steering_fixture::analog(); }
[[nodiscard]] inline simulation::Vector2 other_direction() {
  return simulation::Vector2::create(-1.0, 0.0);
}
[[nodiscard]] inline simulation::Vector2 velocity() {
  return simulation::Vector2::create(23.0, -17.0);
}
[[nodiscard]] inline simulation::Vector2 bump_velocity() {
  return simulation::Vector2::create(-41.0, 13.0);
}
[[nodiscard]] inline simulation::Vector2 replacement_position() {
  return simulation::Vector2::create(thrust_steering_fixture::kReplacementX,
                                     thrust_steering_fixture::kReplacementY);
}
[[nodiscard]] inline simulation::Stun stun(const std::uint64_t activation = kActivation,
                                           const std::uint64_t duration = kDuration) {
  return {simulation::TickWindow::create(tick(activation), duration)};
}
[[nodiscard]] inline simulation::Shield shield(const std::uint64_t activation = kGuardActivation) {
  return simulation::Shield::activate(tick(activation), kShieldDuration, kPerfectDuration,
                                      kCooldownDuration, kParryStunDuration);
}
[[nodiscard]] inline simulation::ThrustCommand
command(std::optional<simulation::TickSequence> generation = {},
        const simulation::Vector2& requested = direction()) {
  return {entity(), requested, generation};
}

[[nodiscard]] inline simulation::GameWorld world() {
  auto result = thrust_steering_fixture::world();
  auto* body = result.mutable_store<simulation::PhysicsBody>().mutable_find(entity());
  *body = body->with_velocity(velocity());
  return result;
}

[[nodiscard]] inline simulation::GameWorld
world_with_status(const std::uint64_t activation = kActivation,
                  const std::uint64_t duration = kDuration) {
  auto result = world();
  result.mutable_store<simulation::Stun>().insert_or_assign(entity(), stun(activation, duration));
  auto* controllable = result.mutable_store<simulation::Controllable>().mutable_find(entity());
  controllable->input_generation = tick(activation);
  controllable->normalized_thrust_intent = zero();
  return result;
}

// One guarded body with no stun yet: the world a cancellation case starts from.
[[nodiscard]] inline simulation::GameWorld
world_with_shield(const std::uint64_t activation = kGuardActivation) {
  auto result = world();
  result.mutable_store<simulation::Shield>().insert_or_assign(entity(), shield(activation));
  return result;
}

[[nodiscard]] inline simulation::GameWorld invalid_target_world() {
  auto result = world();
  auto* controllable = result.mutable_store<simulation::Controllable>().mutable_find(entity());
  controllable->input_generation = tick(kEarlierActivation);
  controllable->normalized_thrust_intent = direction();
  result.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(kBodylessEntity),
      simulation::Controllable{simulation::ControllerId::create(kBodylessEntity)});
  result.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(kStaticEntity), simulation::PhysicsBody::create_static(replacement_position()));
  return result;
}

[[nodiscard]] inline simulation::GameWorld later_collision_world() {
  auto result = world();
  result.mutable_store<simulation::Controllable>().erase(entity());
  result.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(),
      simulation::PhysicsBody::create(simulation::Vector2::create(100.0, 320.0), zero(), zero())
          .with_radius(kRadius));
  result.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(kHazardEntity),
      simulation::PhysicsBody::create(simulation::Vector2::create(123.0, 320.0),
                                      simulation::Vector2::create(-400.0, 0.0), zero())
          .with_radius(kRadius));
  return result;
}

struct ScheduledRequest final {
  std::uint64_t activation;
  simulation::StunRequest request;
};

// Approved Step 14 in-tick producer: exercises the complete kernel with literal timed requests.
class RequestProducer final : public simulation::SimulationSystem {
public:
  explicit RequestProducer(std::vector<ScheduledRequest> requests)
      : requests_(std::move(requests)) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "test_stun_request"; }
  void apply(simulation::GameWorld& target, const simulation::TickContext& context) const override {
    for (const auto& scheduled : requests_) {
      if (context.tick_sequence().value() == scheduled.activation) {
        target.emit(scheduled.request);
      }
    }
  }

private:
  std::vector<ScheduledRequest> requests_;
};

[[nodiscard]] inline simulation::GameSimulation game(std::vector<ScheduledRequest> requests,
                                                     simulation::GameWorld initial = world()) {
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPreKernel, gameplay::ThrustSteeringSystem::create()});
  systems.push_back({simulation::SystemStage::kPostKernel,
                     std::make_unique<const RequestProducer>(std::move(requests))});
  systems.push_back({simulation::SystemStage::kPostKernel, gameplay::StatusSystem::create()});
  systems.push_back(
      {simulation::SystemStage::kLifecycle, gameplay::LifetimeExpirySystem::create()});
  return simulation::GameSimulation::create(
      gameplay_configuration(), std::move(initial),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(gameplay_map(kSpawnPointCount))
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
}

inline void advance(simulation::GameSimulation& target, const std::uint64_t until) {
  while (target.tick_sequence().value() < until) {
    target.step(kGameplayFixedDelta, simulation::InputBatch::empty());
  }
}

inline void step(simulation::GameSimulation& target, std::vector<simulation::Command> commands) {
  target.step(kGameplayFixedDelta,
              simulation::InputBatch::create(std::move(commands), target.accepted_command_kinds(),
                                             simulation::EntityIdReservation::none()));
}

} // namespace blob_royale::testing::status_fixture

#endif
