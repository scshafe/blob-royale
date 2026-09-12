#include "chaser_controller.hpp"
#include "controller_observation_queries.hpp"
#include "controller_steering.hpp"
#include "fixtures/controller_steering_frozen_reference.hpp"
#include "fixtures/controller_steering_promotion_fixture.hpp"
#include "fixtures/controller_steering_racer_fixture.hpp"
#include "hill_seeker_controller.hpp"

#include "commands/thrust_command.hpp"
#include "racer_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::controller_steering_promotion;
namespace frozen = blob_royale::testing::controller_steering_reference;
namespace racer_fixture = blob_royale::testing::controller_steering_racer;

namespace {

[[nodiscard]] std::uint64_t bits(const double value) noexcept {
  return std::bit_cast<std::uint64_t>(value);
}

void check_commands(const std::vector<simulation::Command>& actual,
                    const std::vector<simulation::Command>& expected) {
  REQUIRE(actual.size() == expected.size());
  CHECK(actual == expected);
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (const auto* expected_thrust = std::get_if<simulation::ThrustCommand>(&expected[index])) {
      const auto* thrust = std::get_if<simulation::ThrustCommand>(&actual[index]);
      REQUIRE(thrust != nullptr);
      CHECK(thrust->entity == expected_thrust->entity);
      CHECK(thrust->input_generation == expected_thrust->input_generation);
      CHECK(bits(thrust->direction.x()) == bits(expected_thrust->direction.x()));
      CHECK(bits(thrust->direction.y()) == bits(expected_thrust->direction.y()));
    }
  }
}

void check_decision(controllers::Controller& actual, controllers::Controller& reference,
                    controllers::Controller& candidate,
                    const controllers::Observation& observation) {
  std::vector<simulation::Command> expected;
  std::vector<simulation::Command> actual_commands;
  std::vector<simulation::Command> candidate_commands;
  REQUIRE_NOTHROW(expected = reference.decide(observation));
  REQUIRE_NOTHROW(actual_commands = actual.decide(observation));
  REQUIRE_NOTHROW(candidate_commands = candidate.decide(observation));
  check_commands(actual_commands, expected);
  check_commands(candidate_commands, expected);
  CHECK(actual.entity() == reference.entity());
  CHECK(candidate.entity() == reference.entity());
  CHECK(actual.last_spawn_request_tick() == reference.last_spawn_request_tick());
  CHECK(candidate.last_spawn_request_tick() == reference.last_spawn_request_tick());
}

template <typename Snapshot>
concept FindsPlayer = requires(Snapshot&& snapshot) {
  controllers::find_observed_player(std::forward<Snapshot>(snapshot),
                                    simulation::EntityId::create(fixture::kSelf));
};
template <typename Snapshot>
concept FindsBody = requires(Snapshot&& snapshot) {
  controllers::find_observed_component<simulation::PhysicsBody>(
      std::forward<Snapshot>(snapshot), simulation::EntityId::create(fixture::kSelf));
};
static_assert(FindsPlayer<const simulation::WorldSnapshot&>);
static_assert(FindsBody<const simulation::WorldSnapshot&>);
static_assert(!FindsPlayer<simulation::WorldSnapshot>);
static_assert(!FindsBody<simulation::WorldSnapshot>);

} // namespace

TEST_CASE("controller steering promotion preserves raw signed offsets and written norm bits",
          "[unit][controllers][promotion]") {
  for (const auto& entry : fixture::kGeometryCases) {
    CAPTURE(entry.name);
    const auto origin = simulation::Vector2::create(entry.origin_x, entry.origin_y);
    const auto target = simulation::Vector2::create(entry.target_x, entry.target_y);
    const auto reference = frozen::FrozenOperations::offset(origin, target);
    const auto candidate = controllers::controller_target_offset(origin, target);
    CHECK(bits(candidate.x) == bits(reference.x));
    CHECK(bits(candidate.y) == bits(reference.y));
    CHECK(bits(controllers::controller_squared_magnitude(candidate)) ==
          bits(frozen::FrozenOperations::squared(reference)));
    CHECK(bits(controllers::controller_magnitude(candidate)) ==
          bits(frozen::FrozenOperations::magnitude(reference)));
    const double old_magnitude = frozen::FrozenOperations::magnitude(reference);
    if (old_magnitude > 0.0) {
      // Racer uses direct division, unlike Chaser's weighted multiply. The complete Racer
      // composition below also exercises these operations after its observation and target joins.
      const double new_magnitude = controllers::controller_magnitude(candidate);
      CHECK(bits(candidate.x / new_magnitude) == bits(reference.x / old_magnitude));
      CHECK(bits(candidate.y / new_magnitude) == bits(reference.y / old_magnitude));
    }
  }
  const auto& extreme = fixture::kGeometryCases.back();
  const auto origin = simulation::Vector2::create(extreme.origin_x, extreme.origin_y);
  const auto target = simulation::Vector2::create(extreme.target_x, extreme.target_y);
  REQUIRE_THROWS(target - origin);
  REQUIRE_NOTHROW(controllers::controller_target_offset(origin, target));
}

