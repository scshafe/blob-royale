#include "racer_controller.hpp"

#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/racer_observation_fixture.hpp"
#include "terrain_queries.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

struct FrozenCourse final {
  double track_half_width;
  std::vector<simulation::Vector2> track;
  std::vector<simulation::Vector2> checkpoints;
};

struct FrozenTarget final {
  simulation::Vector2 point;
  double distance;
};

// Frozen verbatim from f837061:src/controllers/racer_controller.cpp (CourseTarget renamed).
// No query helper, vector norm, new clamp, or geometry normalization may replace this reference.
[[nodiscard]] FrozenTarget
frozen_nearest_course_target(const std::span<const simulation::Vector2> track,
                             const simulation::Vector2& position) {
  FrozenTarget nearest{track.front(), std::numeric_limits<double>::infinity()};
  for (std::size_t index = 1; index < track.size(); ++index) {
    const simulation::Vector2& a = track[index - 1];
    const simulation::Vector2& b = track[index];
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double wx = position.x() - a.x();
    const double wy = position.y() - a.y();
    if (dx == 0.0 && dy == 0.0) {
      throw controllers::ControllersValidationError(
          controllers::ControllersValidationCode::kRacerCourseInvalid,
          "racer_controller.observation.track",
          "published course contains coincident consecutive nodes");
    }
    double t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
    if (t < 0.0) {
      t = 0.0;
    } else if (t > 1.0) {
      t = 1.0;
    }
    const double cx = a.x() + (dx * t);
    const double cy = a.y() + (dy * t);
    const double ex = position.x() - cx;
    const double ey = position.y() - cy;
    const double distance = std::sqrt(ex * ex + ey * ey);
    if (distance < nearest.distance) {
      nearest = {simulation::Vector2::create(cx, cy), distance};
    }
  }
  return nearest;
}

enum class ProjectionReader { kFrozen, kCanonical };

// Complete frozen f837061 racer decision body, including body/progress joins, lifecycle branches,
// strict caution comparison and written normalization. Course geometry comes from independently
// owned test literals so retiring the public mirror cannot destroy the reference. The candidate
// differs ONLY at the projection call. Both candidates passed before production delegation;
// the same reference now proves that the delegated production reader retains those command bits.
// The production Controller base remains the canonical identity/spawn-request implementation.
class ReferenceRacer final : public controllers::Controller {
public:
  ReferenceRacer(const simulation::ControllerId controller, FrozenCourse course,
                 const double caution_fraction, const ProjectionReader projection)
      : Controller(controller), course_(std::move(course)), caution_fraction_(caution_fraction),
        projection_(projection) {}

  [[nodiscard]] std::string_view kind() const noexcept override { return "racer_projection_proof"; }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const controllers::Observation& observation) override {
    if (!observation.entity().has_value()) {
      return request_body(observation);
    }
    const simulation::WorldSnapshot& snapshot = observation.snapshot();
    if (std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state()) == nullptr) {
      return {};
    }
    const FrozenCourse* const course = &course_;
    const simulation::EntityId self = *observation.entity();
    const simulation::PhysicsBody* body = nullptr;
    for (const auto& entry : snapshot.components<simulation::PhysicsBody>()) {
      if (entry.entity == self) {
        body = &entry.value;
        break;
      }
    }
    const simulation::RaceProgress* progress = nullptr;
    for (const auto& entry : snapshot.components<simulation::RaceProgress>()) {
      if (entry.entity == self) {
        progress = &entry.value;
        break;
      }
    }
    if (body == nullptr || progress == nullptr) {
      return {};
    }
    if (course->track.size() < 2 || course->checkpoints.empty() ||
        !std::isfinite(course->track_half_width) || course->track_half_width <= 0.0 ||
        progress->next_checkpoint > course->checkpoints.size()) {
      throw controllers::ControllersValidationError(
          controllers::ControllersValidationCode::kRacerCourseInvalid,
          "racer_controller.observation.course",
          "published race geometry or checkpoint progress is invalid");
    }

    simulation::Vector2 direction = simulation::Vector2::create(0.0, 0.0);
    if (progress->next_checkpoint < course->checkpoints.size()) {
      const FrozenTarget nearest = project(observation, body->position());
      const simulation::Vector2& target =
          nearest.distance > caution_fraction_ * course->track_half_width
              ? nearest.point
              : course->checkpoints[static_cast<std::size_t>(progress->next_checkpoint)];
      const double dx = target.x() - body->position().x();
      const double dy = target.y() - body->position().y();
      const double magnitude = std::sqrt(dx * dx + dy * dy);
      if (magnitude > 0.0) {
        direction = simulation::Vector2::create(dx / magnitude, dy / magnitude);
      }
    }
    return {simulation::Command{simulation::ThrustCommand{.entity = self, .direction = direction}}};
  }

  [[nodiscard]] FrozenTarget project(const controllers::Observation& observation,
                                     const simulation::Vector2& position) const {
    if (projection_ == ProjectionReader::kFrozen) {
      return frozen_nearest_course_target(course_.track, position);
    }
    // This fixture explicitly authors one named road. No production binding/fallback is added.
    const auto* corridor = observation.terrain().find_corridor("road");
    if (corridor == nullptr) {
      throw std::logic_error{"projection proof fixture has no authored road"};
    }
    const auto projected = simulation::corridor_project_to_centreline(*corridor, position);
    return {projected.point, projected.distance};
  }

  FrozenCourse course_;
  double caution_fraction_;
  ProjectionReader projection_;
};

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] FrozenCourse straight_course() {
  return {
      80.0, {point(100.0, 320.0), point(800.0, 320.0)}, {point(300.0, 320.0), point(600.0, 320.0)}};
}

