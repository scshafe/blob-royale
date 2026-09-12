#include "motion_body_envelope.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

using Violation = simulation::MotionBodyEnvelopeViolation;

simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }

simulation::PhysicsBody
body(const simulation::Vector2& position, const simulation::Vector2& velocity, double radius,
     bool is_static = false,
     simulation::BoundsBehavior bounds_behavior = simulation::BoundsBehavior::kFold) {
  return simulation::PhysicsBody::create(position, velocity, point(0, 0), radius, 1, 1, 1,
                                         is_static)
      .with_bounds_behavior(bounds_behavior);
}

// Frozen admission block from continuous_motion.cpp at dbc99ad. This intentionally does not call
// the promotion candidate: after live delegation it remains an independent old-path oracle.
// The return strings replace only fail_motion, preserving branch order and written arithmetic.
std::string_view frozen_admission_message(const simulation::PhysicsBody& subject_body,
                                          const simulation::ArenaBounds& bounds,
                                          double configured_radius) {
  const double radius = simulation::effective_radius(subject_body, configured_radius);
  if (subject_body.is_static()) {
    if (!bounds.contains(subject_body.position())) {
      return "static motion geometry lies outside the envelope";
    }
  } else if (!subject_body.crosses_bounds()) {
    if (!bounds.contains(subject_body.position()) || bounds.width() < 2.0 * radius ||
        bounds.height() < 2.0 * radius ||
        (bounds.width() == 2.0 * radius && subject_body.velocity().x() != 0.0) ||
        (bounds.height() == 2.0 * radius && subject_body.velocity().y() != 0.0)) {
      return "folding motion body does not fit its envelope";
    }
  }
  return {};
}

std::string_view candidate_admission_message(std::optional<Violation> violation) {
  if (!violation) {
    return {};
  }
  switch (*violation) {
  case Violation::kStaticOutsideEnvelope:
    return "static motion geometry lies outside the envelope";
  case Violation::kFoldingBodyDoesNotFitEnvelope:
    return "folding motion body does not fit its envelope";
  }
  FAIL("motion body envelope returned an undeclared violation");
  return {};
}

struct EnvelopeCase final {
  std::string_view name;
  simulation::ArenaBounds bounds;
  simulation::PhysicsBody subject;
  double configured_radius;
  std::optional<Violation> expected;
};

std::vector<EnvelopeCase> envelope_cases() {
  const auto roomy = simulation::ArenaBounds::create(10, 20);
  const auto zero_x_span = simulation::ArenaBounds::create(2, 20);
  const auto zero_y_span = simulation::ArenaBounds::create(20, 2);
  return {
      {"initial left wall overlap is legal", roomy, body(point(0.25, 10), point(-1, 0), 1), 1,
       std::nullopt},
      {"initial right wall overlap is legal", roomy, body(point(9.75, 10), point(1, 0), 1), 1,
       std::nullopt},
      {"closed boundary center is legal", roomy, body(point(0, 20), point(-1, 1), 1), 1,
       std::nullopt},
      {"folding center outside is rejected", roomy, body(point(-0.25, 10), point(1, 0), 1), 1,
       Violation::kFoldingBodyDoesNotFitEnvelope},
      {"dynamic crossing center and diameter are exempt", roomy,
       body(point(-10, 40), point(1, 1), 30, false, simulation::BoundsBehavior::kCross), 1,
       std::nullopt},
      {"static crossing center is not exempt", roomy,
       body(point(-10, 40), point(1, 1), 30, true, simulation::BoundsBehavior::kCross), 1,
       Violation::kStaticOutsideEnvelope},
      {"static diameter and stored motion are exempt", roomy,
       body(point(0, 20), point(1, 1), 30, true), 1, std::nullopt},
      {"declared oversized radius rejects folding", roomy, body(point(5, 10), point(0, 0), 6), 1,
       Violation::kFoldingBodyDoesNotFitEnvelope},
      {"declared radius takes precedence over configured radius", roomy,
       body(point(5, 10), point(0, 0), 1), 30, std::nullopt},
      {"undeclared radius defers to configured radius", roomy,
       body(point(5, 10), point(0, 0), simulation::PhysicsBody::kUndeclaredRadius), 6,
       Violation::kFoldingBodyDoesNotFitEnvelope},
      {"zero x span permits y motion and signed zero x", zero_x_span,
       body(point(0.25, 10), point(-0.0, 1), 1), 1, std::nullopt},
      {"zero x span rejects nonzero x motion", zero_x_span,
       body(point(1, 10), point(std::nextafter(0.0, 1.0), 0), 1), 1,
       Violation::kFoldingBodyDoesNotFitEnvelope},
      {"zero y span permits x motion and signed zero y", zero_y_span,
       body(point(10, 0.25), point(1, -0.0), 1), 1, std::nullopt},
      {"zero y span rejects nonzero y motion", zero_y_span,
       body(point(10, 1), point(0, std::nextafter(0.0, -1.0)), 1), 1,
       Violation::kFoldingBodyDoesNotFitEnvelope},
  };
}

} // namespace

