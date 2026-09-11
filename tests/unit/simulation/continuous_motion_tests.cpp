#include "continuous_motion.hpp"
#include "motion_triggers.hpp"
#include "simulation_tolerance.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

using Subject = simulation::ContactRule::Subject;
using Effect = std::uint64_t;
simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }
Subject moving(std::uint64_t id, double x, double y, double vx = 0.0, double vy = 0.0) {
  return {simulation::EntityId::create(id),
          simulation::PhysicsBody::create(point(x, y), point(vx, vy), point(0.0, 0.0))
              .with_radius(1.0)};
}
Subject fixed(std::uint64_t id, double x, double y) {
  return {simulation::EntityId::create(id),
          simulation::PhysicsBody::create_static(point(x, y)).with_radius(1.0)};
}
simulation::TerrainDefinition solid() {
  return simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(100.0, 100.0));
}

struct Fixture final {
  simulation::SimulationConfig config =
      simulation::SimulationConfig::create(100.0, 100.0, 1.0, 400, 1, 1, 0.0);
  simulation::GameWorld world = simulation::GameWorld::create({});
  simulation::MapDefinition map;
  simulation::SpatialGrid grid;
  simulation::TickContext context;
  explicit Fixture(simulation::TerrainDefinition terrain = solid())
      : map(simulation::MapDefinition::create("motion", std::move(terrain), {}, {},
                                              simulation::MapMetadata::none())),
        grid(simulation::SpatialGrid::create(config, map.bounds(), world)),
        context(simulation::TickContext::create(simulation::TickSequence::create(1),
                                                simulation::FixedDelta::canonical(), config, map,
                                                grid)) {}
};

enum class PairBehavior {
  kElastic,
  kUnchanged,
  kTeleport,
  kTerminate,
  kMass,
  kDisposition,
  kRetainedTangent,
  kCaptureNormal,
  kEligibleEffects,
  kNoEffects,
  kTerminateEligibleRecipient,
  kRetainedApproach,
  kSelfVelocityChange
};
enum class ScriptBehavior {
  kAcceleration,
  kVelocity,
  kNoProgress,
  kVelocityOnly,
  kPast,
  kBackwardCursor,
  kExcessCursor
};
struct Facts final {
  PairBehavior pair = PairBehavior::kElastic;
  std::vector<simulation::MotionCircleGate> gates;
  bool finish = true;
  simulation::MotionTime script_time = simulation::MotionTime::create(0.1);
  ScriptBehavior script = ScriptBehavior::kAcceleration;
  std::vector<simulation::MotionContactEffectPolicy> effect_policies;
  bool capture_observation = false;
};
using Trigger = simulation::MotionTrigger<Effect, Facts>;
using Result = simulation::ContinuousMotionResult<Effect>;

simulation::PairMotionResponse<Effect>
respond_pair(const simulation::GameWorld&, const Subject& first, const Subject& second,
             const simulation::PairContactObservation& observation, const simulation::TickContext&,
             const Facts& facts) {
  simulation::PairMotionResponse<Effect> response{
      {first.body}, {second.body}, {first.entity.value() * 1000 + second.entity.value()}};
  if (facts.capture_observation) {
    response.effects.push_back(observation.impact.has_value());
    response.effects.push_back(observation.first_effect_eligible);
    response.effects.push_back(observation.second_effect_eligible);
  }
  if (facts.pair == PairBehavior::kNoEffects) {
    response.effects.clear();
    return response;
  }
  if (facts.pair == PairBehavior::kTerminateEligibleRecipient) {
    if (observation.first_effect_eligible) {
      response.second.disposition = simulation::MotionDisposition::kTerminate;
    }
    if (observation.second_effect_eligible) {
      response.first.disposition = simulation::MotionDisposition::kTerminate;
    }
    return response;
  }
  if (facts.pair == PairBehavior::kEligibleEffects) {
    response.effects.clear();
    if (observation.first_effect_eligible) {
      response.effects.push_back(first.entity.value());
    }
    if (observation.second_effect_eligible) {
      response.effects.push_back(second.entity.value());
    }
  }
  if (facts.pair == PairBehavior::kUnchanged) {
    return response;
  }
  if (facts.pair == PairBehavior::kRetainedTangent) {
    if (first.entity.value() == 1 && second.entity.value() == 2) {
      response.second.body = second.body.with_velocity(point(-9.6e9, 2.8e9));
    }
    return response;
  }
  if (facts.pair == PairBehavior::kRetainedApproach) {
    if (first.entity.value() == 1 && second.entity.value() == 2) {
      response.first.disposition = simulation::MotionDisposition::kTerminate;
      response.second.body = second.body.with_velocity(point(4000, 0));
    } else {
      response.first.disposition = simulation::MotionDisposition::kTerminate;
      response.second.disposition = simulation::MotionDisposition::kTerminate;
    }
    return response;
  }
  if (facts.pair == PairBehavior::kSelfVelocityChange) {
    response.first.body = first.body.with_velocity(point(-4000, 0));
    return response;
  }
  if (!observation.impact) {
    return response;
  }
  const auto& contact = *observation.impact;
  if (facts.pair == PairBehavior::kCaptureNormal) {
    response.effects.push_back(std::bit_cast<std::uint64_t>(contact.normal().x()));
    response.effects.push_back(std::bit_cast<std::uint64_t>(contact.normal().y()));
    response.first.disposition = simulation::MotionDisposition::kTerminate;
    response.second.disposition = simulation::MotionDisposition::kTerminate;
    return response;
  }
  if (facts.pair == PairBehavior::kTeleport) {
    response.first.body = first.body.with_position(first.body.position() + point(1.0, 0.0));
    return response;
  }
  if (facts.pair == PairBehavior::kMass) {
    response.first.body = first.body.with_mass(2.0);
    return response;
  }
  if (facts.pair == PairBehavior::kDisposition) {
    response.first.disposition = static_cast<simulation::MotionDisposition>(77);
    return response;
  }
  if (facts.pair == PairBehavior::kTerminate) {
    response.first.disposition = simulation::MotionDisposition::kTerminate;
    response.second.disposition = simulation::MotionDisposition::kTerminate;
    return response;
  }
  if (first.body.is_static()) {
    response.second.body = second.body.with_velocity(
        simulation::reflect_static_contact_velocity(second.body.velocity(), contact.normal()));
  } else if (second.body.is_static()) {
    response.first.body = first.body.with_velocity(
        simulation::reflect_static_contact_velocity(first.body.velocity(), contact.normal()));
  } else {
    const auto impulse =
        simulation::resolve_player_pair_collision(first.body, second.body, contact);
    response.first.body = first.body.with_velocity(impulse.first_velocity());
    response.second.body = second.body.with_velocity(impulse.second_velocity());
  }
  return response;
}

