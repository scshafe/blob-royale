#include "shared/guarded_pair_contact.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "continuous_motion.hpp"
#include "controller_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "physics.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::Vector2 vector(const double x, const double y = 0.0) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::EntityId entity(const simulation::EntityId::Value value) {
  return simulation::EntityId::create(value);
}

// The pair is exactly touching in its authored orientation. Every pure-core test below uses these
// named bodies; integration into a live mode is deliberately deferred by the Step 4 boundary.
[[nodiscard]] simulation::PhysicsBody moving_body(const double x, const double speed,
                                                  const double mass = 1.0,
                                                  const double restitution = 1.0) {
  return simulation::PhysicsBody::create(vector(x, 100.0), vector(speed), vector(3.0, 4.0), 10.0,
                                         mass, simulation::PhysicsBody::kDefaultCollisionLayer,
                                         simulation::PhysicsBody::kDefaultCollisionMask, false)
      .with_restitution(restitution);
}

struct GuardPairFixture final {
  GuardPairFixture(simulation::PhysicsBody first_body, simulation::PhysicsBody second_body)
      : world(simulation::GameWorld::create({})), first{entity(1), first_body},
        second{entity(2), second_body}, harness(simulation::TickSequence::create(1)) {
    world.mutable_match().phase = simulation::MatchPhase::kRunning;
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(first.entity, first.body);
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(second.entity, second.body);
    if (!first.body.is_static()) {
      world.mutable_store<simulation::Controllable>().insert_or_assign(
          first.entity, simulation::Controllable{simulation::ControllerId::create(1)});
    }
    if (!second.body.is_static()) {
      world.mutable_store<simulation::Controllable>().insert_or_assign(
          second.entity, simulation::Controllable{simulation::ControllerId::create(2)});
    }
  }

  [[nodiscard]] simulation::PlayerPairContact contact() const {
    return simulation::detect_pair_contact(first.body, second.body, 20.0);
  }

  [[nodiscard]] gameplay::GuardedPairOutcome compose(const gameplay::PairGuardFacts& guards) const {
    return gameplay::compose_guarded_pair(world, first, second, contact(), harness.context(),
                                          guards);
  }

  void make_lethal(const simulation::EntityId id) {
    world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
        id, simulation::LethalOnContact{});
  }

  simulation::GameWorld world;
  simulation::ContactRule::Subject first;
  simulation::ContactRule::Subject second;
  testing::TickHarness harness;
};

template <class Fact>
[[nodiscard]] std::size_t fact_count(const gameplay::GuardedPairOutcome& outcome) {
  return static_cast<std::size_t>(std::count_if(outcome.effects.begin(), outcome.effects.end(),
                                                [](const gameplay::GuardedPairConsequence& fact) {
                                                  return std::holds_alternative<Fact>(fact);
                                                }));
}

[[nodiscard]] double kinetic_energy(const simulation::PhysicsBody& body) {
  return 0.5 * body.mass() *
         ((body.velocity().x() * body.velocity().x()) +
          (body.velocity().y() * body.velocity().y()));
}

void check_non_closing(const gameplay::GuardedPairOutcome& outcome,
                       const simulation::PlayerPairContact& contact) {
  const simulation::Vector2& normal = contact.normal();
  const double speed =
      ((outcome.second.body.velocity().x() - outcome.first.body.velocity().x()) * normal.x()) +
      ((outcome.second.body.velocity().y() - outcome.first.body.velocity().y()) * normal.y());
  CHECK(speed >= -simulation::kVelocityTolerance);
}

} // namespace

TEST_CASE("unguarded composition retains the accepted baseline contact-taking equation",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -2.0)};
  const auto expected = simulation::resolve_player_pair_collision(
      fixture.first.body, fixture.second.body, fixture.contact());
  const auto result = fixture.compose({});
  CHECK(result.first.body.velocity() == expected.first_velocity());
  CHECK(result.second.body.velocity() == expected.second_velocity());
  CHECK(result.first.body.acceleration() == fixture.first.body.acceleration());
  CHECK(result.effects.size() == 1);
}

