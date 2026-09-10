#include "motion_event_order.hpp"
#include "swept_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::MotionEventKey boundary(const double time,
                                                  const simulation::MotionEventPriority priority,
                                                  const std::uint64_t entity,
                                                  const std::uint64_t feature = 0) {
  return simulation::MotionEventKey::boundary(simulation::MotionTime::create(time), priority,
                                              simulation::EntityId::create(entity), feature);
}

} // namespace

TEST_CASE("motion time accepts only finite unit parameters and canonicalizes signed zero",
          "[unit][simulation][motion_event_order]") {
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<simulation::MotionTime>);
  CHECK(simulation::MotionTime::create(-0.0) == simulation::MotionTime::start());
  CHECK_FALSE(std::signbit(simulation::MotionTime::create(-0.0).value()));
  CHECK(simulation::MotionTime::create(1.0) == simulation::MotionTime::end());
  for (const double invalid :
       {std::nextafter(0.0, -1.0), std::nextafter(1.0, 2.0),
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_THROWS_AS(simulation::MotionTime::create(invalid), simulation::SimulationValidationError);
  }
}

TEST_CASE("motion order ranks exact time before priority and identity",
          "[unit][simulation][motion_event_order]") {
  const auto early =
      boundary(std::nextafter(0.5, 0.0), simulation::MotionEventPriority::kCheckpoint, 100);
  const auto later = boundary(0.5, simulation::MotionEventPriority::kSupportLoss, 1);
  CHECK(early < later);
  CHECK_FALSE(later < early);
  CHECK_FALSE(early == later);
}

TEST_CASE("motion ties rank support loss contacts x walls y walls then checkpoint credit",
          "[unit][simulation][motion_event_order]") {
  const auto time = simulation::MotionTime::create(0.5);
  const auto pair = simulation::CandidatePair::create(simulation::EntityId::create(1),
                                                      simulation::EntityId::create(2));
  const std::array ordered{boundary(0.5, simulation::MotionEventPriority::kSupportLoss, 20),
                           simulation::MotionEventKey::contact(time, pair),
                           boundary(0.5, simulation::MotionEventPriority::kWallX, 10),
                           boundary(0.5, simulation::MotionEventPriority::kWallY, 5),
                           boundary(0.5, simulation::MotionEventPriority::kCheckpoint, 1)};
  for (std::size_t first = 0; first < ordered.size(); ++first) {
    for (std::size_t second = first + 1; second < ordered.size(); ++second) {
      CHECK(ordered[first] < ordered[second]);
    }
  }
}

TEST_CASE("motion pair and feature identities are canonical final tie breakers",
          "[unit][simulation][motion_event_order]") {
  const auto first = simulation::EntityId::create(1);
  const auto second = simulation::EntityId::create(2);
  const auto third = simulation::EntityId::create(3);
  const auto time = simulation::MotionTime::start();
  const auto pair =
      simulation::MotionEventKey::contact(time, simulation::CandidatePair::create(second, first));
  CHECK(pair == simulation::MotionEventKey::contact(
                    time, simulation::CandidatePair::create(first, second)));
  CHECK(pair <
        simulation::MotionEventKey::contact(time, simulation::CandidatePair::create(first, third)));
  CHECK(boundary(0.0, simulation::MotionEventPriority::kSupportLoss, 1, 9) <
        boundary(0.0, simulation::MotionEventPriority::kSupportLoss, 2, 0));
  CHECK(boundary(0.0, simulation::MotionEventPriority::kSupportLoss, 1, 2) <
        boundary(0.0, simulation::MotionEventPriority::kSupportLoss, 1, 3));
}

TEST_CASE("motion order stays transitive across adversarial near-tied roots",
          "[unit][simulation][motion_event_order]") {
  const double center = 0.5;
  const std::array times{std::nextafter(center, 0.0), center, std::nextafter(center, 1.0),
                         center + 0.75e-9, center + 1.5e-9};
  const std::array priorities{simulation::MotionEventPriority::kSupportLoss,
                              simulation::MotionEventPriority::kWallX,
                              simulation::MotionEventPriority::kCheckpoint};
  std::vector<simulation::MotionEventKey> events;
  for (const double time : times) {
    for (const auto priority : priorities) {
      events.push_back(boundary(time, priority, 2, 1));
      events.push_back(boundary(time, priority, 1, 2));
    }
  }
  for (const auto& first : events) {
    CHECK_FALSE(first < first);
    for (const auto& second : events) {
      CHECK(((first < second) != (second < first) || first == second));
      for (const auto& third : events) {
        if (first < second && second < third) {
          CHECK(first < third);
        }
      }
    }
  }
  std::sort(events.begin(), events.end());
  const auto expected = events;
  std::reverse(events.begin(), events.end());
  std::sort(events.begin(), events.end());
  CHECK(events == expected);
}

TEST_CASE("circle and wall times feed the same exact tie order",
          "[unit][simulation][motion_event_order]") {
  const auto circle = simulation::swept_circle_boundary_roots(
      simulation::Vector2::create(-2.0, 0.0), simulation::Vector2::create(2.0, 0.0),
      simulation::Vector2::create(0.0, 0.0), 1.0);
  const auto line = simulation::swept_line_boundary_roots(-2.0, 2.0, -1.0);
  REQUIRE(circle.first().has_value());
  REQUIRE(line.first().has_value());
  CHECK(circle.first() == line.first());
  const auto falling = simulation::MotionEventKey::boundary(
      *circle.first(), simulation::MotionEventPriority::kSupportLoss,
      simulation::EntityId::create(9), 0);
  const auto wall = simulation::MotionEventKey::boundary(
      *line.first(), simulation::MotionEventPriority::kWallX, simulation::EntityId::create(1), 0);
  CHECK(falling < wall);
}

TEST_CASE("motion boundary construction rejects a contact or unknown priority",
          "[unit][simulation][motion_event_order]") {
  CHECK_THROWS_AS(boundary(0.0, simulation::MotionEventPriority::kBodyContact, 1),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(boundary(0.0, static_cast<simulation::MotionEventPriority>(255), 1),
                  simulation::SimulationValidationError);
}

TEST_CASE("motion roots map to the enclosing tick interval with canonical endpoint identity",
          "[unit][simulation][motion_event_order]") {
  const auto begin = simulation::MotionTime::create(0.25);
  const auto end = simulation::MotionTime::create(0.75);
  CHECK(simulation::map_motion_time(simulation::MotionTime::create(0.5), begin, end) ==
        simulation::MotionTime::create(0.5));
  CHECK(simulation::map_motion_time(simulation::MotionTime::start(), begin, end) == begin);
  CHECK(simulation::map_motion_time(simulation::MotionTime::end(), begin, end) == end);
  CHECK(simulation::map_motion_time(simulation::MotionTime::create(0.5), begin, begin) == begin);
  CHECK_THROWS_AS(simulation::map_motion_time(simulation::MotionTime::start(), end, begin),
                  simulation::SimulationValidationError);
}