TEST_CASE("controller steering promotion preserves both old component clamps bit for bit",
          "[unit][controllers][promotion]") {
  for (const double component : fixture::clamp_inputs()) {
    CAPTURE(bits(component));
    const double candidate = controllers::clamp_controller_direction_component(component);
    CHECK(bits(candidate) == bits(frozen::FrozenOperations::chaser_clamp(component)));
    CHECK(bits(candidate) == bits(frozen::FrozenOperations::seeker_clamp(component)));
  }
}

TEST_CASE(
    "controller observation promotion retains player versus body domains and snapshot ownership",
    "[unit][controllers][promotion]") {
  const auto retained = [] {
    const auto observation = fixture::observation(fixture::baseline_frame());
    return observation.retained_snapshot();
  }();
  for (const auto entity : retained->entities()) {
    CHECK(controllers::find_observed_player(*retained, entity) ==
          frozen::FrozenOperations::player(*retained, entity));
    const simulation::PhysicsBody* expected = nullptr;
    for (const auto& entry : retained->components<simulation::PhysicsBody>()) {
      if (entry.entity == entity) {
        expected = &entry.value;
        break;
      }
    }
    CHECK(controllers::find_observed_component<simulation::PhysicsBody>(*retained, entity) ==
          expected);
  }
  const auto body_only = simulation::EntityId::create(fixture::kBodyOnly);
  CHECK(controllers::find_observed_player(*retained, body_only) == nullptr);
  CHECK(controllers::find_observed_component<simulation::PhysicsBody>(*retained, body_only) !=
        nullptr);
  const auto bodyless = simulation::EntityId::create(fixture::kBodyless);
  CHECK(controllers::find_observed_player(*retained, bodyless) == nullptr);
  CHECK(controllers::find_observed_component<simulation::PhysicsBody>(*retained, bodyless) ==
        nullptr);
  CHECK(controllers::find_observed_component<simulation::Controllable>(*retained, bodyless) !=
        nullptr);
  const auto absent = simulation::EntityId::create(fixture::kAbsent);
  CHECK(controllers::find_observed_player(*retained, absent) == nullptr);
  CHECK(controllers::find_observed_component<simulation::PhysicsBody>(*retained, absent) ==
        nullptr);
}

TEST_CASE(
    "controller steering promotion preserves full old Chaser commands and targets before cutover",
    "[unit][controllers][promotion]") {
  const auto controller = simulation::ControllerId::create(fixture::kController);
  for (const double weight : fixture::kAggressionWeights) {
    CAPTURE(weight);
    auto actual =
        controllers::ChaserController::create(controller, fixture::kSeeds.front(), {weight});
    const auto* typed = dynamic_cast<const controllers::ChaserController*>(actual.get());
    REQUIRE(typed != nullptr);
    frozen::Chaser reference(controller, weight);
    frozen::Chaser<fixture::PromotedOperations> candidate(controller, weight);
    for (const auto& frame : fixture::frames()) {
      CAPTURE(frame.name);
      const auto observation = fixture::observation(frame);
      for (std::size_t pass = 0; pass < fixture::kRepeatedPassCount; ++pass) {
        CAPTURE(pass);
        check_decision(*actual, reference, candidate, observation);
        CHECK(typed->target() == reference.target());
        CHECK(candidate.target() == reference.target());
      }
    }
  }
}

TEST_CASE("controller steering promotion preserves full old HillSeeker commands and ordered RNG "
          "draws before cutover",
          "[unit][controllers][promotion]") {
  const auto controller = simulation::ControllerId::create(fixture::kController);
  for (const auto seed : fixture::kSeeds) {
    for (const auto weights : fixture::kSeekerWeights) {
      CAPTURE(seed, weights.approach, weights.jitter);
      auto actual = controllers::HillSeekerController::create(controller, seed,
                                                              {weights.approach, weights.jitter});
      const auto* typed = dynamic_cast<const controllers::HillSeekerController*>(actual.get());
      REQUIRE(typed != nullptr);
      frozen::HillSeeker reference(controller, seed, weights.approach, weights.jitter);
      frozen::HillSeeker<fixture::PromotedOperations> candidate(controller, seed, weights.approach,
                                                                weights.jitter);
      std::uint64_t independently_expected_draws = 0;
      for (const auto& frame : fixture::frames()) {
        CAPTURE(frame.name);
        const auto observation = fixture::observation(frame);
        const bool produces_direction =
            observation.entity().has_value() &&
            frozen::FrozenOperations::player(observation.snapshot(), *observation.entity()) !=
                nullptr &&
            !observation.snapshot().components<simulation::Hill>().empty();
        for (std::size_t pass = 0; pass < fixture::kRepeatedPassCount; ++pass) {
          CAPTURE(pass);
          check_decision(*actual, reference, candidate, observation);
          independently_expected_draws += produces_direction ? 2 : 0;
          CHECK(typed->draw_count() == independently_expected_draws);
          CHECK(reference.draw_count() == independently_expected_draws);
          CHECK(candidate.draw_count() == independently_expected_draws);
          CHECK(typed->hill() == reference.hill());
          CHECK(candidate.hill() == reference.hill());
        }
      }
    }
  }
}