TEST_CASE("ordinary dual shields project their still-closing quartered velocities to rest",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -10.0)};
  const auto result =
      fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kOrdinary});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body.velocity() == vector(0.0));
  CHECK(fact_count<gameplay::GuardedPairStunFact>(result) == 0);
  check_non_closing(result, fixture.contact());
  // Before correction the nominal velocities are +5 and -5, with total energy 25.
  CHECK(kinetic_energy(result.first.body) + kinetic_energy(result.second.body) <= 25.0);
}

TEST_CASE("guarded unequal masses and restitution use the existing general impulse",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0, 2.0, 0.5), moving_body(120.0, -2.0)};
  const auto ordinary = simulation::resolve_general_pair_collision(
      fixture.first.body, fixture.second.body, fixture.contact());
  REQUIRE(ordinary.first_velocity() == vector(4.0));
  REQUIRE(ordinary.second_velocity() == vector(10.0));
  const auto result =
      fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kNone});
  CHECK(result.first.body.velocity() == vector(8.5));
  CHECK(result.second.body.velocity() == vector(10.0));
  check_non_closing(result, fixture.contact());
}

TEST_CASE("dual guarded unequal masses share a momentum-weighted normal velocity",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0, 2.0, 0.5), moving_body(120.0, -2.0)};
  const auto result =
      fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kOrdinary});
  CHECK(result.first.body.velocity() == vector(6.0));
  CHECK(result.second.body.velocity() == vector(6.0));
  // Quartered pre-projection velocities are 8.5 and 1: energy 72.75, momentum 18.
  CHECK(kinetic_energy(result.first.body) + kinetic_energy(result.second.body) <= 72.75);
  CHECK((2.0 * result.first.body.velocity().x()) + result.second.body.velocity().x() == 18.0);
}

TEST_CASE("a guarded static wall response stops closing motion without stunning the wall",
          "[unit][gameplay][shared][guarded_pair]") {
  const auto wall = simulation::PhysicsBody::create_static(vector(120.0, 100.0));
  const GuardPairFixture fixture{moving_body(100.0, 10.0), wall};
  const auto result =
      fixture.compose({gameplay::GuardState::kPerfect, gameplay::GuardState::kNone});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body == wall);
  CHECK(fact_count<gameplay::GuardedPairStunFact>(result) == 0);
  CHECK(result.first.body.acceleration() == fixture.first.body.acceleration());
}

TEST_CASE("a moving perfect defender cannot drive into its newly stopped incoming source",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -5.0)};
  const auto result =
      fixture.compose({gameplay::GuardState::kPerfect, gameplay::GuardState::kNone});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body.velocity() == vector(0.0));
  CHECK(result.second.body.acceleration() == vector(0.0));
  CHECK_FALSE(result.second.body.is_static());
  CHECK(result.second.disposition == simulation::MotionDisposition::kContinue);
  REQUIRE(fact_count<gameplay::GuardedPairStunFact>(result) == 1);
  CHECK(std::get<gameplay::GuardedPairStunFact>(result.effects.front()).entity ==
        fixture.second.entity);
  check_non_closing(result, fixture.contact());
}

TEST_CASE("a static body's stored velocity is preserved but never enters separation motion",
          "[unit][gameplay][shared][guarded_pair]") {
  const auto wall = simulation::PhysicsBody::create_static(vector(120.0, 100.0))
                        .with_velocity(vector(-100.0, 40.0));
  const GuardPairFixture fixture{moving_body(100.0, 10.0), wall};
  // The continuous owner certifies stationary geometry while preserving the authored subject.
  const auto contact =
      simulation::detect_pair_contact(fixture.first.body, wall.with_velocity(vector(0.0)), 20.0);
  const auto result = gameplay::compose_guarded_pair(
      fixture.world, fixture.first, fixture.second, contact, fixture.harness.context(),
      gameplay::PairGuardFacts{gameplay::GuardState::kOrdinary, gameplay::GuardState::kNone});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body == wall);
  CHECK(fact_count<gameplay::GuardedPairStunFact>(result) == 0);
  CHECK(kinetic_energy(result.first.body) <= kinetic_energy(fixture.first.body));
}