std::optional<simulation::MotionTriggerProposal>
query_gate(const simulation::GameWorld&, const Subject&,
           const simulation::MotionTriggerWindow& window, const simulation::TickContext&,
           const Facts& facts, std::uint64_t cursor, simulation::MotionQueryBudget& budget) {
  return simulation::ordered_gate_motion_trigger(facts.gates, cursor, window, budget);
}
simulation::MotionTriggerResponse<Effect> respond_gate(const simulation::GameWorld&,
                                                       const Subject& body,
                                                       const simulation::MotionTriggerEvent& event,
                                                       const simulation::TickContext&,
                                                       const Facts& facts) {
  const auto cursor = event.cursor + 1;
  return {{body.body, facts.finish && cursor == facts.gates.size()
                          ? simulation::MotionDisposition::kTerminate
                          : simulation::MotionDisposition::kContinue},
          cursor,
          {100 + event.cursor}};
}
std::optional<simulation::MotionTriggerProposal>
query_support(const simulation::GameWorld&, const Subject&,
              const simulation::MotionTriggerWindow& window, const simulation::TickContext& context,
              const Facts&, std::uint64_t cursor, simulation::MotionQueryBudget& budget) {
  return cursor == 0
             ? simulation::support_loss_motion_trigger(context.map().terrain(), window, budget)
             : std::nullopt;
}
simulation::MotionTriggerResponse<Effect> respond_support(const simulation::GameWorld&,
                                                          const Subject& body,
                                                          const simulation::MotionTriggerEvent&,
                                                          const simulation::TickContext&,
                                                          const Facts&) {
  return {{body.body, simulation::MotionDisposition::kTerminate}, 1, {300}};
}
std::optional<simulation::MotionTriggerProposal>
query_script(const simulation::GameWorld&, const Subject& body,
             const simulation::MotionTriggerWindow&, const simulation::TickContext&,
             const Facts& facts, std::uint64_t cursor, simulation::MotionQueryBudget&) {
  if (facts.script == ScriptBehavior::kPast && cursor != 0) {
    return simulation::MotionTriggerProposal{simulation::MotionTime::start(),
                                             simulation::MotionEventPriority::kCheckpoint};
  }
  if ((cursor != 0 && facts.script != ScriptBehavior::kBackwardCursor) ||
      (facts.script == ScriptBehavior::kVelocityOnly && body.body.velocity().x() <= 0.0)) {
    return std::nullopt;
  }
  return simulation::MotionTriggerProposal{facts.script_time,
                                           simulation::MotionEventPriority::kCheckpoint};
}
simulation::MotionTriggerResponse<Effect>
respond_script(const simulation::GameWorld&, const Subject& body,
               const simulation::MotionTriggerEvent& event, const simulation::TickContext&,
               const Facts& facts) {
  auto changed = body.body;
  if (facts.script == ScriptBehavior::kBackwardCursor ||
      facts.script == ScriptBehavior::kExcessCursor) {
    return {{changed}, facts.script == ScriptBehavior::kBackwardCursor ? 0U : 2U, {400}};
  }
  if (facts.script == ScriptBehavior::kAcceleration ||
      facts.script == ScriptBehavior::kNoProgress) {
    changed = changed.with_acceleration(point(123.0, 0.0));
  } else {
    changed = changed.with_velocity(point(-changed.velocity().x(), changed.velocity().y()));
  }
  return {{changed},
          event.cursor + (facts.script == ScriptBehavior::kNoProgress ||
                                  facts.script == ScriptBehavior::kVelocityOnly
                              ? 0
                              : 1),
          {400}};
}

Result solve(const Fixture& fixture, const std::vector<Subject>& bodies, const Facts& facts = {},
             const std::vector<Trigger>& triggers = {}, simulation::MotionLimits limits = {}) {
  return simulation::solve_continuous_motion<Effect, Facts>(fixture.world, bodies, fixture.context,
                                                            facts, respond_pair, triggers, limits,
                                                            facts.effect_policies);
}
simulation::MotionContactEffectPolicy any_touch(std::uint64_t id) {
  return {simulation::EntityId::create(id), simulation::ContactEffectPolicy::kAnyTouch};
}
std::vector<Effect> effects(const Result& result) {
  std::vector<Effect> values;
  for (const auto& effect : result.effects) {
    values.push_back(effect.effect);
  }
  return values;
}
const simulation::MotionBodyResult& body(const Result& result, std::uint64_t id) {
  const auto found = std::find_if(result.motion.bodies.begin(), result.motion.bodies.end(),
                                  [id](const auto& entry) { return entry.entity.value() == id; });
  REQUIRE(found != result.motion.bodies.end());
  return found->result;
}
Trigger gate(std::uint64_t id, std::uint64_t count) {
  return {simulation::EntityId::create(id), 100, 0, count, query_gate, respond_gate};
}
Trigger support(std::uint64_t id) {
  return {simulation::EntityId::create(id), 10, 0, 1, query_support, respond_support};
}
Trigger script(std::uint64_t id) {
  return {simulation::EntityId::create(id), 200, 0, 1, query_script, respond_script};
}
template <class Action> void rejected(Action&& action, simulation::SimulationValidationCode code) {
  try {
    static_cast<void>(std::forward<Action>(action)());
    FAIL("invalid continuous motion was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == code);
  }
}

} // namespace

TEST_CASE("continuous swept candidates prevent high speed tunneling and preserve pure inputs",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto before = fixture.world;
  const std::vector input{moving(1, 10, 50, 20000), moving(2, 40, 50)};
  const auto result = solve(fixture, input);
  REQUIRE(result.effects.size() == 1);
  CHECK(result.effects.front().effect == 1002);
  CHECK(result.effects.front().event.time().value() == Catch::Approx(0.56));
  CHECK(body(result, 1).body.position().x() == Catch::Approx(38.0));
  CHECK(body(result, 2).body.position().x() == Catch::Approx(62.0));
  CHECK(result.motion.work.maximum_candidate_pairs == 1);
  CHECK(fixture.world == before);
  CHECK(input.front().body.position() == point(10, 50));
}

TEST_CASE("continuous overlaps admit only closing contact and never depenetrate",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  Facts facts;
  facts.pair = PairBehavior::kUnchanged;
  const auto closing = solve(fixture, {moving(1, 20, 50, 100), moving(2, 21, 50)}, facts);
  REQUIRE(closing.effects.size() == 1);
  CHECK(closing.effects.front().event.time() == simulation::MotionTime::start());
  CHECK(body(closing, 1).body.position().x() == 20.25);
  CHECK(closing.motion.work.events == 1);
  CHECK(solve(fixture, {moving(1, 20, 50, -100), moving(2, 21, 50, 100)}).effects.empty());
  CHECK(solve(fixture, {moving(1, 20, 50), moving(2, 21, 50)}).effects.empty());
  CHECK(solve(fixture, {moving(1, 20, 20, 4000), fixed(2, 25, 22)}).effects.empty());
}

