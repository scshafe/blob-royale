#include "shared/thrust_steering_system.hpp"

#include "fixtures/thrust_steering_fixture.hpp"
#include "gameplay_test_fixture.hpp"

#include "events/elimination_event.hpp"
#include "input_batch.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "royale/royale_mode.hpp"
#include "sandbox/sandbox_mode.hpp"
#include "shared/respawn_system.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "spawn_seating.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace fixture = blob_royale::testing::thrust_steering_fixture;

namespace {

constexpr double kThrustMaximum = 400.0;

// Two seated entities in a sandbox simulation, so "moves the intended entity and only that one" is
// a question the world can answer. Entity 1 sits at (100, 320) and entity 2 at (200, 320).
[[nodiscard]] simulation::GameSimulation seated_pair() {
  simulation::GameSimulation game =
      testing::gameplay_simulation(gameplay::SandboxMode::create(), testing::gameplay_map(4));
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7), testing::spawn_command(8)}));
  return game;
}

} // namespace

TEST_CASE("steered_acceleration applies the magnitude clamp exactly once, as written",
          "[unit][gameplay][thrust_steering]") {
  // `(1, 1)`: m = sqrt(2), s = 1 / sqrt(2), each component is (1 * s) * thrust_max. Written as the
  // ADR writes it rather than as `thrust_max / sqrt(2)`, because those are different binary64
  // values and the operation order is the contract.
  const double diagonal_magnitude = std::sqrt((1.0 * 1.0) + (1.0 * 1.0));
  const double diagonal_scale = 1.0 / diagonal_magnitude;
  const simulation::Vector2 diagonal =
      gameplay::steered_acceleration(simulation::Vector2::create(1.0, 1.0), kThrustMaximum);
  CHECK(diagonal.x() == (1.0 * diagonal_scale) * kThrustMaximum);
  CHECK(diagonal.y() == (1.0 * diagonal_scale) * kThrustMaximum);

  // Clamping once means the result has magnitude thrust_max; clamping twice would scale by
  // 1/sqrt(2) again and land at thrust_max / 2. The magnitude is compared with ADR 0003's
  // absolute-plus-relative tolerance rather than for bit equality, because
  // `sqrt(((1/sqrt(2)) * 400)^2 * 2)` rounds to 399.99999999999994 and the exact-magnitude claim of
  // `docs/architecture/0005-royale-mode.md` § "Steering" is about the arithmetic, not about a
  // round trip through a second square root. The exact claim is the component equality above.
  const double diagonal_magnitude_result =
      std::sqrt((diagonal.x() * diagonal.x()) + (diagonal.y() * diagonal.y()));
  CHECK(simulation::approximately_equal(diagonal_magnitude_result, kThrustMaximum,
                                        simulation::kAccelerationTolerance));
  // The clamp is applied once and not twice, stated as the number a second clamp would produce.
  CHECK(diagonal.x() != ((1.0 * diagonal_scale) * diagonal_scale) * kThrustMaximum);
  CHECK(gameplay::steered_acceleration(diagonal, 1.0) != diagonal);

  // A direction inside the unit disc keeps its magnitude: s is exactly 1.0, and multiplying by 1.0
  // is the identity on every finite binary64 value, so this is (0.5 * 1.0) * 400 and not a
  // renormalization to full thrust.
  const simulation::Vector2 partial =
      gameplay::steered_acceleration(simulation::Vector2::create(0.5, 0.0), kThrustMaximum);
  CHECK(partial.x() == (0.5 * 1.0) * kThrustMaximum);
  CHECK(partial.y() == 0.0);

  // The unit axis is the boundary case m == 1, which takes the `s = 1` arm.
  const simulation::Vector2 axis =
      gameplay::steered_acceleration(simulation::Vector2::create(0.0, -1.0), kThrustMaximum);
  CHECK(axis.x() == 0.0);
  CHECK(axis.y() == -kThrustMaximum);

  // `(0, 0)` is the coast command and stores zero acceleration rather than dividing by zero.
  const simulation::Vector2 coast =
      gameplay::steered_acceleration(simulation::Vector2::create(0.0, 0.0), kThrustMaximum);
  CHECK(coast == simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("A thrust command accelerates the intended entity and only that one",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = seated_pair();
  const simulation::Vector2 entity_two_position =
      testing::published_body(game.snapshot(), 2)->position();

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::thrust_command(1, 1.0, 0.0)}));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  const simulation::PhysicsBody steered = *testing::published_body(snapshot, 1);
  const simulation::PhysicsBody untouched = *testing::published_body(snapshot, 2);

  CHECK(steered.acceleration() == simulation::Vector2::create(kThrustMaximum, 0.0));
  CHECK(steered.velocity().x() == kThrustMaximum * testing::kGameplayFixedDelta.seconds());
  CHECK(steered.position().x() > 100.0);

  // The entity that was not addressed keeps the at-rest body the seating gave it: no acceleration,
  // no velocity, and the position it was seated at.
  CHECK(untouched.acceleration() == simulation::Vector2::create(0.0, 0.0));
  CHECK(untouched.velocity() == simulation::Vector2::create(0.0, 0.0));
  CHECK(untouched.position() == entity_two_position);
}