TEST_CASE("simultaneous eligible perfects stop both bodies and emit canonical mutual stun facts",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -5.0)};
  const auto result =
      fixture.compose({gameplay::GuardState::kPerfect, gameplay::GuardState::kPerfect});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body.velocity() == vector(0.0));
  CHECK(result.first.body.acceleration() == vector(0.0));
  CHECK(result.second.body.acceleration() == vector(0.0));
  REQUIRE(result.effects.size() == 3);
  CHECK(std::get<gameplay::GuardedPairStunFact>(result.effects[0]).entity == fixture.first.entity);
  CHECK(std::get<gameplay::GuardedPairStunFact>(result.effects[1]).entity == fixture.second.entity);
  CHECK(std::holds_alternative<gameplay::GuardedPairContactFact>(result.effects[2]));
}

TEST_CASE("ramming stationary or away-moving targets never earns the perfect bonus",
          "[unit][gameplay][shared][guarded_pair]") {
  for (const double target_speed : std::array{0.0, 9.0}) {
    const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, target_speed)};
    const auto result =
        fixture.compose({gameplay::GuardState::kPerfect, gameplay::GuardState::kNone});
    CHECK(fact_count<gameplay::GuardedPairStunFact>(result) == 0);
    CHECK(result.second.body.velocity() == vector(10.0));
    CHECK(result.second.body.acceleration() == fixture.second.body.acceleration());
  }
}

TEST_CASE("quartering the received impulse can increase world-frame kinetic energy",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, 9.0)};
  const auto result =
      fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kNone});
  CHECK(result.first.body.velocity() == vector(9.75));
  CHECK(result.second.body.velocity() == vector(10.0));
  CHECK(2.0 * (kinetic_energy(fixture.first.body) + kinetic_energy(fixture.second.body)) == 181.0);
  CHECK(2.0 * (kinetic_energy(result.first.body) + kinetic_energy(result.second.body)) == 195.0625);
}

TEST_CASE("unguarded lethal contact preserves both bodies and terminates only the victim",
          "[unit][gameplay][shared][guarded_pair]") {
  GuardPairFixture fixture{moving_body(100.0, 30.0), moving_body(120.0, 0.0)};
  fixture.make_lethal(fixture.first.entity);
  const auto result = fixture.compose({});
  CHECK(result.first.body == fixture.first.body);
  CHECK(result.second.body == fixture.second.body);
  CHECK(result.first.disposition == simulation::MotionDisposition::kContinue);
  CHECK(result.second.disposition == simulation::MotionDisposition::kTerminate);
  REQUIRE(result.effects.size() == 2);
  CHECK(std::get<gameplay::GuardedPairEliminationFact>(result.effects[0]).entity ==
        fixture.second.entity);
  CHECK(std::holds_alternative<gameplay::GuardedPairContactFact>(result.effects[1]));
}

TEST_CASE("two unguarded lethal players terminate symmetrically without impulses",
          "[unit][gameplay][shared][guarded_pair]") {
  GuardPairFixture fixture{moving_body(100.0, 30.0), moving_body(120.0, -10.0)};
  fixture.make_lethal(fixture.first.entity);
  fixture.make_lethal(fixture.second.entity);
  const auto result = fixture.compose({});
  CHECK(result.first.body == fixture.first.body);
  CHECK(result.second.body == fixture.second.body);
  CHECK(result.first.disposition == simulation::MotionDisposition::kTerminate);
  CHECK(result.second.disposition == simulation::MotionDisposition::kTerminate);
  REQUIRE(result.effects.size() == 3);
  CHECK(std::get<gameplay::GuardedPairEliminationFact>(result.effects[0]).entity ==
        fixture.first.entity);
  CHECK(std::get<gameplay::GuardedPairEliminationFact>(result.effects[1]).entity ==
        fixture.second.entity);
}

