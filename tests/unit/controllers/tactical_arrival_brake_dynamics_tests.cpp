#include "commands/thrust_command.hpp"
#include "components/charge_component.hpp"
#include "components/hill_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "fixtures/tactical_profile_fixture.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "movement_tuning.hpp"
#include "observation.hpp"
#include "physics_body.hpp"
#include "shared/thrust_steering_system.hpp"
#include "simulation_config.hpp"
#include "system_pipeline.hpp"
#include "tactical_controller.hpp"
#include "world_snapshot.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace profiles = blob_royale::testing::tactical_profile_fixture;

namespace {

// A stationary, generous hill keeps every measured block in the arrival branch. It is authored
// once; all subsequent body state comes back from the real production tick. No contact, ability,
// objective motion, tuning update, or runtime delay is part of this conditional velocity bound.
constexpr std::uint64_t kBodyId = 7;
constexpr std::uint64_t kHillId = 20;
constexpr double kWorldExtent = 10'000.0;
constexpr double kHillCenter = 5'000.0;
constexpr double kHillRadius = 1'000.0;
constexpr double kBodyRadius = 10.0;
constexpr std::uint64_t kObservationSpacing = 20;
constexpr std::uint64_t kSettlingBlocks = 80;
constexpr double kComponentTolerance = 1e-10;
// At most eight simulated seconds after the first decision (four at R0). This is 0.1% of one
// player radius per second, a measured finite-horizon tolerance rather than an exact-rest claim.
constexpr double kSettledSpeed = 0.01;
constexpr std::array<double, 3> kDrags{0.0, 2.0, 40.0};
constexpr std::array<double, 2> kBrakeFractions{1.0, 0.5};

struct BrakeFixture final {
  std::uint64_t reaction_ticks{40};
  double drag{0.0};
  double fraction{1.0};
  double acceleration{400.0};
  double ceiling{600.0};
  double initial_x{10.0};
  double initial_y{0.0};
};

struct MotionFrame final {
  std::uint64_t tick;
  simulation::PhysicsBody body;
};

struct DecisionFrame final {
  std::uint64_t tick;
  simulation::Vector2 velocity;
  simulation::Vector2 direction;
};

struct BrakeTrace final {
  std::vector<MotionFrame> motion;
  std::vector<DecisionFrame> decisions;
};

[[nodiscard]] double magnitude(const simulation::Vector2& vector) {
  return std::hypot(vector.x(), vector.y());
}

[[nodiscard]] simulation::PhysicsBody observed_body(const simulation::WorldSnapshot& snapshot) {
  const auto bodies = snapshot.components<simulation::PhysicsBody>();
  REQUIRE(bodies.size() == 1);
  REQUIRE(bodies.front().entity.value() == kBodyId);
  return bodies.front().value;
}

[[nodiscard]] simulation::GameSimulation brake_game(const BrakeFixture& fixture) {
  const auto center = simulation::Vector2::create(kHillCenter, kHillCenter);
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  auto world = simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(kBodyId),
      simulation::PhysicsBody::create(
          center, simulation::Vector2::create(fixture.initial_x, fixture.initial_y), zero,
          kBodyRadius, 1.0, simulation::PhysicsBody::kDefaultCollisionLayer,
          simulation::PhysicsBody::kDefaultCollisionMask, false))});
  world.mutable_store<simulation::Hill>().insert_or_assign(simulation::EntityId::create(kHillId),
                                                           simulation::Hill{center, kHillRadius});
  auto& match = world.mutable_match();
  match.phase = simulation::MatchPhase::kRunning;
  match.previous_phase = simulation::MatchPhase::kRunning;
  match.mode_state = simulation::KingOfTheHillModeState{};
  match.movement.current =
      simulation::MovementTuning::create(fixture.acceleration, fixture.ceiling);
  match.movement.defaults = match.movement.current;
  const auto bounds = simulation::ArenaBounds::create(kWorldExtent, kWorldExtent);
  auto map = simulation::MapDefinition::create("tactical_arrival_brake_dynamics",
                                               simulation::TerrainDefinition::solid(bounds), {}, {},
                                               simulation::MapMetadata::none());
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPreKernel, gameplay::ThrustSteeringSystem::create()});
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(kWorldExtent, kWorldExtent, kBodyRadius, 400, 16, 16,
                                           fixture.drag),
      std::move(world),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(std::move(map))
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
}