TEST_CASE("continuous walls order corners and preserve stored static motion fields",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto corner = solve(fixture, {moving(1, 90, 90, 8000, 8000)});
  REQUIRE(corner.motion.events.size() == 2);
  CHECK(corner.motion.events[0].priority() == simulation::MotionEventPriority::kWallX);
  CHECK(corner.motion.events[1].priority() == simulation::MotionEventPriority::kWallY);
  CHECK(corner.motion.events[0].time() == corner.motion.events[1].time());
  CHECK(body(corner, 1).body.position().x() == Catch::Approx(88.0));
  CHECK(body(corner, 1).body.position().y() == Catch::Approx(88.0));
  CHECK(corner.motion.paths.size() == 2);
  auto obstacle = fixed(2, 25, 22);
  obstacle.body = obstacle.body.with_velocity(point(9000, -7000)).with_acceleration(point(30, 40));
  const auto stationary = solve(fixture, {moving(1, 20, 20, 4000), obstacle});
  CHECK(stationary.effects.empty());
  CHECK(body(stationary, 2).body == obstacle.body);
}

TEST_CASE("wall penetration reflects only outward motion without relocation",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto outward = solve(fixture, {moving(1, 0.5, 50, -100)});
  REQUIRE(outward.motion.events.size() == 1);
  CHECK(outward.motion.events.front().time() == simulation::MotionTime::start());
  CHECK(body(outward, 1).body.position().x() == 0.75);
  const auto inward = solve(fixture, {moving(1, 0.5, 50, 100)});
  CHECK(inward.motion.events.empty());
  CHECK(body(inward, 1).body.position().x() == 0.75);
  rejected([&] { return solve(fixture, {moving(1, -0.5, 50, 100)}); },
           simulation::SimulationValidationCode::kContinuousMotionInvalidInput);
}

TEST_CASE("body contact precedes a certified tied wall", "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto result = solve(fixture, {moving(1, 89, 50, 8000), moving(2, 99, 56, 0, -3200)});
  REQUIRE(result.motion.events.size() == 2);
  CHECK(result.motion.events[0].priority() == simulation::MotionEventPriority::kBodyContact);
  CHECK(result.motion.events[1].priority() == simulation::MotionEventPriority::kWallX);
  CHECK(result.motion.events[0].time() == simulation::MotionTime::create(0.5));
  CHECK(result.motion.events[1].time() == result.motion.events[0].time());
  CHECK(body(result, 1).body.position() == point(89, 46));
  CHECK(body(result, 2).body.position() == point(99, 52));
}

TEST_CASE("declining a tied outward wall still schedules the opposite future wall",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture(
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(10, 10)));
  auto crossing = moving(2, 15, 5, -16000);
  crossing.body = crossing.body.with_bounds_behavior(simulation::BoundsBehavior::kCross);
  const auto result = solve(fixture, {moving(1, 5, 5, 16000), crossing});
  REQUIRE(result.effects.size() == 1);
  CHECK(result.effects.front().event.time() == simulation::MotionTime::create(0.1));
  REQUIRE(result.motion.events.size() == 6);
  CHECK(result.motion.events[1].priority() == simulation::MotionEventPriority::kWallX);
  CHECK(result.motion.events[1].time() == result.motion.events[0].time());
  CHECK(body(result, 1).body.position().x() == Catch::Approx(5.0));
  CHECK(body(result, 1).body.velocity().x() == -16000);
}

TEST_CASE("only an external velocity revision reenables a processed pair",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto result =
      solve(fixture, {moving(1, 20, 50, 4000), moving(2, 24, 50), moving(3, 28, 50, -4000)});
  REQUIRE(result.effects.size() == 3);
  CHECK(result.effects[0].effect == 1002);
  CHECK(result.effects[1].effect == 2003);
  CHECK(result.effects[2].effect == 1002);
  for (const auto& effect : result.effects) {
    CHECK(effect.event.time() == simulation::MotionTime::create(0.2));
  }
  CHECK(body(result, 1).body.position().x() == Catch::Approx(14.0));
  CHECK(body(result, 2).body.position().x() == Catch::Approx(24.0));
  CHECK(body(result, 3).body.position().x() == Catch::Approx(34.0));
}

TEST_CASE("terminal contact chains precede finish without inventing remaining travel",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  auto crossing = [](Subject subject) {
    subject.body = subject.body.with_bounds_behavior(simulation::BoundsBehavior::kCross);
    return subject;
  };
  const auto first = crossing(moving(1, 10, 50, 400));
  const auto second = crossing(moving(2, 13, 50));
  Facts facts;
  facts.gates = {{point(12, 50), 1.0 - simulation::kPositionTolerance}};
  const auto chained =
      solve(fixture, {first, second, crossing(moving(3, 15, 50))}, facts, {gate(1, 1)});
  REQUIRE(chained.effects.size() == 3);
  CHECK(chained.effects[0].effect == 1002);
  CHECK(chained.effects[1].effect == 2003);
  CHECK(chained.effects[2].effect == 100);
  for (const auto& effect : chained.effects) {
    CHECK(effect.event.time() == simulation::MotionTime::end());
  }
  CHECK(body(chained, 1).body.position() == point(11, 50));
  CHECK(body(chained, 2).body.position() == point(13, 50));
  CHECK(body(chained, 3).body.position() == point(15, 50));
  CHECK(body(chained, 3).body.velocity() == point(400, 0));
  CHECK(body(chained, 1).disposition == simulation::MotionDisposition::kTerminate);
  for (const auto third_position : {point(13, 52), point(14.5, 51.5)}) {
    // Respectively an initial reference-line tangent, and an outside secant with a future
    // reference entry. Both AABBs overlap B; neither can become a terminal impact.
    const auto third = crossing(moving(3, third_position.x(), third_position.y()));
    const auto no_future = solve(fixture, {first, second, third});
    REQUIRE(no_future.effects.size() == 1);
    CHECK(no_future.effects.front().effect == 1002);
    CHECK(body(no_future, 2).body.position() == point(13, 50));
    CHECK(body(no_future, 3).body.position() == third_position);
  }
}

TEST_CASE("finish terminates before a later collision and nonlethal contact retains a tied gate",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  Facts facts;
  facts.gates = {{point(21, 50), 1.0 - simulation::kPositionTolerance}};
  const auto finished =
      solve(fixture, {moving(1, 10, 50, 20000), fixed(2, 40, 50)}, facts, {gate(1, 1)});
  REQUIRE(finished.effects.size() == 1);
  CHECK(finished.effects.front().effect == 100);
  CHECK(body(finished, 1).disposition == simulation::MotionDisposition::kTerminate);
  CHECK(body(finished, 1).body.position().x() == Catch::Approx(20.0));
  facts.gates = {{point(23, 50), 1.0 - simulation::kPositionTolerance}};
  facts.finish = false;
  const auto tied =
      solve(fixture, {moving(1, 10, 50, 8000), fixed(2, 24, 50)}, facts, {gate(1, 1)});
  REQUIRE(tied.effects.size() == 2);
  CHECK(tied.effects[0].effect == 1002);
  CHECK(tied.effects[1].effect == 100);
  CHECK(tied.effects[0].event.time() == tied.effects[1].event.time());
  CHECK(body(tied, 1).body.position().x() == Catch::Approx(14.0));
  CHECK(tied.trigger_cursors == std::vector<std::uint64_t>{1});
}

