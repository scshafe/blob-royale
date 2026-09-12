#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "controller_observation_queries.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/tactical_observation_fixture.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "tactical_objective_candidates.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tactical_observation_fixture;

namespace {
// Every existing case keeps the policy Step 15 effectively had: no weights, no tolerance and a zero
// horizon, which casts no escape ray and predicts no hill, so those assertions still mean exactly
// what they meant.
[[nodiscard]] controllers::TacticalObjectiveCandidates
candidates(const fixture::Frame& frame, const controllers::TacticalObjectivePolicy& policy = {}) {
  const auto observation = fixture::observation(frame);
  const auto* body = controllers::find_observed_component<simulation::PhysicsBody>(
      observation.snapshot(), *observation.entity());
  REQUIRE(body != nullptr);
  return controllers::collect_tactical_objective_candidates(observation, *body, policy);
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

[[nodiscard]] controllers::TacticalObjectivePolicy
policy(const double weight, const double risk_tolerance, const std::uint64_t horizon_ticks) {
  controllers::TacticalObjectivePolicy value;
  value.objective_weights.fill(weight);
  value.risk_tolerance = risk_tolerance;
  value.prediction_horizon_ticks = horizon_ticks;
  return value;
}

[[nodiscard]] controllers::TacticalObjectiveCandidate
scored(const controllers::TacticalObjectiveKind kind, const std::uint64_t subject,
       const double normalized_distance, const bool escape_blocked) {
  return {{kind, subject}, simulation::Vector2::create(0.0, 0.0), 0.0, 0.0, normalized_distance,
          escape_blocked};
}

// The shared observation fixture publishes no `HillMotion`, and this step does not own that file,
// so the cases that need a committed hill velocity build their own world here. It is the fixture's
// own construction, narrowed to what an intercept needs; merge it into
// `fixtures/tactical_observation_fixture.hpp` as a per-circle velocity when that file next opens.
[[nodiscard]] controllers::Observation moving_hills(const std::size_t count,
                                                    const simulation::Vector2& velocity) {
  const auto self = simulation::EntityId::create(fixture::kEntity);
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      self, simulation::PhysicsBody::create(simulation::Vector2::create(200.0, 320.0), zero, zero),
      simulation::ControllerId::create(fixture::kController)));
  auto world = simulation::GameWorld::create(std::move(seeds));
  auto& match = world.mutable_match();
  match.phase = simulation::MatchPhase::kRunning;
  match.previous_phase = simulation::MatchPhase::kRunning;
  match.mode_state = simulation::KingOfTheHillModeState{};
  for (std::size_t index = 0; index < count; ++index) {
    const auto hill = simulation::EntityId::create(fixture::kFirstObjective + index);
    world.mutable_store<simulation::Hill>().insert_or_assign(
        hill, simulation::Hill{simulation::Vector2::create(600.0, 320.0), 20.0});
    world.mutable_store<simulation::HillMotion>().insert_or_assign(
        hill, simulation::HillMotion{velocity});
  }
  auto map = simulation::MapDefinition::create(
      "tactical_hill_motion_fixture",
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(960.0, 640.0)), {}, {},
      simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(fixture::kController));
}