[[nodiscard]] controllers::TacticalProfile brake_profile(const BrakeFixture& fixture) {
  auto section = profiles::immediate_section();
  section.objective_seek_probability = 0.0;
  section.reaction_delay_ticks = fixture.reaction_ticks;
  section.target_persistence_ticks = 400;
  section.objective_weights = {1.0, 0.0, 0.0, 0.0, 0.0};
  section.shield_anticipation_ticks = 0;
  section.arrival_brake_fraction = fixture.fraction;
  return controllers::TacticalProfile::create(section);
}

// canonical: arrival_brake_production_loop -- ordinary observed decisions, next-tick InputBatch
// application, and unchanged production steering/integration. The only authored scheduling is
// one observation every twenty committed ticks. The controller itself decides when its reaction
// window has expired; this loop neither calculates a brake nor decides when to replace one.
[[nodiscard]] BrakeTrace run_brake(const BrakeFixture& fixture, const std::uint64_t ticks) {
  auto game = brake_game(fixture);
  controllers::TacticalController controller(simulation::ControllerId::create(kBodyId),
                                             brake_profile(fixture), profiles::kIdentity);
  BrakeTrace trace;
  trace.motion.reserve(static_cast<std::size_t>(ticks + 1));
  std::vector<simulation::Command> pending;
  const auto observe = [&](const simulation::WorldSnapshot& snapshot) {
    auto commands = controller.decide(controllers::Observation::create(
        std::make_shared<const simulation::WorldSnapshot>(snapshot),
        simulation::ControllerId::create(kBodyId)));
    if (commands.empty()) {
      REQUIRE(controller.decision_reason() ==
              controllers::TacticalDecisionReason::kAwaitingReaction);
    } else {
      REQUIRE(commands.size() == 1);
      REQUIRE(std::holds_alternative<simulation::ThrustCommand>(commands.front()));
      REQUIRE(controller.decision_reason() == controllers::TacticalDecisionReason::kArrived);
      const auto& thrust = std::get<simulation::ThrustCommand>(commands.front());
      REQUIRE(thrust.entity.value() == kBodyId);
      REQUIRE_FALSE(thrust.input_generation.has_value());
      const auto body = observed_body(snapshot);
      trace.decisions.push_back(
          {snapshot.tick_sequence().value(), body.velocity(), thrust.direction});
    }
    CHECK(controller.draw_count() == 0);
    return commands;
  };
  auto snapshot = game.snapshot();
  trace.motion.push_back({0, observed_body(snapshot)});
  pending = observe(snapshot);
  for (std::uint64_t tick = 1; tick <= ticks; ++tick) {
    static_cast<void>(
        game.step(simulation::FixedDelta::canonical(),
                  simulation::InputBatch::create(std::move(pending), game.accepted_command_kinds(),
                                                 simulation::EntityIdReservation::none())));
    pending.clear();
    snapshot = game.snapshot();
    const auto body = observed_body(snapshot);
    REQUIRE(snapshot.tick_sequence().value() == tick);
    REQUIRE(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    REQUIRE(snapshot.components<simulation::Hill>().size() == 1);
    REQUIRE(snapshot.components<simulation::Stun>().empty());
    REQUIRE(snapshot.components<simulation::Shield>().empty());
    REQUIRE(snapshot.components<simulation::Charge>().empty());
    REQUIRE(snapshot.match().movement().current.acceleration() == fixture.acceleration);
    REQUIRE(snapshot.match().movement().current.normal_top_speed() == fixture.ceiling);
    REQUIRE(std::hypot(body.position().x() - kHillCenter, body.position().y() - kHillCenter) <
            kHillRadius);
    trace.motion.push_back({tick, body});
    if (tick % kObservationSpacing == 0) {
      pending = observe(snapshot);
    }
  }
  return trace;
}

// Expected decision spacing, used only to assert observations and choose the finite test horizon.
// Production reaction timing is exercised by run_brake's unconditional twenty-tick observations.
[[nodiscard]] std::uint64_t expected_hold(const std::uint64_t reaction) {
  return reaction == 0
             ? kObservationSpacing
             : ((reaction + kObservationSpacing - 1) / kObservationSpacing) * kObservationSpacing;
}

void check_qualified_hold(const BrakeTrace& trace, const std::size_t index,
                          const BrakeFixture& fixture) {
  REQUIRE(index + 1 < trace.decisions.size());
  const auto& start = trace.decisions[index];
  const auto& next = trace.decisions[index + 1];
  REQUIRE(next.tick - start.tick == expected_hold(fixture.reaction_ticks));
  REQUIRE(magnitude(start.velocity) < fixture.ceiling / 2.0);
  const auto& first_applied = trace.motion.at(static_cast<std::size_t>(start.tick + 1)).body;
  for (auto tick = start.tick; tick <= next.tick; ++tick) {
    const auto& body = trace.motion.at(static_cast<std::size_t>(tick)).body;
    CAPTURE(fixture.reaction_ticks, fixture.drag, fixture.fraction, start.tick, tick);
    CHECK(std::abs(body.velocity().x()) <= std::abs(start.velocity.x()) + kComponentTolerance);
    CHECK(std::abs(body.velocity().y()) <= std::abs(start.velocity.y()) + kComponentTolerance);
    CHECK(magnitude(body.velocity()) < fixture.ceiling / 2.0);
    if (tick > start.tick) {
      // A pending decision changes nothing until the following tick. The same accepted intent
      // remains active through the last tick before replacement, with no cap intervention.
      CHECK(body.acceleration() == first_applied.acceleration());
      CHECK(magnitude(body.acceleration()) <= fixture.acceleration + kComponentTolerance);
    }
  }
}

void check_repeated_braking(const std::uint64_t reaction) {
  for (const double drag : kDrags) {
    for (const double fraction : kBrakeFractions) {
      BrakeFixture fixture;
      fixture.reaction_ticks = reaction;
      fixture.drag = drag;
      fixture.fraction = fraction;
      fixture.initial_y = 6.0;
      const auto hold = expected_hold(reaction);
      const auto warmup = reaction == 0 ? 0 : hold;
      const auto trace = run_brake(fixture, warmup + kSettlingBlocks * hold);
      REQUIRE(trace.decisions.size() == kSettlingBlocks + 1);
      REQUIRE(trace.decisions.front().tick == warmup);
      // The first R0 command uses H=1. Apply it honestly, then begin the qualified bound at its
      // next real observation; a dedicated regression below retains that first-hold limitation.
      const std::size_t first_qualified = reaction == 0 ? 1 : 0;
      for (auto index = first_qualified; index + 1 < trace.decisions.size(); ++index) {
        check_qualified_hold(trace, index, fixture);
      }
      CAPTURE(reaction, drag, fraction);
      CHECK(magnitude(trace.motion.back().body.velocity()) <= kSettledSpeed);
    }
  }
}

} // namespace