TEST_CASE("overlapping next gates are eligible at previous credit and finish stops there",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  Facts facts;
  facts.gates = {{point(20, 50), 2.0 - simulation::kPositionTolerance},
                 {point(19, 50), 2.0 - simulation::kPositionTolerance}};
  const auto result = solve(fixture, {moving(1, 10, 50, 8000)}, facts, {gate(1, 2)});
  REQUIRE(result.effects.size() == 2);
  CHECK(result.effects[0].effect == 100);
  CHECK(result.effects[1].effect == 101);
  CHECK(result.effects[0].event.time() == result.effects[1].event.time());
  CHECK(result.trigger_cursors == std::vector<std::uint64_t>{2});
  CHECK(body(result, 1).disposition == simulation::MotionDisposition::kTerminate);
  CHECK(body(result, 1).body.position().x() == Catch::Approx(18.0));
}

TEST_CASE("swept candidates honor masks static exclusion actual radii and canonical permutation",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  auto masked = moving(1, 10, 50, 8000);
  masked.body = simulation::PhysicsBody::create(masked.body.position(), masked.body.velocity(),
                                                point(0, 0), 1.0, 1.0, 1, 0, false);
  const auto rejected_pair = solve(fixture, {masked, fixed(2, 20, 50)});
  CHECK(rejected_pair.effects.empty());
  CHECK(rejected_pair.motion.work.maximum_candidate_pairs == 0);
  CHECK(solve(fixture, {fixed(1, 20, 50), fixed(2, 20, 50)}).effects.empty());
  auto large = fixed(2, 30, 50);
  large.body = large.body.with_radius(3.0);
  const auto canonical = solve(fixture, {moving(1, 10, 50, 8000), large});
  const auto reversed = solve(fixture, {large, moving(1, 10, 50, 8000)});
  REQUIRE(canonical.effects.size() == 1);
  CHECK(canonical.effects.front().event.time().value() == Catch::Approx(0.8));
  CHECK(body(canonical, 1).body.position().x() == Catch::Approx(22.0));
  CHECK(canonical.motion.bodies == reversed.motion.bodies);
  CHECK(canonical.motion.events == reversed.motion.events);
  CHECK(canonical.motion.paths == reversed.motion.paths);
}

TEST_CASE("support loss wins a gate tie across a narrow hole after earlier credit",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture(simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(100, 100), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("pit", point(30, 50),
                                       1.0 + simulation::kPositionTolerance)}));
  Facts facts;
  facts.gates = {{point(20, 50), 1.0 - simulation::kPositionTolerance},
                 {point(30, 50), 1.0 - simulation::kPositionTolerance}};
  const auto result =
      solve(fixture, {moving(1, 10, 50, 20000), fixed(2, 40, 50)}, facts, {support(1), gate(1, 2)});
  REQUIRE(result.effects.size() == 2);
  CHECK(result.effects[0].effect == 100);
  CHECK(result.effects[1].effect == 300);
  CHECK(result.effects[1].event.priority() == simulation::MotionEventPriority::kSupportLoss);
  CHECK(body(result, 1).body.position().x() == Catch::Approx(29.0));
  CHECK(body(result, 1).disposition == simulation::MotionDisposition::kTerminate);
  CHECK(result.trigger_cursors == std::vector<std::uint64_t>{1, 1});
}

TEST_CASE("support loss precedes a tied body and exact final rim remains supported",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture(simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(100, 100), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("pit", point(30, 50),
                                       1.0 + simulation::kPositionTolerance)}));
  const auto tied = solve(fixture, {moving(1, 10, 50, 20000), fixed(2, 31, 50)}, {}, {support(1)});
  REQUIRE(tied.effects.size() == 1);
  CHECK(tied.effects.front().effect == 300);
  CHECK(body(tied, 1).body.position().x() == Catch::Approx(29.0));
  const auto rim = solve(fixture, {moving(1, 10, 50, 7600)}, {}, {support(1)});
  CHECK(rim.effects.empty());
  CHECK(body(rim, 1).body.position() == point(29, 50));
  const auto void_start = solve(fixture, {moving(1, 30, 50)}, {}, {support(1)});
  REQUIRE(void_start.effects.size() == 1);
  CHECK(void_start.effects.front().event.time() == simulation::MotionTime::start());
}

TEST_CASE("support queries follow the actual bent epoch and paths retain its anchors",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture(simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(100, 100), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("after_bounce", point(31.4, 45.2),
                                       0.5 + simulation::kPositionTolerance)}));
  auto obstacle = fixed(2, 33, 54);
  obstacle.body = obstacle.body.with_radius(4.0);
  const auto result = solve(fixture, {moving(1, 20, 50, 8000), obstacle}, {}, {support(1)});
  REQUIRE(result.effects.size() == 2);
  CHECK(result.effects[0].effect == 1002);
  CHECK(result.effects[1].effect == 300);
  CHECK(result.effects[0].event.time().value() == Catch::Approx(0.5));
  CHECK(result.effects[1].event.time().value() == Catch::Approx(0.725));
  REQUIRE(result.motion.paths.size() == 2);
  const auto& incoming = result.motion.paths[0];
  const auto& outgoing = result.motion.paths[1];
  CHECK(incoming.begin == simulation::MotionTime::start());
  CHECK(incoming.start == point(20, 50));
  CHECK(incoming.finish.x() == Catch::Approx(30.0));
  CHECK(incoming.finish.y() == Catch::Approx(50.0));
  CHECK(outgoing.begin == incoming.end);
  CHECK(outgoing.start == incoming.finish);
  CHECK(outgoing.end == result.effects[1].event.time());
  CHECK(outgoing.finish.x() == Catch::Approx(31.26));
  CHECK(outgoing.finish.y() == Catch::Approx(45.68));
  CHECK(body(result, 1).body.position() == outgoing.finish);
}

TEST_CASE("unrelated events preserve untouched epoch root and endpoint bits",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const std::vector input{moving(1, 13.7, 50, 3123.45), fixed(2, 20, 50),
                          moving(3, 80, 80, 123.456789)};
  const auto baseline = solve(fixture, input);
  REQUIRE(baseline.effects.size() == 1);
  for (const auto behavior : {ScriptBehavior::kAcceleration, ScriptBehavior::kVelocity}) {
    Facts facts;
    facts.script = behavior;
    const auto extra = solve(fixture, input, facts, {script(3)});
    const auto contact = std::find_if(extra.effects.begin(), extra.effects.end(),
                                      [](const auto& effect) { return effect.effect == 1002; });
    REQUIRE(contact != extra.effects.end());
    CHECK(std::bit_cast<std::uint64_t>(contact->event.time().value()) ==
          std::bit_cast<std::uint64_t>(baseline.effects[0].event.time().value()));
    CHECK(std::bit_cast<std::uint64_t>(body(extra, 1).body.position().x()) ==
          std::bit_cast<std::uint64_t>(body(baseline, 1).body.position().x()));
    const auto count = std::count_if(extra.motion.paths.begin(), extra.motion.paths.end(),
                                     [](const auto& path) { return path.entity.value() == 3; });
    CHECK(count == (behavior == ScriptBehavior::kAcceleration ? 1 : 2));
  }
  auto accelerated = moving(1, 10, 50, 100);
  accelerated.body = accelerated.body.with_acceleration(point(1e6, 1e6));
  CHECK(body(solve(fixture, {accelerated}), 1).body.position() == point(10.25, 50));
}