TEST_CASE("Held thrust preserves uncapped acceleration until the next thrust for that entity",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = seated_pair();

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::thrust_command(1, 1.0, 0.0)}));
  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());

  // Unchanged tuning and an inactive ceiling reproduce the stored-acceleration baseline from the
  // retained normalized intent, rather than zeroing it or normalizing it again.
  CHECK(testing::published_body(game.snapshot(), 1)->acceleration() ==
        simulation::Vector2::create(kThrustMaximum, 0.0));
  CHECK(testing::published_body(game.snapshot(), 1)->velocity().x() ==
        2.0 * (kThrustMaximum * testing::kGameplayFixedDelta.seconds()));

  // The coast command is how a controller stops accelerating, and it is a thrust like any other.
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::thrust_command(1, 0.0, 0.0)}));
  CHECK(testing::published_body(game.snapshot(), 1)->acceleration() ==
        simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("A thrust naming an entity that owns no body is skipped rather than failing the tick",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game =
      testing::gameplay_simulation(gameplay::SandboxMode::create(), testing::gameplay_map(1));

  // Two joiners and one point: entity 2 is created carrying only its controller link and is
  // deferred, so this tick's thrust for it has nothing to write.
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7), testing::spawn_command(8)}));
  REQUIRE_FALSE(testing::published_body(game.snapshot(), 2).has_value());

  CHECK_NOTHROW(game.step(testing::kGameplayFixedDelta,
                          testing::gameplay_batch(game, {testing::thrust_command(2, 1.0, 0.0)})));
  CHECK(testing::published_player_count(game) == 1);
}

TEST_CASE("A thrust for an entity a mode did not seat cannot address a foreign body",
          "[unit][gameplay][thrust_steering]") {
  simulation::GameSimulation game = seated_pair();

  // A command naming an id no entity holds is ignored by the tick rather than failing it, because
  // the command source is a network session and one client must not be able to stop the match.
  CHECK_NOTHROW(game.step(testing::kGameplayFixedDelta,
                          testing::gameplay_batch(game, {testing::thrust_command(404, 1.0, 0.0)})));
  CHECK(testing::published_body(game.snapshot(), 1)->acceleration() ==
        simulation::Vector2::create(0.0, 0.0));
  CHECK(testing::published_body(game.snapshot(), 2)->acceleration() ==
        simulation::Vector2::create(0.0, 0.0));
}

TEST_CASE("ThrustSteeringSystem is scalar-free and preserves its registered name",
          "[unit][gameplay][thrust_steering]") {
  CHECK(gameplay::ThrustSteeringSystem::create()->name() == std::string_view{"thrust_steering"});
}

TEST_CASE("steering retunes held analog intent without reclamping in every existing phase",
          "[unit][gameplay][thrust_steering][movement]") {
  const auto system = gameplay::ThrustSteeringSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  for (const auto phase : fixture::kPhases) {
    CAPTURE(phase);
    auto world = fixture::world();
    world.mutable_match().phase = phase;
    auto* controllable =
        world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
    REQUIRE(controllable != nullptr);
    controllable->commands_this_tick.push_back(fixture::analog_command());
    system->apply(world, harness.context());
    REQUIRE(controllable->normalized_thrust_intent.has_value());
    CHECK(*controllable->normalized_thrust_intent == fixture::analog());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
          simulation::Vector2::create(100.0, -300.0));

    controllable->commands_this_tick.clear();
    world.mutable_match().movement.current = fixture::retuned();
    system->apply(world, harness.context());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
          simulation::Vector2::create(200.0, -600.0));
    CHECK(*controllable->normalized_thrust_intent == fixture::analog());

    world.mutable_match().movement.current =
        simulation::MovementTuning::create(0.0, fixture::kInactiveCeiling);
    system->apply(world, harness.context());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
          fixture::zero());
    CHECK(*controllable->normalized_thrust_intent == fixture::analog());
    world.mutable_match().movement.current = fixture::retuned();
    system->apply(world, harness.context());
    CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
          simulation::Vector2::create(200.0, -600.0));
    CHECK(world.match().phase == phase);
  }
}

