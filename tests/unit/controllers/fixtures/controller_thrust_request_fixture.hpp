#ifndef BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_FIXTURES_CONTROLLER_THRUST_REQUEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_CONTROLLERS_FIXTURES_CONTROLLER_THRUST_REQUEST_FIXTURE_HPP

#include "racer_observation_fixture.hpp"

#include "chaser_controller.hpp"
#include "components/hill_component.hpp"
#include "components/stun_component.hpp"
#include "controller.hpp"
#include "hill_seeker_controller.hpp"
#include "racer_controller.hpp"
#include "wanderer_controller.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string_view>

namespace blob_royale::testing::thrust_request_fixture {

inline constexpr std::uint64_t kController = 1;
inline constexpr std::uint64_t kOtherController = 2;
inline constexpr std::uint64_t kUnseatedController = 3;
inline constexpr std::uint64_t kHillEntity = 50;
inline constexpr std::uint64_t kActivation = 100;
inline constexpr std::uint64_t kDuration = 40;
inline constexpr std::uint64_t kExpiry = kActivation + kDuration;
inline constexpr std::uint64_t kSeed = 9;
inline constexpr std::uint64_t kReactionComparisonPasses =
    2 * (controllers::WandererController::kDefaultReactionDelayFrames + 1);
inline constexpr std::array<std::string_view, 4> kActiveKinds{"wanderer", "chaser", "hill_seeker",
                                                              "racer"};

enum class BodyState { kDynamic, kAbsent, kStatic };

[[nodiscard]] inline simulation::Vector2 direction() {
  return simulation::Vector2::create(1.0, -0.5);
}

[[nodiscard]] inline simulation::TickSequence generation() {
  return simulation::TickSequence::create(kActivation);
}

[[nodiscard]] inline simulation::Stun active_stun() {
  return simulation::Stun{simulation::TickWindow::create(generation(), kDuration)};
}

[[nodiscard]] inline simulation::GameWorld
world(const std::optional<simulation::TickSequence> input_generation = generation(),
      const std::optional<simulation::Stun> stun = std::nullopt,
      const BodyState body_state = BodyState::kDynamic) {
  auto result = race_test_world(
      {simulation::Vector2::create(200.0, 320.0), simulation::Vector2::create(600.0, 320.0)});
  result.mutable_match().mode_state = straight_racer_course();
  result.mutable_store<simulation::RaceProgress>().insert_or_assign(
      simulation::EntityId::create(kController), simulation::RaceProgress{0});
  result.mutable_store<simulation::Hill>().insert_or_assign(
      simulation::EntityId::create(kHillEntity),
      simulation::Hill{simulation::Vector2::create(500.0, 320.0), 40.0});
  for (auto& controllable : result.mutable_store<simulation::Controllable>().mutable_values()) {
    controllable.input_generation = input_generation;
  }
  const auto entity = simulation::EntityId::create(kController);
  if (stun.has_value()) {
    result.mutable_store<simulation::Stun>().insert_or_assign(entity, *stun);
  }
  if (body_state == BodyState::kAbsent) {
    result.mutable_store<simulation::PhysicsBody>().erase(entity);
  } else if (body_state == BodyState::kStatic) {
    result.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        entity, simulation::PhysicsBody::create_static(simulation::Vector2::create(200.0, 320.0)));
  }
  return result;
}

[[nodiscard]] inline controllers::Observation
observation(simulation::GameWorld initial_world, const std::uint64_t tick = kActivation,
            const std::uint64_t controller = kController) {
  auto map =
      simulation::MapDefinition::create("thrust_request_observation", straight_racer_terrain(), {},
                                        {}, simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      gameplay_configuration(), std::move(initial_world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  for (std::uint64_t current = 0; current < tick; ++current) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(controller));
}

[[nodiscard]] inline std::unique_ptr<controllers::Controller>
active_bot(const std::string_view kind) {
  const auto controller = simulation::ControllerId::create(kController);
  if (kind == "wanderer") {
    return controllers::WandererController::create(controller, kSeed);
  }
  if (kind == "chaser") {
    return controllers::ChaserController::create(controller, kSeed);
  }
  if (kind == "hill_seeker") {
    return controllers::HillSeekerController::create(controller, kSeed);
  }
  return controllers::RacerController::create(controller, kSeed);
}

// Exposes only the protected authoring operation so its exact-vector contract is observable.
class ThrustRequestController final : public controllers::Controller {
public:
  ThrustRequestController() : Controller(simulation::ControllerId::create(kController)) {}

  using Controller::request_thrust;
  [[nodiscard]] std::string_view kind() const noexcept override { return "thrust_request_fixture"; }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const controllers::Observation& observed) override {
    return request_thrust(observed, direction());
  }
};

[[nodiscard]] inline simulation::Command
literal(const std::optional<simulation::TickSequence> token,
        const std::uint64_t entity = kController, const bool release = false) {
  return simulation::ThrustCommand{simulation::EntityId::create(entity),
                                   release ? simulation::Vector2::create(0.0, 0.0) : direction(),
                                   token};
}

} // namespace blob_royale::testing::thrust_request_fixture

#endif