TEST_CASE("relative arithmetic admits opposing maximum physical velocities",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  auto first = moving(1, 50, 50, 1e12);
  auto second = moving(2, 60, 50, -1e12);
  first.body = first.body.with_bounds_behavior(simulation::BoundsBehavior::kCross);
  second.body = second.body.with_bounds_behavior(simulation::BoundsBehavior::kCross);
  Facts facts;
  facts.pair = PairBehavior::kTerminate;
  const auto result = solve(fixture, {first, second}, facts);
  REQUIRE(result.effects.size() == 1);
  CHECK(result.effects.front().event.time().value() == Catch::Approx(8.0 / 5e9));
  CHECK(body(result, 1).disposition == simulation::MotionDisposition::kTerminate);
  CHECK(body(result, 2).disposition == simulation::MotionDisposition::kTerminate);
}

TEST_CASE("exact nonaxis tangent never acquires a closing impulse from normalized rounding",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  auto first = moving(1, 13e6, 22.25e6, -9.6e9, 2.8e9);
  auto second = moving(2, 0, 0);
  first.body =
      first.body.with_radius(12.5e6).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  second.body =
      second.body.with_radius(12.5e6).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  Facts facts;
  facts.pair = PairBehavior::kUnchanged;
  // Exact tangent t=.25 at (7e6,24e6), summed radius25e6. The rounded normal's dot
  // product has a spurious -4.768e-7 closing residual; canonical root topology owns admission.
  const auto result = solve(fixture, {first, second}, facts);
  CHECK(result.effects.empty());
  CHECK(result.motion.events.empty());
  for (const auto endpoint_start : {point(7e6, 24e6), point(31e6, 17e6)}) {
    // The identical supporting line is tangent at t=0 and t=1 respectively.
    first.body = first.body.with_position(endpoint_start);
    const auto endpoint = solve(fixture, {first, second}, facts);
    CHECK(endpoint.effects.empty());
    CHECK(endpoint.motion.events.empty());
  }
}

TEST_CASE("retained contact excludes revised exact radial tangency before callback admission",
          "[unit][simulation][continuous_motion][retained_radial_motion]") {
  const Fixture fixture;
  auto first = moving(1, -18e6, 24e6, 1, 0);
  auto second = moving(2, 7e6, 24e6, -1, 0);
  auto third = moving(3, 0, 0);
  for (auto* subject : {&first, &second, &third}) {
    subject->body =
        subject->body.with_radius(12.5e6).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  }
  Facts facts;
  facts.pair = PairBehavior::kRetainedTangent;
  // AB and BC initially close at t=0. The first callback makes B exactly tangent to C;
  // the retained BC normal still produces a spurious negative rounded speed. Selected
  // diagnostic certificates may include BC, but no BC callback/consequence is admissible.
  const auto result = solve(fixture, {first, second, third}, facts);
  REQUIRE(result.effects.size() == 1);
  CHECK(result.effects.front().effect == 1002);
  CHECK(result.effects.front().event.time() == simulation::MotionTime::start());
  CHECK(result.motion.work.root_queries == 4); // Three initial candidates plus revised BC.
  simulation::MotionLimits limited;
  limited.root_queries = 3;
  rejected([&] { return solve(fixture, {first, second, third}, facts, {}, limited); },
           simulation::SimulationValidationCode::kContinuousMotionBudgetExceeded);
}

TEST_CASE("tiny distinct centers retain geometric normals and separating contact stays inert",
          "[unit][simulation][continuous_motion][retained_radial_motion]") {
  const Fixture fixture;
  auto first = moving(1, 0, 0, -1, 0);
  auto second = moving(2, 2e-10, 0);
  first.body =
      first.body.with_radius(1e-10).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  second.body =
      second.body.with_radius(1e-10).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  Facts facts;
  facts.pair = PairBehavior::kCaptureNormal;
  const auto separating = solve(fixture, {first, second}, facts);
  CHECK(separating.effects.empty());
  first.body = first.body.with_velocity(point(1, 0)).with_radius(1.1e-10);
  second.body = second.body.with_position(point(1.2e-10, 1.6e-10)).with_radius(1.1e-10);
  const auto oblique = solve(fixture, {first, second}, facts);
  REQUIRE(oblique.effects.size() == 3);
  CHECK(std::bit_cast<double>(oblique.effects[1].effect) == Catch::Approx(0.6));
  CHECK(std::bit_cast<double>(oblique.effects[2].effect) == Catch::Approx(0.8));
  first.body = first.body.with_velocity(point(1, 1));
  second.body = second.body.with_position(first.body.position());
  const auto coincident = solve(fixture, {first, second}, facts);
  REQUIRE(coincident.effects.size() == 3);
  CHECK(std::bit_cast<double>(coincident.effects[1].effect) == Catch::Approx(0.7071067811865475));
  CHECK(std::bit_cast<double>(coincident.effects[2].effect) == Catch::Approx(0.7071067811865475));
}

TEST_CASE("initial body impact uses exact summed radii without a discrete proximity band",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  Facts facts;
  facts.pair = PairBehavior::kTerminate;
  const auto result = solve(
      fixture, {moving(1, 20, 50, 100), fixed(2, 22.0 + 0.5 * simulation::kPositionTolerance, 50)},
      facts);
  REQUIRE(result.effects.size() == 1);
  CHECK(result.effects.front().event.time() > simulation::MotionTime::start());
}

TEST_CASE("unhandled swept AABB endpoints do not reject an earlier representable termination",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  auto first = moving(1, 1e12 - 4.0, 50, 1e12);
  auto second = moving(2, 1e12 - 1.0, 50);
  first.body = first.body.with_bounds_behavior(simulation::BoundsBehavior::kCross);
  second.body = second.body.with_bounds_behavior(simulation::BoundsBehavior::kCross);
  Facts facts;
  facts.pair = PairBehavior::kTerminate;
  const auto result = solve(fixture, {first, second}, facts);
  REQUIRE(result.effects.size() == 1);
  CHECK(body(result, 1).body.position().x() == 1e12 - 3.0);
  CHECK(body(result, 1).disposition == simulation::MotionDisposition::kTerminate);
  CHECK(body(result, 2).disposition == simulation::MotionDisposition::kTerminate);
}