TEST_CASE("a bodyless thrust cannot become held intent for a later seating",
          "[unit][gameplay][thrust_steering][movement][spawn]") {
  auto world = fixture::world();
  const auto system = gameplay::ThrustSteeringSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  world.mutable_store<simulation::PhysicsBody>().erase(fixture::entity());
  auto* controllable =
      world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
  REQUIRE(controllable != nullptr);
  controllable->commands_this_tick.push_back(fixture::analog_command());
  system->apply(world, harness.context());
  CHECK_FALSE(controllable->normalized_thrust_intent.has_value());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity()) == nullptr);
  // This input belonged to the bodyless tick, not to the later body's first tick.
  controllable->commands_this_tick.clear();
  simulation::seat_body_at_rest(
      world, fixture::entity(),
      simulation::Vector2::create(fixture::kReplacementX, fixture::kReplacementY), 10.0);
  system->apply(world, harness.context());
  CHECK_FALSE(controllable->normalized_thrust_intent.has_value());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
        fixture::zero());
}

TEST_CASE("steering distinguishes absent authored acceleration from an explicit held coast",
          "[unit][gameplay][thrust_steering][movement]") {
  auto world = fixture::world();
  const auto system = gameplay::ThrustSteeringSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  auto* controllable =
      world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
  REQUIRE(controllable != nullptr);
  world.mutable_match().movement.current = fixture::retuned();
  system->apply(world, harness.context());
  CHECK_FALSE(controllable->normalized_thrust_intent.has_value());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
        fixture::authored_acceleration());
  controllable->commands_this_tick.push_back(testing::thrust_command(fixture::kEntity, 0.0, 0.0));
  system->apply(world, harness.context());
  REQUIRE(controllable->normalized_thrust_intent.has_value());
  CHECK(*controllable->normalized_thrust_intent == fixture::zero());
  controllable->commands_this_tick.clear();
  world.mutable_match().movement.current = testing::gameplay_movement_tuning();
  system->apply(world, harness.context());
  CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
        fixture::zero());
  CHECK(controllable->normalized_thrust_intent.has_value());
}

TEST_CASE("steering retains held intent when the cap projects the requested step to coast",
          "[unit][gameplay][thrust_steering][movement]") {
  auto world = fixture::world();
  const auto system = gameplay::ThrustSteeringSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  const auto axis = simulation::Vector2::create(1.0, 0.0);
  world.mutable_match().movement.current = simulation::MovementTuning::create(kThrustMaximum, 1.0);
  auto& bodies = world.mutable_store<simulation::PhysicsBody>();
  bodies.insert_or_assign(fixture::entity(), bodies.find(fixture::entity())->with_velocity(axis));
  auto* controllable =
      world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
  REQUIRE(controllable != nullptr);
  controllable->commands_this_tick.push_back(testing::thrust_command(fixture::kEntity, 1.0, 0.0));
  system->apply(world, harness.context());
  CHECK(bodies.find(fixture::entity())->acceleration() == fixture::zero());
  CHECK(controllable->normalized_thrust_intent == axis);
  CHECK(bodies.find(fixture::entity())->velocity() == axis);
  controllable->commands_this_tick.clear();
  bodies.insert_or_assign(fixture::entity(),
                          bodies.find(fixture::entity())->with_velocity(fixture::zero()));
  system->apply(world, harness.context());
  CHECK(bodies.find(fixture::entity())->acceleration() ==
        simulation::Vector2::create(kThrustMaximum, 0.0));
  CHECK(controllable->normalized_thrust_intent == axis);
}

