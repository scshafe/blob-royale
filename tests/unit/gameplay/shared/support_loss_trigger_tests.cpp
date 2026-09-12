#include "shared/support_loss_trigger.hpp"

#include "components/controllable_component.hpp"
#include "contact_rule_table.hpp"
#include "events/contact_event.hpp"
#include "events/despawn_event.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "motion_trigger_table.hpp"
#include "motion_triggers.hpp"
#include "simulation_tolerance.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

using Subject = simulation::ContactRule::Subject;
simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }
simulation::EntityId entity(std::uint64_t id) { return simulation::EntityId::create(id); }

simulation::MapDefinition pit_map() {
  return simulation::MapDefinition::create(
      "support_pit",
      simulation::TerrainDefinition::create(
          simulation::ArenaBounds::create(100, 100), simulation::TerrainGround::kSolid, {},
          {simulation::TerrainHole::create("pit", point(30, 50),
                                           1 + simulation::kPositionTolerance)}),
      {}, {}, simulation::MapMetadata::none());
}

simulation::PhysicsBody bound_body(double x = 10, double velocity = 20'000) {
  return simulation::PhysicsBody::create(point(x, 50), point(velocity, 0), point(0, 0))
      .with_radius(1)
      .with_ground_attachment(simulation::GroundAttachment::kGroundBound);
}

struct Fixture final {
  simulation::SimulationConfig configuration =
      simulation::SimulationConfig::create(100, 100, 1, 400, 10, 10, 0);
  simulation::GameWorld world;
  simulation::MapDefinition map = pit_map();
  simulation::SpatialGrid grid;
  simulation::TickContext context;

  explicit Fixture(simulation::PhysicsBody body = bound_body(), bool controlled = true)
      : world(simulation::GameWorld::create(
            {simulation::GameWorld::EntitySeed{entity(1), body, std::nullopt}})),
        grid(simulation::SpatialGrid::create(configuration, map.bounds(), world)),
        context(simulation::TickContext::create(simulation::TickSequence::create(1),
                                                simulation::FixedDelta::canonical(), configuration,
                                                map, grid)) {
    if (controlled) {
      world.mutable_store<simulation::Controllable>().insert_or_assign(
          entity(1), {simulation::ControllerId::create(1)});
    }
  }

  Fixture(const Fixture&) = delete;
  Fixture(Fixture&&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture& operator=(Fixture&&) = delete;

  std::vector<Subject> subjects() const {
    std::vector<Subject> result;
    for (const auto& entry : world.store<simulation::PhysicsBody>().entries()) {
      result.push_back({entry.entity, entry.value});
    }
    return result;
  }
};

simulation::PairMotionResponse<simulation::WorldEvent>
pair_response(const simulation::GameWorld& world, const Subject& first, const Subject& second,
              const simulation::PairContactObservation& observation,
              const simulation::TickContext& context, const simulation::LiveMotionFacts& facts) {
  return facts.require_contacts().respond(world, first, second, observation, context);
}

simulation::ContinuousMotionResult<simulation::WorldEvent>
solve(Fixture& fixture,
      gameplay::SupportLossPhasePolicy phase = gameplay::SupportLossPhasePolicy::kAlways) {
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 0, gameplay::SupportLossTrigger::create(phase)});
  const auto table = simulation::MotionTriggerTable::create(std::move(declarations));
  const auto bindings = table.bind(fixture.world, fixture.context);
  const auto contacts = simulation::ContactRuleTable::built_in();
  const auto facts = simulation::LiveMotionFacts::for_contacts(contacts);
  return simulation::solve_continuous_motion<simulation::WorldEvent, simulation::LiveMotionFacts>(
      fixture.world, fixture.subjects(), fixture.context, facts, pair_response, bindings.rows());
}

} // namespace

TEST_CASE("support binding requires a ground-bound dynamic body and its declared match phase",
          "[unit][gameplay][shared][falling]") {
  Fixture fixture;
  const auto always =
      gameplay::SupportLossTrigger::create(gameplay::SupportLossPhasePolicy::kAlways);
  const auto running =
      gameplay::SupportLossTrigger::create(gameplay::SupportLossPhasePolicy::kRunningOnly);
  for (const auto phase : simulation::kMatchPhases) {
    fixture.world.mutable_match().phase = phase;
    for (const auto attachment :
         {simulation::GroundAttachment::kFloating, simulation::GroundAttachment::kGroundBound}) {
      for (const bool is_static : {false, true}) {
        const auto body =
            (is_static ? simulation::PhysicsBody::create_static(point(30, 50)) : bound_body())
                .with_ground_attachment(attachment);
        fixture.world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(1), body);
        const bool eligible =
            !is_static && attachment == simulation::GroundAttachment::kGroundBound;
        CHECK(always->bind(fixture.world, entity(1), fixture.context).has_value() == eligible);
        CHECK(running->bind(fixture.world, entity(1), fixture.context).has_value() ==
              (eligible && phase == simulation::MatchPhase::kRunning));
      }
    }
  }
  CHECK_FALSE(always->bind(fixture.world, entity(2), fixture.context).has_value());
  CHECK_THROWS_AS(
      gameplay::SupportLossTrigger::create(static_cast<gameplay::SupportLossPhasePolicy>(2)),
      gameplay::GameplayValidationError);
}