TEST_CASE("geometry trigger helpers expose their bounded full endpoint query domain",
          "[unit][simulation][continuous_motion]") {
  const auto subject = moving(1, 1e12 - 4.0, 50, 1e12);
  const simulation::MotionTriggerWindow window{
      {subject, simulation::MotionTime::start(), point(2.5e9, 0)}, simulation::MotionTime::start()};
  simulation::MotionWorkCounts counts;
  const simulation::MotionLimits limits;
  simulation::MotionQueryBudget budget(counts, limits);
  const std::vector<simulation::MotionCircleGate> gates{{point(1e12 - 1.0, 50), 1.0}};
  rejected([&] { return simulation::ordered_gate_motion_trigger(gates, 0, window, budget); },
           simulation::SimulationValidationCode::kPhysicalScalarOutOfRange);
  rejected([&] { return simulation::support_loss_motion_trigger(solid(), window, budget); },
           simulation::SimulationValidationCode::kPhysicalScalarOutOfRange);
}

TEST_CASE("ordered gate occupancy retains the canonical spatial rim tolerance",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  Facts facts;
  facts.gates = {{point(50, 50), 1.0}};
  const auto band = solve(fixture, {moving(1, 51.0 + simulation::kPositionTolerance * 0.5, 50)},
                          facts, {gate(1, 1)});
  REQUIRE(band.effects.size() == 1);
  CHECK(band.effects.front().event.time() == simulation::MotionTime::start());
  const auto outside = solve(fixture, {moving(1, 51.0 + simulation::kPositionTolerance * 2.0, 50)},
                             facts, {gate(1, 1)});
  CHECK(outside.effects.empty());
}

TEST_CASE("continuous response validation and no progress fail without publishing world writes",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto before = fixture.world;
  Facts facts;
  for (const auto behavior :
       {PairBehavior::kTeleport, PairBehavior::kMass, PairBehavior::kDisposition}) {
    facts.pair = behavior;
    rejected([&] { return solve(fixture, {moving(1, 20, 50, 100), moving(2, 21, 50)}, facts); },
             simulation::SimulationValidationCode::kContinuousMotionInvalidResponse);
  }
  facts.script = ScriptBehavior::kNoProgress;
  rejected([&] { return solve(fixture, {moving(1, 20, 50, 100)}, facts, {script(1)}); },
           simulation::SimulationValidationCode::kContinuousMotionNoProgress);
  facts.script = ScriptBehavior::kVelocityOnly;
  const auto progress = solve(fixture, {moving(1, 20, 50, 100)}, facts, {script(1)});
  CHECK(progress.effects.size() == 1);
  CHECK(progress.trigger_cursors == std::vector<std::uint64_t>{0});
  facts.script = ScriptBehavior::kPast;
  rejected([&] { return solve(fixture, {moving(1, 20, 50, 100)}, facts, {script(1)}); },
           simulation::SimulationValidationCode::kContinuousMotionInvalidResponse);
  facts.script = ScriptBehavior::kExcessCursor;
  rejected([&] { return solve(fixture, {moving(1, 20, 50, 100)}, facts, {script(1)}); },
           simulation::SimulationValidationCode::kContinuousMotionNoProgress);
  facts.script = ScriptBehavior::kBackwardCursor;
  auto backward = script(1);
  backward.initial_cursor = 1;
  backward.cursor_limit = 2;
  rejected([&] { return solve(fixture, {moving(1, 20, 50, 100)}, facts, {backward}); },
           simulation::SimulationValidationCode::kContinuousMotionNoProgress);
  CHECK(fixture.world == before);
}

TEST_CASE("every continuous motion budget is a named visible boundary",
          "[unit][simulation][continuous_motion]") {
  const Fixture fixture;
  const auto before = fixture.world;
  const std::vector input{moving(1, 10, 50, 20000), moving(2, 40, 50)};
  const auto exhausted = simulation::SimulationValidationCode::kContinuousMotionBudgetExceeded;
  for (const auto field :
       {&simulation::MotionLimits::bodies, &simulation::MotionLimits::candidate_pairs,
        &simulation::MotionLimits::pair_examinations, &simulation::MotionLimits::root_queries,
        &simulation::MotionLimits::events, &simulation::MotionLimits::paths,
        &simulation::MotionLimits::effects}) {
    simulation::MotionLimits limits;
    limits.*field = 0;
    rejected([&] { return solve(fixture, input, {}, {}, limits); }, exhausted);
  }
  Facts facts;
  facts.gates = {{point(20, 50), 1.0}};
  for (const auto field : {&simulation::MotionLimits::trigger_declarations,
                           &simulation::MotionLimits::trigger_queries}) {
    simulation::MotionLimits limits;
    limits.*field = 0;
    rejected([&] { return solve(fixture, input, facts, {gate(1, 1)}, limits); }, exhausted);
  }
  simulation::MotionLimits cursor;
  cursor.trigger_cursor = 0;
  rejected([&] { return solve(fixture, input, facts, {gate(1, 1)}, cursor); },
           simulation::SimulationValidationCode::kContinuousMotionInvalidInput);
  simulation::MotionLimits excessive;
  excessive.bodies = simulation::kMaximumMotionBodyCount + 1;
  rejected([&] { return solve(fixture, input, {}, {}, excessive); },
           simulation::SimulationValidationCode::kContinuousMotionInvalidInput);
  rejected([&] { return solve(fixture, {input[0], input[0]}); },
           simulation::SimulationValidationCode::kContinuousMotionInvalidInput);
  rejected([&] { return solve(fixture, input, facts, {gate(1, 1), gate(1, 1)}); },
           simulation::SimulationValidationCode::kContinuousMotionInvalidInput);
  CHECK(fixture.world == before);
}

TEST_CASE("contact effects follow individual source policies under identity and input swaps",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.pair = PairBehavior::kEligibleEffects;
  facts.effect_policies = {any_touch(2)};
  const std::vector input{moving(1, 20, 20), fixed(2, 22, 20), moving(3, 60, 20), fixed(4, 62, 20)};
  const auto result = solve(fixture, input, facts);
  CHECK(effects(result) == std::vector<Effect>{2});
  CHECK(result.motion.events.size() == 1);
  auto permuted = input;
  std::reverse(permuted.begin(), permuted.end());
  const auto reordered = solve(fixture, permuted, facts);
  CHECK(result.motion.bodies == reordered.motion.bodies);
  CHECK(result.motion.paths == reordered.motion.paths);
  CHECK(result.motion.events == reordered.motion.events);
  CHECK(effects(result) == effects(reordered));
  facts.effect_policies = {any_touch(1)};
  const auto swapped = solve(fixture, {fixed(1, 22, 20), moving(2, 20, 20)}, facts);
  CHECK(effects(swapped) == std::vector<Effect>{1});
  CHECK(body(swapped, 1).body.position() == body(result, 2).body.position());
  CHECK(body(swapped, 2).body.position() == body(result, 1).body.position());
}