TEST_CASE("guarded lethal contact enters the ordinary physical composition",
          "[unit][gameplay][shared][guarded_pair]") {
  GuardPairFixture fixture{moving_body(100.0, 30.0), moving_body(120.0, 0.0)};
  fixture.make_lethal(fixture.first.entity);
  const auto result =
      fixture.compose({gameplay::GuardState::kNone, gameplay::GuardState::kOrdinary});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body.velocity() == vector(7.5));
  CHECK(result.first.disposition == simulation::MotionDisposition::kContinue);
  CHECK(result.second.disposition == simulation::MotionDisposition::kContinue);
  CHECK(fact_count<gameplay::GuardedPairEliminationFact>(result) == 0);
}

TEST_CASE("lethality reads the committed running phase instead of marker presence alone",
          "[unit][gameplay][shared][guarded_pair]") {
  for (const auto phase :
       std::array{simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                  simulation::MatchPhase::kEnded}) {
    GuardPairFixture fixture{moving_body(100.0, 30.0), moving_body(120.0, 0.0)};
    fixture.make_lethal(fixture.first.entity);
    fixture.world.mutable_match().phase = phase;
    const auto result = fixture.compose({});
    CHECK(result.first.body.velocity() == vector(0.0));
    CHECK(result.second.body.velocity() == vector(30.0));
    CHECK(fact_count<gameplay::GuardedPairEliminationFact>(result) == 0);
  }
}

TEST_CASE("pair reversal preserves physical results and canonical consequence order",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0, 2.0, 0.5), moving_body(120.0, -5.0)};
  const gameplay::PairGuardFacts guards{gameplay::GuardState::kPerfect,
                                        gameplay::GuardState::kPerfect};
  const auto forward = fixture.compose(guards);
  const auto reversed_contact =
      simulation::detect_pair_contact(fixture.second.body, fixture.first.body, 20.0);
  const auto reversed = gameplay::compose_guarded_pair(
      fixture.world, fixture.second, fixture.first, reversed_contact, fixture.harness.context(),
      gameplay::PairGuardFacts{guards.second, guards.first});
  CHECK(forward.first == reversed.second);
  CHECK(forward.second == reversed.first);
  CHECK(forward.effects == reversed.effects);
}

TEST_CASE("perfect facts are frozen and not consumed by repeated composition calls",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -5.0)};
  const gameplay::PairGuardFacts guards{gameplay::GuardState::kPerfect,
                                        gameplay::GuardState::kNone};
  const auto first_call = fixture.compose(guards);
  const auto second_call = fixture.compose(guards);
  CHECK(first_call == second_call);
  CHECK(guards.first == gameplay::GuardState::kPerfect);
  CHECK(*fixture.world.store<simulation::PhysicsBody>().find(fixture.second.entity) ==
        fixture.second.body);
}

TEST_CASE("a newly stunned body loses acceleration but can receive a later external bump",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -5.0)};
  const auto stopped =
      fixture.compose({gameplay::GuardState::kNone, gameplay::GuardState::kPerfect});
  REQUIRE(stopped.first.body.velocity() == vector(0.0));
  REQUIRE(stopped.first.body.acceleration() == vector(0.0));
  const simulation::ContactRule::Subject bumped{fixture.first.entity, stopped.first.body};
  const simulation::ContactRule::Subject bumper{entity(3), moving_body(120.0, -10.0)};
  const auto contact = simulation::detect_pair_contact(bumped.body, bumper.body, 20.0);
  const auto result = gameplay::compose_guarded_pair(fixture.world, bumped, bumper, contact,
                                                     fixture.harness.context(), {});
  CHECK(result.first.body.velocity() == vector(-10.0));
  CHECK(result.first.body.acceleration() == vector(0.0));
  CHECK_FALSE(result.first.body.is_static());
  CHECK(result.first.disposition == simulation::MotionDisposition::kContinue);
}