TEST_CASE("steering a lowered ceiling constrains propulsion without clamping external velocity",
          "[unit][gameplay][thrust_steering][movement]") {
  auto world = fixture::world();
  const auto system = gameplay::ThrustSteeringSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  const auto velocity = simulation::Vector2::create(fixture::kExternalSpeed, 0.0);
  world.mutable_match().movement.current =
      simulation::MovementTuning::create(kThrustMaximum, fixture::kLoweredCeiling);
  auto& bodies = world.mutable_store<simulation::PhysicsBody>();
  bodies.insert_or_assign(fixture::entity(),
                          bodies.find(fixture::entity())->with_velocity(velocity));
  world.mutable_store<simulation::Controllable>()
      .mutable_find(fixture::entity())
      ->normalized_thrust_intent = simulation::Vector2::create(0.0, 1.0);
  system->apply(world, harness.context());
  const auto& body = *bodies.find(fixture::entity());
  CHECK(body.velocity() == velocity);
  CHECK(body.acceleration().y() > 0.0);
  CHECK(body.acceleration().dot(body.acceleration()) <= kThrustMaximum * kThrustMaximum);
  const auto endpoint = simulation::integrate_accelerated_velocity(velocity, body.acceleration(),
                                                                   testing::kGameplayFixedDelta);
  CHECK(endpoint.dot(endpoint) <= velocity.dot(velocity));
  CHECK(endpoint.dot(endpoint) > fixture::kLoweredCeiling * fixture::kLoweredCeiling);
}

TEST_CASE("seating clears previous-body intent but preserves this tick's fresh command",
          "[unit][gameplay][thrust_steering][movement][spawn]") {
  const auto system = gameplay::ThrustSteeringSystem::create();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  for (const bool zero_delay_respawn : {false, true}) {
    for (const bool fresh_command : {false, true}) {
      CAPTURE(zero_delay_respawn, fresh_command);
      auto world = fixture::world();
      auto* controllable =
          world.mutable_store<simulation::Controllable>().mutable_find(fixture::entity());
      REQUIRE(controllable != nullptr);
      controllable->normalized_thrust_intent = simulation::Vector2::create(1.0, 0.0);
      if (fresh_command) {
        controllable->commands_this_tick.push_back(fixture::analog_command());
      }
      const auto commands = controllable->commands_this_tick;
      if (zero_delay_respawn) {
        world.emit(simulation::EliminationEvent{fixture::entity()});
        gameplay::RespawnSystem::create(0)->apply(world, harness.context());
        REQUIRE(world.store<simulation::PhysicsBody>().find(fixture::entity()) == nullptr);
      }
      simulation::seat_body_at_rest(
          world, fixture::entity(),
          simulation::Vector2::create(fixture::kReplacementX, fixture::kReplacementY), 10.0);
      CHECK_FALSE(controllable->normalized_thrust_intent.has_value());
      CHECK(controllable->commands_this_tick == commands);
      const auto& seated = *world.store<simulation::PhysicsBody>().find(fixture::entity());
      CHECK(seated.velocity() == fixture::zero());
      CHECK(seated.acceleration() == fixture::zero());
      system->apply(world, harness.context());
      const auto expected =
          fresh_command ? simulation::Vector2::create(100.0, -300.0) : fixture::zero();
      CHECK(world.store<simulation::PhysicsBody>().find(fixture::entity())->acceleration() ==
            expected);
      CHECK(controllable->normalized_thrust_intent.has_value() == fresh_command);
    }
  }
}

TEST_CASE(
    "a committed tuning command recomputes held thrust on its effective tick only in its room",
    "[unit][gameplay][thrust_steering][movement]") {
  auto first = testing::gameplay_simulation(gameplay::RoyaleMode::create(),
                                            testing::gameplay_map(1), 0, fixture::seated_lobby());
  auto second = testing::gameplay_simulation(gameplay::RoyaleMode::create(),
                                             testing::gameplay_map(1), 0, fixture::seated_lobby());
  for (auto* game : {&first, &second}) {
    game->step(testing::kGameplayFixedDelta,
               testing::gameplay_batch(*game, {testing::spawn_command(fixture::kController),
                                               fixture::analog_command()}));
  }
  first.step(testing::kGameplayFixedDelta,
             testing::gameplay_batch(first, {fixture::retuning_command()}));
  second.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  const auto changed = first.snapshot();
  const auto unchanged = second.snapshot();
  const auto changed_body = testing::published_body(changed, fixture::kEntity);
  const auto unchanged_body = testing::published_body(unchanged, fixture::kEntity);
  REQUIRE(changed_body.has_value());
  REQUIRE(unchanged_body.has_value());
  CHECK(changed.match().movement().current == fixture::retuned());
  CHECK(changed.match().movement().defaults == testing::gameplay_movement_tuning());
  CHECK(changed.match().movement().revision == 1);
  CHECK(changed.match().movement().effective_tick == first.tick_sequence());
  CHECK(changed_body->acceleration() == simulation::Vector2::create(200.0, -600.0));
  CHECK(unchanged.match().movement().current == testing::gameplay_movement_tuning());
  CHECK(unchanged.match().movement().revision == 0);
  CHECK(unchanged_body->acceleration() == simulation::Vector2::create(100.0, -300.0));
}