TEST_CASE("exact rotated tangent touch never gains an impulse at initial interior or final time",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  auto first = moving(1, 13e6, 22.25e6, -9.6e9, 2.8e9);
  auto second = moving(2, 0, 0);
  first.body =
      first.body.with_radius(12.5e6).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  second.body =
      second.body.with_radius(12.5e6).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  Facts facts;
  facts.effect_policies = {any_touch(2)};
  facts.capture_observation = true;
  for (const auto& [start, expected_time] :
       std::vector<std::pair<simulation::Vector2, simulation::MotionTime>>{
           {point(7e6, 24e6), simulation::MotionTime::start()},
           {point(13e6, 22.25e6), simulation::MotionTime::create(0.25)},
           {point(31e6, 17e6), simulation::MotionTime::end()}}) {
    first.body = first.body.with_position(start);
    const auto baseline = solve(fixture, {first, second});
    const auto result = solve(fixture, {first, second}, facts);
    CHECK(effects(result) == std::vector<Effect>{1002, 0, 0, 1});
    REQUIRE(result.motion.events.size() == 1);
    CHECK(result.motion.events.front().time() == expected_time);
    CHECK(result.motion.bodies == baseline.motion.bodies);
    CHECK(result.motion.paths == baseline.motion.paths);
  }
}

TEST_CASE("any touch observes stationary and separating closed overlap without physical response",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.effect_policies = {any_touch(1), any_touch(2)};
  facts.capture_observation = true;
  auto obstacle = fixed(2, 22, 50);
  obstacle.body = obstacle.body.with_velocity(point(9000, -7000)).with_acceleration(point(30, 40));
  for (const auto& first : std::vector{moving(1, 20, 50), moving(1, 21, 50), moving(1, 22, 50),
                                       moving(1, 20, 50, -100), moving(1, 21, 50, -100)}) {
    const auto baseline = solve(fixture, {first, obstacle});
    const auto result = solve(fixture, {first, obstacle}, facts);
    CHECK(effects(result) == std::vector<Effect>{1002, 0, 1, 1});
    REQUIRE(result.motion.events.size() == 1);
    CHECK(result.motion.events.front().time() == simulation::MotionTime::start());
    CHECK(result.motion.bodies == baseline.motion.bodies);
    CHECK(result.motion.paths == baseline.motion.paths);
    CHECK(body(result, 2).body == obstacle.body);
  }
}

TEST_CASE("explicit default policies preserve impact work and mixed policies share one impulse",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  const std::vector input{moving(1, 10, 50, 20000), moving(2, 40, 50)};
  const auto baseline = solve(fixture, input);
  Facts explicit_defaults;
  explicit_defaults.effect_policies = {{simulation::EntityId::create(1)},
                                       {simulation::EntityId::create(2)}};
  const auto defaults = solve(fixture, input, explicit_defaults);
  CHECK(defaults.motion.bodies == baseline.motion.bodies);
  CHECK(defaults.motion.paths == baseline.motion.paths);
  CHECK(defaults.motion.events == baseline.motion.events);
  CHECK(defaults.motion.work == baseline.motion.work);
  CHECK(effects(defaults) == effects(baseline));
  Facts mixed;
  mixed.effect_policies = {any_touch(2)};
  mixed.capture_observation = true;
  const auto result = solve(fixture, input, mixed);
  CHECK(effects(result) == std::vector<Effect>{1002, 1, 1, 1});
  CHECK(result.motion.bodies == baseline.motion.bodies);
  CHECK(result.motion.paths == baseline.motion.paths);
  CHECK(result.motion.work == baseline.motion.work);
}

TEST_CASE("default stationary rejection retains its domain without constructing a tiny normal",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  auto first = moving(1, 0, 0);
  auto second = moving(2, 1e-170, 0);
  for (auto* subject : {&first, &second}) {
    subject->body =
        subject->body.with_radius(1e-170).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  }
  const auto result = solve(fixture, {first, second});
  CHECK(result.effects.empty());
  CHECK(result.motion.events.empty());
  CHECK(body(result, 1).body == first.body);
  CHECK(body(result, 2).body == second.body);
}

TEST_CASE("no effect touch is consumed despite time advance and acceleration only changes",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.pair = PairBehavior::kNoEffects;
  facts.effect_policies = {any_touch(1)};
  facts.script = ScriptBehavior::kAcceleration;
  simulation::MotionLimits limits;
  limits.events = 2;
  const auto result =
      solve(fixture, {moving(1, 20, 50, 100), moving(2, 21, 50, 100)}, facts, {script(1)}, limits);
  CHECK(effects(result) == std::vector<Effect>{400});
  REQUIRE(result.motion.events.size() == 2);
  CHECK(result.motion.events.front().priority() == simulation::MotionEventPriority::kBodyContact);
  CHECK(result.motion.events.back().time() == facts.script_time);
  CHECK(result.motion.paths.size() == 2);
  CHECK(body(result, 1).body.velocity() == point(100, 0));
  CHECK(body(result, 1).body.acceleration() == point(123, 0));
}

TEST_CASE("external velocity revision reenables a consumed nonimpact touch within event budgets",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  const auto before = fixture.world;
  Facts facts;
  facts.effect_policies = {any_touch(1)};
  facts.capture_observation = true;
  facts.script = ScriptBehavior::kVelocity;
  const std::vector input{moving(1, 20, 50, 100), moving(2, 21, 50, 100)};
  simulation::MotionLimits limits;
  limits.events = 3;
  const auto result = solve(fixture, input, facts, {script(1)}, limits);
  CHECK(effects(result) == std::vector<Effect>{1002, 0, 1, 0, 400, 1002, 0, 1, 0});
  REQUIRE(result.motion.events.size() == 3);
  CHECK(result.motion.events.front().time() == simulation::MotionTime::start());
  CHECK(result.motion.events[1].time() == facts.script_time);
  CHECK(result.motion.events[2].time() == facts.script_time);
  CHECK(body(result, 1).body.velocity() == point(-100, 0));
  CHECK(body(result, 2).body.velocity() == point(100, 0));
  limits.events = 2;
  rejected([&] { return solve(fixture, input, facts, {script(1)}, limits); },
           simulation::SimulationValidationCode::kContinuousMotionBudgetExceeded);
  limits.events = 3;
  limits.effects = 8;
  rejected([&] { return solve(fixture, input, facts, {script(1)}, limits); },
           simulation::SimulationValidationCode::kContinuousMotionBudgetExceeded);
  CHECK(fixture.world == before);
}

TEST_CASE("touch response consumes its own changed velocity revision without an immediate repeat",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.pair = PairBehavior::kSelfVelocityChange;
  facts.effect_policies = {any_touch(2)};
  simulation::MotionLimits limits;
  limits.events = 1;
  const auto result = solve(fixture, {moving(1, 20, 50), fixed(2, 22, 50)}, facts, {}, limits);
  CHECK(effects(result) == std::vector<Effect>{1002});
  CHECK(result.motion.work.events == 1);
  CHECK(body(result, 1).body.position() == point(10, 50));
  CHECK(body(result, 1).body.velocity() == point(-4000, 0));
}

