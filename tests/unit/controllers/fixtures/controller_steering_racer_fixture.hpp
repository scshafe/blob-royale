#ifndef BLOB_ROYALE_TESTING_CONTROLLER_STEERING_RACER_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_CONTROLLER_STEERING_RACER_FIXTURE_HPP

#include "racer_observation_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/stun_component.hpp"
#include "fixed_delta.hpp"
#include "input_batch.hpp"
#include "tick_window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace blob_royale::testing::controller_steering_racer {

inline constexpr std::uint64_t kController = 1;
inline constexpr double kDefaultCaution = 0.75;

enum class BodyState { kPresent, kAbsent, kBodyless, kMissingProgress, kOtherMode };
enum class RoadShape { kStraight, kBent, kAlternateBinding };

struct Frame final {
  std::string_view name;
  double x;
  double y;
  std::uint64_t checkpoint;
  double caution{kDefaultCaution};
  RoadShape road{RoadShape::kStraight};
  BodyState body{BodyState::kPresent};
  std::optional<std::uint64_t> generation{};
  bool stun{false};
  std::uint64_t ticks{0};
};

// Literal checkpoints and terrain come from the established Racer fixture. These cases add the
// new helper composition proof without editing the accepted projection oracle or its workload.
[[nodiscard]] inline std::vector<Frame> frames() {
  return {
      {"straight-target", 200.0, 320.0, 0},
      {"later-target", 400.0, 320.0, 1},
      {"fractional-target", 192.125, 352.75, 0},
      {"coincident-target", 300.0, 320.0, 0},
      {"caution-equal", 200.0, 380.0, 0},
      {"caution-below", 200.0, std::nextafter(380.0, 0.0), 0},
      {"caution-above", 200.0, std::nextafter(380.0, std::numeric_limits<double>::infinity()), 0},
      {"straight-recovery", 200.0, 381.0, 0},
      {"changed-caution", 200.0, 361.0, 0, 0.5},
      {"first-endpoint-recovery", 60.0, 370.0, 0},
      {"last-endpoint-recovery", 840.0, 370.0, 1},
      {"finished-coast", 600.0, 381.0, 2},
      {"bend-recovery", 550.0, 440.0, 1, 0.5, RoadShape::kBent},
      {"bend-tie", 450.0, 370.0, 1, 0.5, RoadShape::kBent},
      {"named-road-not-first", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kAlternateBinding},
      {"absent-spawn", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight, BodyState::kAbsent},
      {"bodyless-wait", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight,
       BodyState::kBodyless},
      {"progressless-wait", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight,
       BodyState::kMissingProgress},
      {"other-mode-wait", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight,
       BodyState::kOtherMode},
      {"active-stun", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight, BodyState::kPresent,
       1, true, 1},
      {"exact-stun-expiry", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight,
       BodyState::kPresent, 1, true, 3},
      {"generation-retained", 200.0, 320.0, 0, kDefaultCaution, RoadShape::kStraight,
       BodyState::kPresent, 1, false, 4}};
}

[[nodiscard]] inline std::size_t expected_command_count(const Frame& frame,
                                                        const std::size_t repeated_pass) noexcept {
  if (frame.body == BodyState::kAbsent) {
    return repeated_pass == 0 ? 1 : 0;
  }
  if (frame.body != BodyState::kPresent || (frame.stun && frame.ticks < 3)) {
    return 0;
  }
  return 1;
}

[[nodiscard]] inline controllers::Observation observation(const Frame& frame) {
  auto course = frame.road == RoadShape::kBent               ? bent_racer_course()
                : frame.road == RoadShape::kAlternateBinding ? alternate_racer_course()
                                                             : straight_racer_course();
  auto terrain = frame.road == RoadShape::kBent               ? bent_racer_terrain()
                 : frame.road == RoadShape::kAlternateBinding ? alternate_racer_terrain(false)
                                                              : straight_racer_terrain();
  auto world = racer_observation_world(simulation::Vector2::create(frame.x, frame.y),
                                       frame.checkpoint, std::move(course));
  const auto self = simulation::EntityId::create(kController);
  switch (frame.body) {
  case BodyState::kAbsent:
    world.destroy_entity(self);
    break;
  case BodyState::kBodyless:
    world.mutable_store<simulation::PhysicsBody>().erase(self);
    break;
  case BodyState::kMissingProgress:
    world.mutable_store<simulation::RaceProgress>().erase(self);
    break;
  case BodyState::kOtherMode:
    world.mutable_match().mode_state = simulation::NoModeState{};
    break;
  case BodyState::kPresent:
    break;
  }
  if (frame.generation) {
    world.mutable_store<simulation::Controllable>().insert_or_assign(
        self, simulation::Controllable{simulation::ControllerId::create(kController),
                                       {},
                                       {},
                                       simulation::TickSequence::create(*frame.generation)});
  }
  if (frame.stun) {
    world.mutable_store<simulation::Stun>().insert_or_assign(
        self,
        simulation::Stun{simulation::TickWindow::create(simulation::TickSequence::create(1), 2)});
  }
  auto map = simulation::MapDefinition::create("racer_steering_proof", std::move(terrain), {}, {},
                                               simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      gameplay_configuration(), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  for (std::uint64_t tick = 0; tick < frame.ticks; ++tick) {
    static_cast<void>(
        game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  }
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(kController));
}

} // namespace blob_royale::testing::controller_steering_racer

#endif