TEST_CASE("non-contact inputs emit no guarded or lethal facts",
          "[unit][gameplay][shared][guarded_pair]") {
  GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(140.0, -5.0)};
  fixture.make_lethal(fixture.first.entity);
  const auto result =
      fixture.compose({gameplay::GuardState::kPerfect, gameplay::GuardState::kPerfect});
  CHECK(result.first.body == fixture.first.body);
  CHECK(result.second.body == fixture.second.body);
  CHECK(result.effects.empty());
}

TEST_CASE("invalid guard states and static guards fail with the named facts error",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0),
                                 simulation::PhysicsBody::create_static(vector(120.0, 100.0))};
  for (const gameplay::PairGuardFacts guards :
       std::array{gameplay::PairGuardFacts{static_cast<gameplay::GuardState>(255),
                                           gameplay::GuardState::kNone},
                  gameplay::PairGuardFacts{gameplay::GuardState::kNone,
                                           gameplay::GuardState::kOrdinary}}) {
    try {
      static_cast<void>(fixture.compose(guards));
      FAIL("invalid guard facts must be rejected");
    } catch (const simulation::SimulationValidationError& error) {
      CHECK(error.validation_code() ==
            simulation::SimulationValidationCode::kGuardedPairFactsInvalid);
    }
  }
}

TEST_CASE("oblique separation preserves tangential motion and dissipates only closing energy",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 10.0).with_velocity(vector(10.0, 3.0)),
                                 moving_body(112.0, -4.0)
                                     .with_position(vector(112.0, 116.0))
                                     .with_velocity(vector(-4.0, -2.0))};
  const auto result =
      fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kOrdinary});
  CHECK(result.first.body.velocity().x() == Catch::Approx(6.28));
  CHECK(result.first.body.velocity().y() == Catch::Approx(-1.96));
  CHECK(result.second.body.velocity().x() == Catch::Approx(-0.28));
  CHECK(result.second.body.velocity().y() == Catch::Approx(2.96));
  CHECK((-0.8 * result.first.body.velocity().x()) + (0.6 * result.first.body.velocity().y()) ==
        Catch::Approx(-6.2));
  CHECK((-0.8 * result.second.body.velocity().x()) + (0.6 * result.second.body.velocity().y()) ==
        Catch::Approx(2.0));
  CHECK(kinetic_energy(result.first.body) + kinetic_energy(result.second.body) ==
        Catch::Approx(26.06));
  CHECK(kinetic_energy(result.first.body) + kinetic_energy(result.second.body) < 35.67);
  check_non_closing(result, fixture.contact());
}