TEST_CASE("controller steering promotion preserves old real publication spawn retries and "
          "replacement lifecycle",
          "[unit][controllers][promotion]") {
  const auto controller = simulation::ControllerId::create(fixture::kController);
  auto chaser = controllers::ChaserController::create(controller, fixture::kSeeds.front());
  auto seeker = controllers::HillSeekerController::create(controller, fixture::kSeeds.front());
  frozen::Chaser chaser_reference(controller, fixture::kAggressionWeights.back());
  frozen::Chaser<fixture::PromotedOperations> chaser_candidate(controller,
                                                               fixture::kAggressionWeights.back());
  const auto weights = fixture::kSeekerWeights.front();
  frozen::HillSeeker seeker_reference(controller, fixture::kSeeds.front(), weights.approach,
                                      weights.jitter);
  frozen::HillSeeker<fixture::PromotedOperations> seeker_candidate(
      controller, fixture::kSeeds.front(), weights.approach, weights.jitter);
  const auto observations = fixture::lifecycle_observations();
  REQUIRE(observations.size() == fixture::kLifecycleCommandCounts.size());
  for (std::size_t index = 0; index < observations.size(); ++index) {
    CAPTURE(index, observations[index].tick_sequence().value());
    // Expected literal command counts are checked on separate reference instances below: this
    // comparison must invoke each stateful controller exactly once per authored observation.
    check_decision(*chaser, chaser_reference, chaser_candidate, observations[index]);
    check_decision(*seeker, seeker_reference, seeker_candidate, observations[index]);
  }
  frozen::Chaser literal_reference(controller, fixture::kAggressionWeights.back());
  for (std::size_t index = 0; index < observations.size(); ++index) {
    CAPTURE(index);
    CHECK(literal_reference.decide(observations[index]).size() ==
          fixture::kLifecycleCommandCounts[index]);
  }
  CHECK(seeker_reference.draw_count() == 0);
  CHECK(seeker_candidate.draw_count() == 0);
}

TEST_CASE("controller steering promotion preserves complete old Racer decisions with helper-backed "
          "joins and steering",
          "[unit][controllers][racer][promotion]") {
  const auto controller = simulation::ControllerId::create(racer_fixture::kController);
  for (const auto seed : fixture::kSeeds) {
    for (const auto& frame : racer_fixture::frames()) {
      CAPTURE(seed, frame.name, frame.caution);
      auto actual = controllers::RacerController::create(controller, seed, {frame.caution});
      frozen::Racer reference(controller, frame.caution);
      frozen::Racer<fixture::PromotedOperations> candidate(controller, frame.caution);
      frozen::Racer literal_reference(controller, frame.caution);
      const auto observation = racer_fixture::observation(frame);
      for (std::size_t pass = 0; pass < fixture::kRepeatedPassCount; ++pass) {
        CAPTURE(pass);
        check_decision(*actual, reference, candidate, observation);
        CHECK(literal_reference.decide(observation).size() ==
              racer_fixture::expected_command_count(frame, pass));
      }
    }
  }
}

TEST_CASE("controller steering promotion preserves Racer spawn retries and replacement state on "
          "real publications",
          "[unit][controllers][racer][promotion]") {
  const auto controller = simulation::ControllerId::create(fixture::kController);
  const auto observations = fixture::lifecycle_observations();
  for (const auto seed : fixture::kSeeds) {
    CAPTURE(seed);
    auto actual = controllers::RacerController::create(controller, seed);
    frozen::Racer reference(controller, racer_fixture::kDefaultCaution);
    frozen::Racer<fixture::PromotedOperations> candidate(controller,
                                                         racer_fixture::kDefaultCaution);
    frozen::Racer literal_reference(controller, racer_fixture::kDefaultCaution);
    REQUIRE(observations.size() == fixture::kLifecycleCommandCounts.size());
    for (std::size_t index = 0; index < observations.size(); ++index) {
      CAPTURE(index, observations[index].tick_sequence().value());
      check_decision(*actual, reference, candidate, observations[index]);
      CHECK(literal_reference.decide(observations[index]).size() ==
            fixture::kLifecycleCommandCounts[index]);
    }
  }
}