[[nodiscard]] controllers::TacticalObjectiveCandidates
moving_hill_candidates(const controllers::Observation& observation,
                       const controllers::TacticalObjectivePolicy& value) {
  const auto* body = controllers::find_observed_component<simulation::PhysicsBody>(
      observation.snapshot(), *observation.entity());
  REQUIRE(body != nullptr);
  return controllers::collect_tactical_objective_candidates(observation, *body, value);
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

TEST_CASE("Tactical utility keeps its written multiply subtract add order and its unit terms",
          "[unit][controllers][tactical_objectives][utility]") {
  const auto tolerant = policy(0.8, 0.25, 0);
  const auto clear = scored(controllers::TacticalObjectiveKind::kHill, 1, 0.5, false);
  const auto blocked = scored(controllers::TacticalObjectiveKind::kHill, 2, 0.5, true);
  CHECK(controllers::tactical_candidate_score(clear, tolerant, false) ==
        ((0.8 * (1.0 - 0.5)) - 0.0) + 0.0);
  CHECK(controllers::tactical_candidate_score(blocked, tolerant, false) ==
        ((0.8 * (1.0 - 0.5)) - (1.0 - 0.25)) + 0.0);
  CHECK(controllers::tactical_candidate_score(clear, tolerant, true) ==
        ((0.8 * (1.0 - 0.5)) - 0.0) + controllers::kTacticalHeldTargetBonus);
  // A weight scales proximity and nothing else, so it is the term that decides a blocked near
  // candidate against a clear far one. This is the differentiation the step exists to make
  // possible, at the level where it is arithmetic rather than behaviour.
  const auto strict = policy(0.8, 0.0, 0);
  CHECK(controllers::tactical_candidate_score(blocked, strict, false) ==
        ((0.8 * (1.0 - 0.5)) - 1.0) + 0.0);
  const auto full_risk = policy(0.8, 1.0, 0);
  CHECK(controllers::tactical_candidate_score(blocked, full_risk, false) ==
        controllers::tactical_candidate_score(clear, full_risk, false));
}

TEST_CASE("Tactical selection takes the maximum and breaks exact ties on the stable kind ordinal",
          "[unit][controllers][tactical_objectives][utility]") {
  const auto even = policy(1.0, 1.0, 0);
  std::vector<controllers::TacticalObjectiveCandidate> set{
      scored(controllers::TacticalObjectiveKind::kHill, 7, 0.9, false),
      scored(controllers::TacticalObjectiveKind::kHill, 8, 0.1, false)};
  CHECK(controllers::tactical_select_candidate(set, even, std::nullopt) == 1);
  CHECK(controllers::tactical_select_candidate({}, even, std::nullopt) == 0);
  // Held targets win a comparison they would otherwise lose, by exactly the bonus and no more.
  const auto held = controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kHill, 7};
  set[0].normalized_distance = 0.2;
  CHECK(controllers::tactical_select_candidate(set, even, held) == 0);
  set[0].normalized_distance = 0.3;
  CHECK(controllers::tactical_select_candidate(set, even, held) == 1);
  // A per-kind weight orders two kinds directly, which is what Step 22b's opponent-derived provider
  // will rely on; today it is provable here because no running schema publishes two kinds at once.
  auto weighted = policy(0.0, 1.0, 0);
  weighted.objective_weights[controllers::tactical_objective_kind_ordinal(
      controllers::TacticalObjectiveKind::kZone)] = 1.0;
  std::vector<controllers::TacticalObjectiveCandidate> kinds{
      scored(controllers::TacticalObjectiveKind::kHill, 1, 0.1, false),
      scored(controllers::TacticalObjectiveKind::kZone, 2, 0.8, false)};
  CHECK(controllers::tactical_select_candidate(kinds, weighted, std::nullopt) == 1);
  // Exactly equal scores fall to the tie-break chain, whose tail is the key, and the answer cannot
  // depend on the order the provider happened to push them in.
  std::vector<controllers::TacticalObjectiveCandidate> tied{
      scored(controllers::TacticalObjectiveKind::kZone, 4, 0.5, false),
      scored(controllers::TacticalObjectiveKind::kHill, 3, 0.5, false)};
  const auto first = tied[controllers::tactical_select_candidate(tied, even, std::nullopt)].key;
  std::ranges::reverse(tied);
  const auto second = tied[controllers::tactical_select_candidate(tied, even, std::nullopt)].key;
  CHECK(first == second);
  CHECK(first.kind == controllers::TacticalObjectiveKind::kHill);
}

