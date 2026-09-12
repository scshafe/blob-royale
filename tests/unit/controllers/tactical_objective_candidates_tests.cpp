#include "controller_observation_queries.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/tactical_observation_fixture.hpp"
#include "tactical_objective_candidates.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tactical_observation_fixture;

namespace {
[[nodiscard]] controllers::TacticalObjectiveCandidates candidates(const fixture::Frame& frame) {
  const auto observation = fixture::observation(frame);
  const auto* body = controllers::find_observed_component<simulation::PhysicsBody>(
      observation.snapshot(), *observation.entity());
  REQUIRE(body != nullptr);
  return controllers::collect_tactical_objective_candidates(observation, *body);
}
void require_rejection(const fixture::Frame& frame,
                       const controllers::ControllersValidationCode code) {
  try {
    static_cast<void>(candidates(frame));
    FAIL("invalid published objective must fail visibly");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() == code);
  }
}
} // namespace

TEST_CASE("Tactical circle providers preserve public centers radii subjects and ordered ties",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.x = 400.0;
  frame.circles = {{fixture::kSecondObjective, 500.0, 320.0, 0.0},
                   {fixture::kFirstObjective, 300.0, 320.0, 20.0}};
  const auto hill = candidates(frame);
  REQUIRE(hill.candidates.size() == 2);
  const auto best =
      std::ranges::min_element(hill.candidates, controllers::tactical_candidate_precedes);
  CHECK(best->key == controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kHill,
                                                       fixture::kFirstObjective});
  CHECK(best->squared_distance == 10'000.0);
  CHECK(best->target == simulation::Vector2::create(300.0, 320.0));
  CHECK(hill.candidates[1].arrival_radius == 0.0);
  frame.mode = fixture::Mode::kZone;
  const auto zone = candidates(frame);
  REQUIRE(zone.candidates.size() == 2);
  CHECK(zone.candidates.front().key.kind == controllers::TacticalObjectiveKind::kZone);
  CHECK(controllers::tactical_candidate_precedes(hill.candidates.front(), zone.candidates.front()));
  CHECK_FALSE(
      controllers::tactical_candidate_precedes(zone.candidates.front(), hill.candidates.front()));
}

TEST_CASE(
    "Tactical candidate screening rejects unsupported targets and supported targets across void",
    "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.terrain = fixture::terrain_with_hole(600.0, 30.0);
  CHECK(candidates(frame).candidates.empty());
  frame.terrain = fixture::terrain_with_hole(400.0, 30.0);
  CHECK(candidates(frame).candidates.empty());
  frame.circles.front().x = 350.0;
  CHECK(candidates(frame).candidates.size() == 1);
  frame.circles.front().x = 370.0; // Canonical exact supported rim, no void before the endpoint.
  CHECK(candidates(frame).candidates.size() == 1);
}

TEST_CASE("Tactical providers refuse thirty three candidates before terrain filtering and reject "
          "malformed radii",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.circles.clear();
  for (std::size_t index = 0; index < controllers::kMaximumTacticalObjectiveCandidateCount;
       ++index) {
    frame.circles.push_back({fixture::kFirstObjective + index, 600.0, 320.0, 0.0});
  }
  CHECK(candidates(frame).candidates.size() ==
        controllers::kMaximumTacticalObjectiveCandidateCount);
  frame.circles.push_back(
      {fixture::kFirstObjective + controllers::kMaximumTacticalObjectiveCandidateCount, 600.0,
       320.0, 0.0});
  frame.terrain = fixture::terrain_with_hole(600.0, 30.0);
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalCandidateLimitExceeded);
  frame = fixture::Frame{};
  frame.circles.front().radius = -1.0;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.mode = fixture::Mode::kUnsupported;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalModeUnsupported);
}

TEST_CASE("Tactical race provider uses exact road binding progress and strict recovery threshold",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.mode = fixture::Mode::kRace;
  frame.checkpoint = 1;
  auto result = candidates(frame);
  REQUIRE(result.candidates.size() == 1);
  CHECK(result.candidates.front().key ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kRaceGate, 1});
  CHECK(result.candidates.front().target == simulation::Vector2::create(600.0, 320.0));
  frame.y = 380.0;
  CHECK(candidates(frame).candidates.front().key.kind ==
        controllers::TacticalObjectiveKind::kRaceGate);
  frame.y = std::nextafter(380.0, std::numeric_limits<double>::infinity());
  result = candidates(frame);
  REQUIRE(result.candidates.size() == 1);
  CHECK(result.candidates.front().key.kind == controllers::TacticalObjectiveKind::kRaceRecovery);
  CHECK(result.candidates.front().target == simulation::Vector2::create(frame.x, 320.0));
  CHECK(result.candidates.front().arrival_radius == 0.0);
  frame.checkpoint = 2;
  CHECK(candidates(frame).disposition == controllers::TacticalObjectiveDisposition::kFinished);
  frame.race_progress = false;
  CHECK(candidates(frame).disposition == controllers::TacticalObjectiveDisposition::kWaiting);
}

TEST_CASE("Tactical race provider fails missing bindings invalid progress and invalid gate radius",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.mode = fixture::Mode::kRace;
  frame.course = fixture::race_course();
  frame.course->road = simulation::RaceRoadName::create("missing_lane");
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.course = fixture::race_course();
  frame.checkpoint = 3;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.checkpoint = 0;
  frame.course->checkpoint_radius = 0.0;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.course->checkpoint_radius = 81.0;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.course->checkpoints.clear();
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
}