TEST_CASE("body envelope preserves explicit static crossing folding and radius legality",
          "[unit][simulation][motion_body_envelope][promotion]") {
  for (const auto& fixture : envelope_cases()) {
    INFO(fixture.name);
    const auto original_body = fixture.subject;
    const auto result = simulation::motion_body_envelope_violation(fixture.subject, fixture.bounds,
                                                                   fixture.configured_radius);
    CHECK(result == fixture.expected);
    CHECK(candidate_admission_message(result) ==
          frozen_admission_message(fixture.subject, fixture.bounds, fixture.configured_radius));
    CHECK(fixture.subject == original_body);
  }
}

TEST_CASE("body envelope candidate matches frozen admission across representable boundary edges",
          "[unit][simulation][motion_body_envelope][promotion]") {
  const auto below_two = std::nextafter(2.0, 0.0);
  const auto above_two = std::nextafter(2.0, 3.0);
  const std::array dimensions{std::pair{2.0, 2.0},       std::pair{below_two, 2.0},
                              std::pair{above_two, 2.0}, std::pair{2.0, below_two},
                              std::pair{2.0, above_two}, std::pair{10.0, 20.0}};
  const std::array radii{simulation::PhysicsBody::kUndeclaredRadius,
                         0.25,
                         1.0,
                         std::nextafter(1.0, 0.0),
                         std::nextafter(1.0, 2.0),
                         16.0};
  const std::array velocities{point(0, -0.0), point(1, 0), point(0, -1),
                              point(std::nextafter(0.0, 1.0), std::nextafter(0.0, -1.0))};
  for (const auto& [width, height] : dimensions) {
    const auto bounds = simulation::ArenaBounds::create(width, height);
    const std::array positions{point(0, 0),
                               point(-0.0, height),
                               point(width, height),
                               point(width / 2, height / 2),
                               point(std::nextafter(0.0, -1.0), 0),
                               point(0, std::nextafter(0.0, -1.0)),
                               point(std::nextafter(width, 0.0), std::nextafter(height, 0.0)),
                               point(std::nextafter(width, width * 2), 0),
                               point(0, std::nextafter(height, height * 2))};
    for (const auto radius : radii) {
      for (const auto configured_radius : {0.5, 1.0, 2.0}) {
        for (const auto is_static : {false, true}) {
          for (const auto bounds_behavior :
               {simulation::BoundsBehavior::kFold, simulation::BoundsBehavior::kCross}) {
            for (const auto& position : positions) {
              for (const auto& velocity : velocities) {
                CAPTURE(width, height, radius, configured_radius, is_static);
                CAPTURE(position.x(), position.y(), velocity.x(), velocity.y());
                const auto subject = body(position, velocity, radius, is_static, bounds_behavior);
                CHECK(candidate_admission_message(simulation::motion_body_envelope_violation(
                          subject, bounds, configured_radius)) ==
                      frozen_admission_message(subject, bounds, configured_radius));
              }
            }
          }
        }
      }
    }
  }
}