[[nodiscard]] FrozenCourse bent_course(const bool reverse = false) {
  return {80.0,
          reverse ? std::vector{point(500.0, 560.0), point(500.0, 320.0), point(100.0, 320.0)}
                  : std::vector{point(100.0, 320.0), point(500.0, 320.0), point(500.0, 560.0)},
          {point(300.0, 320.0), point(500.0, 520.0)}};
}

// Publication intentionally separates race objectives from explicit terrain authoring. The frozen
// course facts/reference above remain independent of the canonical projection and production reader.
[[nodiscard]] simulation::RaceModeState published_course(const FrozenCourse& frozen) {
  auto published = testing::straight_racer_course();
  published.checkpoints = frozen.checkpoints;
  return published;
}

[[nodiscard]] simulation::TerrainDefinition published_terrain(const FrozenCourse& frozen) {
  return testing::racer_observation_terrain(
      {simulation::TerrainCorridor::create("road", frozen.track_half_width, frozen.track)});
}

void check_command_bits(const std::vector<simulation::Command>& actual,
                        const std::vector<simulation::Command>& expected) {
  REQUIRE(actual.size() == expected.size());
  CHECK(actual == expected);
  for (std::size_t index = 0; index < actual.size(); ++index) {
    CHECK(simulation::command_kind_of(actual[index]) ==
          simulation::command_kind_of(expected[index]));
    CHECK(simulation::addressed_identity_of(actual[index]).ordering_key() ==
          simulation::addressed_identity_of(expected[index]).ordering_key());
    if (const auto* thrust = std::get_if<simulation::ThrustCommand>(&actual[index])) {
      const auto& expected_thrust = std::get<simulation::ThrustCommand>(expected[index]);
      CHECK(std::bit_cast<std::uint64_t>(thrust->direction.x()) ==
            std::bit_cast<std::uint64_t>(expected_thrust.direction.x()));
      CHECK(std::bit_cast<std::uint64_t>(thrust->direction.y()) ==
            std::bit_cast<std::uint64_t>(expected_thrust.direction.y()));
    }
  }
}

void check_decisions(const controllers::Observation& observation, const FrozenCourse& course,
                     const double caution_fraction) {
  for (const auto seed : {std::uint64_t{0}, std::uint64_t{19}}) {
    auto actual = controllers::RacerController::create(observation.controller(), seed,
                                                       {.caution_fraction = caution_fraction});
    ReferenceRacer frozen(observation.controller(), course, caution_fraction,
                          ProjectionReader::kFrozen);
    ReferenceRacer candidate(observation.controller(), course, caution_fraction,
                             ProjectionReader::kCanonical);
    // Repetition also exercises bodyless spawn-request consumption and deterministic decisions.
    for (int repetition = 0; repetition < 2; ++repetition) {
      const auto expected = frozen.decide(observation);
      check_command_bits(actual->decide(observation), expected);
      check_command_bits(candidate.decide(observation), expected);
      CHECK(actual->entity() == frozen.entity());
      CHECK(candidate.entity() == frozen.entity());
      CHECK(actual->last_spawn_request_tick() == frozen.last_spawn_request_tick());
      CHECK(candidate.last_spawn_request_tick() == frozen.last_spawn_request_tick());
    }
  }
}

} // namespace