TEST_CASE("terminal epoch reenables an existing touch but never travels an outside reference root",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.effect_policies = {any_touch(3)};
  facts.capture_observation = true;
  facts.gates = {{point(12, 50), 1.0 - simulation::kPositionTolerance}};
  const auto result = solve(fixture, {moving(1, 10, 50, 400), moving(2, 13, 50), moving(3, 13, 52)},
                            facts, {gate(1, 1)});
  CHECK(effects(result) == std::vector<Effect>{2003, 0, 0, 1, 1002, 1, 1, 1, 2003, 0, 0, 1, 100});
  REQUIRE(result.motion.events.size() == 4);
  CHECK(result.motion.events.front().time() == simulation::MotionTime::start());
  CHECK(result.motion.events[1].time() == simulation::MotionTime::end());
  CHECK(result.motion.events[2].time() == simulation::MotionTime::end());
  CHECK(result.motion.events[3].time() == simulation::MotionTime::end());
  CHECK(body(result, 2).body.position() == point(13, 50));
  CHECK(body(result, 3).body.position() == point(13, 52));
  const auto outside =
      solve(fixture, {moving(1, 10, 50, 400), moving(2, 13, 50), moving(3, 14.5, 51.5)}, facts,
            {gate(1, 1)});
  CHECK(effects(outside) == std::vector<Effect>{1002, 1, 1, 1, 100});
  CHECK(body(outside, 3).body.position() == point(14.5, 51.5));
}

TEST_CASE("retained stationary or tangent touch can gain a causal closing impact",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.pair = PairBehavior::kRetainedApproach;
  facts.effect_policies = {any_touch(3)};
  facts.capture_observation = true;
  for (const auto vertical_speed : {0.0, 100.0}) {
    const auto result = solve(fixture,
                              {moving(1, 18, 20, 4000, vertical_speed),
                               moving(2, 20, 20, 0, vertical_speed), moving(3, 22, 20)},
                              facts);
    CHECK(effects(result) == std::vector<Effect>{1002, 1, 1, 1, 2003, 1, 1, 1});
    REQUIRE(result.motion.events.size() == 2);
    CHECK(result.motion.events[0].time() == simulation::MotionTime::start());
    CHECK(result.motion.events[1].time() == simulation::MotionTime::start());
    CHECK(body(result, 3).disposition == simulation::MotionDisposition::kTerminate);
  }
}

TEST_CASE("retained exact radial tangent still delivers only the any touch source effect",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  auto first = moving(1, -18e6, 24e6, 1, 0);
  auto second = moving(2, 7e6, 24e6, -1, 0);
  auto third = moving(3, 0, 0);
  for (auto* subject : {&first, &second, &third}) {
    subject->body =
        subject->body.with_radius(12.5e6).with_bounds_behavior(simulation::BoundsBehavior::kCross);
  }
  Facts facts;
  facts.pair = PairBehavior::kRetainedTangent;
  facts.effect_policies = {any_touch(3)};
  facts.capture_observation = true;
  const auto result = solve(fixture, {first, second, third}, facts);
  CHECK(effects(result) == std::vector<Effect>{1002, 1, 1, 1, 2003, 0, 0, 1});
  CHECK(result.motion.work.root_queries == 4);
  CHECK(body(result, 2).body.velocity() == point(-9.6e9, 2.8e9));
  CHECK(body(result, 3).body.velocity() == point(0, 0));
}

TEST_CASE("any touch honors support contact finish order and immediate recipient termination",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture(simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(100, 100), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("pit", point(30, 50),
                                       1.0 + simulation::kPositionTolerance)}));
  Facts facts;
  facts.effect_policies = {any_touch(2)};
  facts.gates = {{point(30, 50), 1.0}};
  const auto support_first =
      solve(fixture, {moving(1, 30, 50), fixed(2, 31, 50)}, facts, {support(1), gate(1, 1)});
  CHECK(effects(support_first) == std::vector<Effect>{300});
  CHECK(support_first.trigger_cursors == std::vector<std::uint64_t>{1, 0});
  facts.gates = {{point(20, 50), 1.0}};
  const std::vector input{moving(1, 20, 50), fixed(2, 22, 50)};
  const auto finish_after_touch = solve(fixture, input, facts, {gate(1, 1)});
  CHECK(effects(finish_after_touch) == std::vector<Effect>{1002, 100});
  REQUIRE(finish_after_touch.motion.events.size() == 2);
  CHECK(finish_after_touch.motion.events[0].time() == finish_after_touch.motion.events[1].time());
  facts.pair = PairBehavior::kTerminateEligibleRecipient;
  const auto terminated = solve(fixture, input, facts, {gate(1, 1)});
  CHECK(effects(terminated) == std::vector<Effect>{1002});
  CHECK(body(terminated, 1).disposition == simulation::MotionDisposition::kTerminate);
  CHECK(body(terminated, 2).disposition == simulation::MotionDisposition::kContinue);
  CHECK(terminated.trigger_cursors == std::vector<std::uint64_t>{0});
}

TEST_CASE("any touch retains body filters exact radii and excludes static pairs",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  Facts facts;
  facts.effect_policies = {any_touch(2)};
  auto masked = moving(1, 20, 50);
  masked.body = simulation::PhysicsBody::create(masked.body.position(), point(0, 0), point(0, 0),
                                                1.0, 1.0, 1, 0, false);
  CHECK(solve(fixture, {masked, fixed(2, 21, 50)}, facts).motion.events.empty());
  CHECK(solve(fixture, {fixed(1, 20, 50), fixed(2, 21, 50)}, facts).motion.events.empty());
  CHECK(solve(fixture,
              {moving(1, 20, 50), fixed(2, 22.0 + 0.5 * simulation::kPositionTolerance, 50)}, facts)
            .motion.events.empty());
}

TEST_CASE("contact effect policies reject duplicate absent and undeclared values before callbacks",
          "[unit][simulation][continuous_motion][contact_effect_policy]") {
  const Fixture fixture;
  const auto before = fixture.world;
  const std::vector input{moving(1, 20, 50), fixed(2, 21, 50)};
  for (const auto& policies : std::vector<std::vector<simulation::MotionContactEffectPolicy>>{
           {any_touch(1), any_touch(1)},
           {any_touch(3)},
           {{simulation::EntityId::create(1), static_cast<simulation::ContactEffectPolicy>(77)}},
           {any_touch(1), any_touch(2), any_touch(3)}}) {
    Facts facts;
    facts.effect_policies = policies;
    rejected([&] { return solve(fixture, input, facts); },
             simulation::SimulationValidationCode::kContinuousMotionInvalidInput);
  }
  CHECK(fixture.world == before);
}