TEST_CASE("Arrival braking retains independent aligned and nondivisible hold outcomes",
          "[unit][controllers][tactical][arrival][dynamics]") {
  struct KnownOutcome final {
    std::uint64_t reaction;
    double drag;
    double final_x;
  };
  // Independent numerical witnesses, not the controller expression restated as an oracle.
  constexpr std::array<KnownOutcome, 3> outcomes{{
      {40, 0.0, 0.0},
      {40, 2.0, -0.8553727688952049},
      {21, 0.0, -9.04761904761905},
  }};
  for (const auto& known : outcomes) {
    BrakeFixture fixture;
    fixture.reaction_ticks = known.reaction;
    fixture.drag = known.drag;
    if (known.drag == 2.0) {
      // R40 spends forty ticks coasting before its first decision. Author that pre-reaction
      // velocity to arrive at vx=10 under normal drag. Nothing rewrites a committed body or
      // discards a controller command; the assertion below checks the actual warm-up result.
      fixture.initial_x = 10.0 / std::pow(0.995, 40.0);
    }
    const auto trace = run_brake(fixture, 81);
    CAPTURE(known.reaction, known.drag);
    REQUIRE(trace.decisions.size() == 2);
    CHECK(trace.decisions.front().tick == 40);
    CHECK(trace.decisions.back().tick == 80);
    CHECK(std::abs(trace.decisions.front().velocity.x() - 10.0) < kComponentTolerance);
    CHECK(trace.motion.at(40).body.acceleration().x() == 0.0);
    CHECK(trace.motion.at(41).body.acceleration().x() < 0.0);
    CHECK(std::abs(trace.motion.at(80).body.velocity().x() - known.final_x) < kComponentTolerance);
    CHECK(trace.motion.at(80).body.acceleration().x() < 0.0);
    if (known.final_x < 0.0) {
      CHECK(trace.motion.at(81).body.acceleration().x() > 0.0);
    } else {
      CHECK(std::abs(trace.motion.at(81).body.acceleration().x()) < kComponentTolerance);
    }
    check_qualified_hold(trace, 0, fixture);
  }
}