TEST_CASE("racer projection promotion preserves frozen old racer command bits after delegation",
          "[unit][controllers][racer][terrain][promotion]") {
  struct Case final {
    std::string_view name;
    FrozenCourse course;
    simulation::Vector2 position;
    std::uint64_t checkpoint;
    double caution;
  };
  const std::vector<Case> cases{
      {"straight_gate", straight_course(), point(200.0, 320.0), 0, 0.75},
      {"later_gate", straight_course(), point(400.0, 320.0), 1, 0.75},
      {"caution_equal", straight_course(), point(200.0, 380.0), 0, 0.75},
      {"caution_below", straight_course(), point(200.0, std::nextafter(380.0, 0.0)), 0, 0.75},
      {"caution_above", straight_course(),
       point(200.0, std::nextafter(380.0, std::numeric_limits<double>::infinity())), 0, 0.75},
      {"recover", straight_course(), point(200.0, 381.0), 0, 0.75},
      {"personality", straight_course(), point(200.0, 361.0), 0, 0.5},
      {"first_clamp", straight_course(), point(60.0, 370.0), 0, 0.75},
      {"last_clamp", straight_course(), point(840.0, 370.0), 1, 0.75},
      {"gate_center", straight_course(), point(300.0, 320.0), 0, 0.75},
      {"finished", straight_course(), point(600.0, 381.0), 2, 0.75},
      {"bend_leg", bent_course(), point(550.0, 440.0), 1, 0.5},
      {"exact_tie", bent_course(), point(450.0, 370.0), 1, 0.5},
      {"tie_below", bent_course(), point(450.0, std::nextafter(370.0, 0.0)), 1, 0.5},
      {"tie_above", bent_course(),
       point(450.0, std::nextafter(370.0, std::numeric_limits<double>::infinity())), 1, 0.5},
      {"reversed_tie", bent_course(true), point(450.0, 370.0), 1, 0.5},
      {"sloped",
       {80.0,
        {point(100.0, 100.0), point(500.0, 400.0)},
        {point(260.0, 220.0), point(500.0, 400.0)}},
       point(280.0, 320.0),
       1,
       0.5},
      {"fractional",
       {80.0,
        {point(100.125, 100.375), point(700.625, 500.875)},
        {point(250.25, 200.5), point(550.5, 400.75)}},
       point(340.1875, 370.3125),
       1,
       0.5},
      {"fractional_gate",
       {80.0,
        {point(100.125, 100.375), point(700.625, 500.875)},
        {point(250.25, 200.5), point(550.5, 400.75)}},
       point(250.25, 200.5),
       1,
       1.0}};
  for (const auto& entry : cases) {
    DYNAMIC_SECTION(entry.name) {
      const auto observation = testing::racer_observation(testing::racer_observation_world(
          entry.position, entry.checkpoint, published_course(entry.course)),
          published_terrain(entry.course));
      const auto* corridor = observation.terrain().find_corridor("road");
      REQUIRE(corridor != nullptr);
      CHECK(corridor->half_width() == entry.course.track_half_width);
      CHECK(std::vector(corridor->points().begin(), corridor->points().end()) ==
            entry.course.track);
      const auto frozen = frozen_nearest_course_target(entry.course.track, entry.position);
      const auto canonical = simulation::corridor_project_to_centreline(*corridor, entry.position);
      CHECK(std::bit_cast<std::uint64_t>(canonical.point.x()) ==
            std::bit_cast<std::uint64_t>(frozen.point.x()));
      CHECK(std::bit_cast<std::uint64_t>(canonical.point.y()) ==
            std::bit_cast<std::uint64_t>(frozen.point.y()));
      CHECK(std::bit_cast<std::uint64_t>(canonical.distance) ==
            std::bit_cast<std::uint64_t>(frozen.distance));
      check_decisions(observation, entry.course, entry.caution);
    }
  }
}

TEST_CASE("racer projection promotion preserves complete old lifecycle and identity decisions",
          "[unit][controllers][racer][terrain][promotion]") {
  const auto course = straight_course();
  auto world = testing::racer_observation_world(point(200.0, 320.0), 0, published_course(course));
  SECTION("absent entity consumes a single spawn request") {
    world.destroy_entity(simulation::EntityId::create(1));
  }
  SECTION("missing body waits without a spawn request") {
    world.mutable_store<simulation::PhysicsBody>().erase(simulation::EntityId::create(1));
  }
  SECTION("missing progress waits on the grid") {
    world.mutable_store<simulation::RaceProgress>().erase(simulation::EntityId::create(1));
  }
  SECTION("other mode has no racer decision") {
    world.mutable_match().mode_state = simulation::NoModeState{};
  }
  check_decisions(testing::racer_observation(std::move(world), published_terrain(course)), course,
                   0.75);
}

TEST_CASE(
    "racer projection promotion joins the observed controller after an earlier finished racer",
    "[unit][controllers][racer][terrain][promotion]") {
  const auto course = bent_course();
  auto world = testing::race_test_world({point(700.0, 320.0), point(450.0, 370.0)});
  world.mutable_match().mode_state = published_course(course);
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(1),
                                                                   simulation::RaceProgress{2});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(simulation::EntityId::create(2),
                                                                   simulation::RaceProgress{1});
  check_decisions(testing::racer_observation(std::move(world), published_terrain(course), 2), course,
                   0.5);
}