TEST_CASE("Tactical escape screening ranks down an overshoot into void without deleting it",
          "[unit][controllers][tactical_objectives][terrain]") {
  fixture::Frame frame;
  frame.terrain = fixture::terrain_with_hole(400.0, 30.0);
  frame.circles.front().x = 350.0;
  // The approach itself is supported; the published top speed of 600 world units a second carries
  // this bot 240 units in 160 ticks, which is past the target and into the hole's void.
  const auto blocked = candidates(frame, policy(1.0, 0.0, 160));
  REQUIRE(blocked.candidates.size() == 1);
  CHECK(blocked.candidates.front().escape_blocked);
  CHECK(blocked.work.raw_candidate_count == 1);
  CHECK(blocked.work.screened_candidate_count == 1);
  CHECK(blocked.work.prediction_step_count == 1);
  // The same geometry with a horizon that stops short of the void is not ranked down at all.
  const auto clear = candidates(frame, policy(1.0, 0.0, 100));
  REQUIRE(clear.candidates.size() == 1);
  CHECK_FALSE(clear.candidates.front().escape_blocked);
  CHECK(clear.work.prediction_step_count == 1);
  // A zero horizon casts no ray, and therefore counts no prediction.
  const auto none = candidates(frame, policy(1.0, 0.0, 0));
  REQUIRE(none.candidates.size() == 1);
  CHECK_FALSE(none.candidates.front().escape_blocked);
  CHECK(none.work.prediction_step_count == 0);
  // Screening writes the arena-relative distance the utility rule reads, clamped to the diagonal.
  const double diagonal = std::sqrt((960.0 * 960.0) + (640.0 * 640.0));
  CHECK(none.candidates.front().normalized_distance == 150.0 / diagonal);
}

TEST_CASE("Tactical hill intercept extrapolates the published committed velocity only",
          "[unit][controllers][tactical_objectives][hill]") {
  const auto center = simulation::Vector2::create(600.0, 320.0);
  CHECK(controllers::tactical_predicted_center(center, simulation::Vector2::create(40.0, -80.0),
                                               100) ==
        simulation::Vector2::create(600.0 + (40.0 * 0.25), 320.0 + (-80.0 * 0.25)));
  CHECK(controllers::tactical_predicted_center(center, simulation::Vector2::create(40.0, -80.0),
                                               0) == center);
  // A published velocity that cannot be extrapolated into a representable point is refused rather
  // than thrown on: the host isolates a throw and the bot would stop acting for the pass.
  CHECK(controllers::tactical_predicted_center(
            center,
            simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude, 0.0),
            4'000) == center);
  const auto observation = moving_hills(1, simulation::Vector2::create(40.0, 0.0));
  const auto predicted = moving_hill_candidates(observation, policy(1.0, 1.0, 100));
  REQUIRE(predicted.candidates.size() == 1);
  CHECK(predicted.candidates.front().target == simulation::Vector2::create(610.0, 320.0));
  CHECK(predicted.candidates.front().squared_distance == 410.0 * 410.0);
  CHECK(predicted.work.prediction_step_count == 2);
  // A zero horizon holds the published centre, so every stationary-hill behaviour is unchanged.
  const auto held = moving_hill_candidates(observation, policy(1.0, 1.0, 0));
  REQUIRE(held.candidates.size() == 1);
  CHECK(held.candidates.front().target == simulation::Vector2::create(600.0, 320.0));
  CHECK(held.work.prediction_step_count == 0);
}

TEST_CASE("Tactical bounded work reaches its derived ceiling and never passes it",
          "[unit][controllers][tactical_objectives][limits]") {
  const auto observation = moving_hills(controllers::kMaximumTacticalObjectiveCandidateCount,
                                        simulation::Vector2::create(40.0, 0.0));
  const auto full = moving_hill_candidates(observation, policy(1.0, 1.0, 100));
  CHECK(full.work.raw_candidate_count == controllers::kMaximumTacticalObjectiveCandidateCount);
  CHECK(full.work.screened_candidate_count == controllers::kMaximumTacticalObjectiveCandidateCount);
  // One intercept per raw candidate and one escape ray per screened candidate is the ceiling, and
  // this observation sits exactly on it.
  CHECK(full.work.prediction_step_count == controllers::kMaximumTacticalPredictionStepCount);
  CHECK(full.work.prediction_step_count <= controllers::kMaximumTacticalPredictionStepCount);
}