TEST_CASE("Arrival braking at aligned cadence bounds reversed speed and settles across drags",
          "[unit][controllers][tactical][arrival][dynamics]") {
  check_repeated_braking(40);
}

TEST_CASE("Arrival braking at nondivisible cadence bounds reversed speed and settles across drags",
          "[unit][controllers][tactical][arrival][dynamics]") {
  check_repeated_braking(21);
}

TEST_CASE("Arrival braking at established zero-delay cadence bounds reversed speed and settles",
          "[unit][controllers][tactical][arrival][dynamics]") {
  check_repeated_braking(0);
}

TEST_CASE("The first zero-delay arrival brake retains its amplified reversal limitation",
          "[unit][controllers][tactical][arrival][dynamics]") {
  BrakeFixture fixture;
  fixture.reaction_ticks = 0;
  fixture.initial_x = 0.25;
  const auto trace = run_brake(fixture, 40);
  REQUIRE(trace.decisions.size() == 3);
  CHECK(trace.decisions.front().tick == 0);
  CHECK(trace.decisions.front().direction == simulation::Vector2::create(-0.25, 0.0));
  CHECK(trace.motion.at(1).body.acceleration() == simulation::Vector2::create(-100.0, 0.0));
  CHECK(trace.motion.at(1).body.velocity().x() == 0.0);
  CHECK(trace.motion.at(20).body.velocity().x() == -4.75);
  CHECK(std::abs(trace.motion.at(20).body.velocity().x()) > std::abs(fixture.initial_x));
  // The following command uses established S20 spacing and is inside the accepted envelope.
  check_qualified_hold(trace, 1, fixture);
  CHECK(magnitude(trace.motion.at(40).body.velocity()) < kComponentTolerance);
}

TEST_CASE("Saturated diagonal arrival braking passes through shared thrust normalization",
          "[unit][controllers][tactical][arrival][dynamics]") {
  BrakeFixture fixture;
  fixture.ceiling = 10'000.0;
  fixture.initial_x = 200.0;
  fixture.initial_y = -150.0;
  const auto trace = run_brake(fixture, 80);
  REQUIRE(trace.decisions.size() == 2);
  CHECK(trace.decisions.front().direction == simulation::Vector2::create(-1.0, 1.0));
  const auto& applied = trace.motion.at(41).body;
  CHECK(applied.acceleration().x() < 0.0);
  CHECK(applied.acceleration().y() > 0.0);
  CHECK(std::abs(applied.acceleration().x()) == std::abs(applied.acceleration().y()));
  CHECK(std::abs(magnitude(applied.acceleration()) - fixture.acceleration) < kComponentTolerance);
  CHECK(trace.motion.at(80).body.velocity().x() < fixture.initial_x);
  CHECK(trace.motion.at(80).body.velocity().y() > fixture.initial_y);
  check_qualified_hold(trace, 0, fixture);
}

TEST_CASE("Zero arrival brake or zero acceleration retains ordinary coast and drag",
          "[unit][controllers][tactical][arrival][dynamics]") {
  for (const double drag : kDrags) {
    for (const bool acceleration_available : {true, false}) {
      BrakeFixture fixture;
      fixture.drag = drag;
      fixture.acceleration = acceleration_available ? 400.0 : 0.0;
      fixture.fraction = acceleration_available ? 0.0 : 1.0;
      fixture.initial_y = 6.0;
      const auto trace = run_brake(fixture, 80);
      REQUIRE(trace.decisions.size() == 2);
      CHECK(trace.decisions.front().tick == 40);
      CHECK(trace.decisions.back().tick == 80);
      for (const auto& decision : trace.decisions) {
        CHECK(decision.direction == simulation::Vector2::create(0.0, 0.0));
      }
      for (const auto& frame : trace.motion) {
        CHECK(frame.body.acceleration() == simulation::Vector2::create(0.0, 0.0));
      }
      // A free-drag reference, not a brake model: neither control can author acceleration here.
      const double decay = std::pow(1.0 - drag / 400.0, 80.0);
      CAPTURE(drag, acceleration_available);
      CHECK(std::abs(trace.motion.back().body.velocity().x() - fixture.initial_x * decay) <
            kComponentTolerance);
      CHECK(std::abs(trace.motion.back().body.velocity().y() - fixture.initial_y * decay) <
            kComponentTolerance);
    }
  }
}