TEST_CASE("quartered scalar intermediates can exceed the vector bound while the result fits",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{moving_body(100.0, 800'000'000'000.0),
                                 moving_body(120.0, -800'000'000'000.0)};
  const auto result =
      fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kOrdinary});
  CHECK(result.first.body.velocity() == vector(0.0));
  CHECK(result.second.body.velocity() == vector(0.0));
}

TEST_CASE("unrepresentable one-pass separation fails instead of hiding a high-speed residual",
          "[unit][gameplay][shared][guarded_pair]") {
  // For this 3-4-5 normal and written binary64 operation order, projection leaves a negative
  // residual around 1.9e-6. The prototype refuses it; it does not invent a second tolerance/nudge.
  const GuardPairFixture fixture{
      moving_body(100.0, 0.0).with_velocity(vector(10'000'000'340.2, 47.4)),
      moving_body(112.0, 0.0)
          .with_position(vector(112.0, 116.0))
          .with_velocity(vector(-6'999'999'993.4, 1'000'000'044.4))};
  try {
    static_cast<void>(
        fixture.compose({gameplay::GuardState::kOrdinary, gameplay::GuardState::kOrdinary}));
    FAIL("a closing residual beyond the velocity tolerance must be rejected");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kGuardedPairSeparationFailed);
  }
}

TEST_CASE("unguarded restitution-zero keeps the general equation bits including closing roundoff",
          "[unit][gameplay][shared][guarded_pair]") {
  const GuardPairFixture fixture{
      moving_body(100.0, 0.0, 2.0, 0.0).with_velocity(vector(10'000'005'783.4, 805.8)),
      moving_body(112.0, 0.0, 3.0, 0.0)
          .with_position(vector(112.0, 116.0))
          .with_velocity(vector(-6'999'999'887.8, 1'000'000'754.8))};
  const auto contact = fixture.contact();
  const auto ordinary =
      simulation::resolve_general_pair_collision(fixture.first.body, fixture.second.body, contact);
  const double residual =
      ((ordinary.second_velocity().x() - ordinary.first_velocity().x()) * contact.normal().x()) +
      ((ordinary.second_velocity().y() - ordinary.first_velocity().y()) * contact.normal().y());
  // This exposes the base equation's limit rather than silently assigning defense's projection
  // to unguarded contacts. Continuous re-contact handling remains an explicit solver concern.
  REQUIRE(residual < -simulation::kVelocityTolerance);
  const auto result = fixture.compose({});
  CHECK(std::bit_cast<std::uint64_t>(result.first.body.velocity().x()) ==
        std::bit_cast<std::uint64_t>(ordinary.first_velocity().x()));
  CHECK(std::bit_cast<std::uint64_t>(result.first.body.velocity().y()) ==
        std::bit_cast<std::uint64_t>(ordinary.first_velocity().y()));
  CHECK(std::bit_cast<std::uint64_t>(result.second.body.velocity().x()) ==
        std::bit_cast<std::uint64_t>(ordinary.second_velocity().x()));
  CHECK(std::bit_cast<std::uint64_t>(result.second.body.velocity().y()) ==
        std::bit_cast<std::uint64_t>(ordinary.second_velocity().y()));
}

TEST_CASE("the continuous driver resolves moving and mutual perfects without zero-time repeats",
          "[unit][gameplay][shared][guarded_pair][continuous_motion]") {
  for (const auto guards : std::array{
           gameplay::PairGuardFacts{gameplay::GuardState::kPerfect, gameplay::GuardState::kNone},
           gameplay::PairGuardFacts{gameplay::GuardState::kPerfect,
                                    gameplay::GuardState::kPerfect}}) {
    const GuardPairFixture fixture{moving_body(100.0, 10.0), moving_body(120.0, -5.0)};
    const std::array subjects{fixture.first, fixture.second};
    const auto result = simulation::solve_continuous_motion<gameplay::GuardedPairConsequence,
                                                            gameplay::PairGuardFacts>(
        fixture.world, subjects, fixture.harness.context(), guards, gameplay::compose_guarded_pair);
    REQUIRE(result.motion.bodies.size() == 2);
    CHECK(result.motion.bodies[0].result.body.velocity() == vector(0.0));
    CHECK(result.motion.bodies[1].result.body.velocity() == vector(0.0));
    CHECK(result.motion.bodies[0].result.disposition == simulation::MotionDisposition::kContinue);
    CHECK(result.motion.bodies[1].result.disposition == simulation::MotionDisposition::kContinue);
    CHECK(result.motion.bodies[0].result.body.position() == fixture.first.body.position());
    CHECK(result.motion.bodies[1].result.body.position() == fixture.second.body.position());
    REQUIRE(result.motion.events.size() == 1);
    CHECK(result.motion.events.front().time() == simulation::MotionTime::start());
    CHECK(result.motion.work.events == 1);
    const auto stun_count =
        std::count_if(result.effects.begin(), result.effects.end(), [](const auto& effect) {
          return std::holds_alternative<gameplay::GuardedPairStunFact>(effect.effect);
        });
    CHECK(stun_count == (guards.second == gameplay::GuardState::kPerfect ? 2 : 1));
  }
}