TEST_CASE(
    "support policy delegates its canonical query and terminates without consuming gate cursors",
    "[unit][gameplay][shared][falling]") {
  Fixture fixture;
  const auto policy =
      gameplay::SupportLossTrigger::create(gameplay::SupportLossPhasePolicy::kAlways);
  const Subject subject{entity(1), bound_body()};
  const simulation::MotionTriggerWindow window{
      {subject, simulation::MotionTime::start(), point(50, 0)}, simulation::MotionTime::start()};
  simulation::MotionWorkCounts expected_work;
  simulation::MotionWorkCounts actual_work;
  const simulation::MotionLimits limits;
  simulation::MotionQueryBudget expected_budget(expected_work, limits);
  simulation::MotionQueryBudget actual_budget(actual_work, limits);
  const auto expected =
      simulation::support_loss_motion_trigger(fixture.map.terrain(), window, expected_budget);
  const auto actual =
      policy->query(fixture.world, subject, window, fixture.context, 0, actual_budget);
  REQUIRE(expected.has_value());
  REQUIRE(actual.has_value());
  CHECK(actual->time == expected->time);
  CHECK(actual->priority == expected->priority);
  CHECK(actual_work == expected_work);
  const simulation::MotionTriggerEvent event{
      simulation::MotionEventKey::boundary(actual->time, actual->priority, entity(1), 0), 0};
  const auto response = policy->respond(fixture.world, subject, event, fixture.context);
  CHECK(response.body.body == subject.body);
  CHECK(response.body.disposition == simulation::MotionDisposition::kTerminate);
  CHECK(response.cursor == 0);
  REQUIRE(response.effects.size() == 1);
  CHECK(std::get<simulation::EliminationEvent>(response.effects.front()).entity == entity(1));
}

TEST_CASE("support loss stops at the center crossing before a later collision or contact effect",
          "[unit][gameplay][shared][falling][continuous_motion]") {
  for (const bool controlled : {false, true}) {
    Fixture fixture(bound_body(), controlled);
    fixture.world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        entity(2),
        bound_body(40, 0).with_ground_attachment(simulation::GroundAttachment::kFloating));
    const auto before = fixture.world;
    const auto result = solve(fixture);
    CHECK(fixture.world == before);
    REQUIRE(result.effects.size() == 1);
    CHECK(result.effects.front().event.priority() == simulation::MotionEventPriority::kSupportLoss);
    CHECK(result.effects.front().event.time().value() == Catch::Approx(0.38));
    CHECK(std::holds_alternative<simulation::EliminationEvent>(result.effects.front().effect) ==
          controlled);
    CHECK(std::holds_alternative<simulation::DespawnEvent>(result.effects.front().effect) ==
          !controlled);
    REQUIRE(result.motion.bodies.size() == 2);
    CHECK(result.motion.bodies[0].result.disposition == simulation::MotionDisposition::kTerminate);
    CHECK(result.motion.bodies[0].result.body.position().x() == Catch::Approx(29));
    CHECK(result.motion.bodies[1].result.body.position() == point(40, 50));
    CHECK(result.motion.bodies[1].result.body.velocity() == point(0, 0));
    CHECK(result.trigger_cursors == std::vector<std::uint64_t>{0});
  }
}

TEST_CASE("floating hazards cross interior darkness while bound hazards fall and statics remain",
          "[unit][gameplay][shared][falling]") {
  Fixture floating(bound_body().with_ground_attachment(simulation::GroundAttachment::kFloating),
                   false);
  const auto floated = solve(floating);
  CHECK(floated.effects.empty());
  CHECK(floated.motion.bodies.front().result.body.position() == point(60, 50));
  Fixture bound(bound_body(), false);
  const auto fallen = solve(bound);
  REQUIRE(fallen.effects.size() == 1);
  CHECK(std::get<simulation::DespawnEvent>(fallen.effects.front().effect).entity == entity(1));
  Fixture wall(simulation::PhysicsBody::create_static(point(30, 50))
                   .with_radius(1)
                   .with_ground_attachment(simulation::GroundAttachment::kGroundBound),
               false);
  const auto stationary = solve(wall);
  CHECK(stationary.effects.empty());
  CHECK(stationary.motion.bodies.front().result.body.position() == point(30, 50));
}

TEST_CASE("an already unsupported bound body falls at time zero only in active phases",
          "[unit][gameplay][shared][falling]") {
  Fixture fixture(bound_body(30, 0));
  CHECK(solve(fixture, gameplay::SupportLossPhasePolicy::kRunningOnly).effects.empty());
  const auto always = solve(fixture);
  REQUIRE(always.effects.size() == 1);
  CHECK(always.effects.front().event.time() == simulation::MotionTime::start());
  fixture.world.mutable_match().phase = simulation::MatchPhase::kRunning;
  const auto running = solve(fixture, gameplay::SupportLossPhasePolicy::kRunningOnly);
  REQUIRE(running.effects.size() == 1);
  CHECK(running.effects.front().event.time() == simulation::MotionTime::start());
}
