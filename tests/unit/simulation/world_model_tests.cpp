#include "component_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "components/team_component.hpp"
#include "controller_id.hpp"
#include "deterministic_random.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "team_id.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::PhysicsBody stationary_body(const double x, const double y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::PhysicsBody::create(simulation::Vector2::create(x, y), zero, zero);
}

[[nodiscard]] simulation::GameWorld::EntitySeed seed(const simulation::EntityId::Value id,
                                                     const double x) {
  return simulation::GameWorld::EntitySeed::create(simulation::EntityId::create(id),
                                                   stationary_body(x, 0.0));
}

[[nodiscard]] simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(
      100.0, 100.0, 5.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 4, 4);
}

} // namespace

TEST_CASE("Vector2 is finite, bounded, and normalizes signed zero", "[unit][simulation][vector2]") {
  const simulation::Vector2 vector = simulation::Vector2::create(-0.0, 4.0);

  CHECK(vector.x() == 0.0);
  CHECK_FALSE(std::signbit(vector.x()));
  CHECK(vector.y() == 4.0);
  CHECK(vector.magnitude() == Catch::Approx(4.0));
  CHECK(vector.dot(simulation::Vector2::create(2.0, 3.0)) == Catch::Approx(12.0));

  CHECK_THROWS_AS(simulation::Vector2::create(std::numeric_limits<double>::infinity(), 0.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::Vector2::create(std::numeric_limits<double>::quiet_NaN(), 0.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(
      simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude + 1.0, 0.0),
      simulation::SimulationValidationError);
}

TEST_CASE("Vector2 checked arithmetic preserves valid results and rejects invalid ones",
          "[unit][simulation][vector2]") {
  const simulation::Vector2 left = simulation::Vector2::create(3.0, -2.0);
  const simulation::Vector2 right = simulation::Vector2::create(-1.0, 6.0);

  CHECK(left + right == simulation::Vector2::create(2.0, 4.0));
  CHECK(left - right == simulation::Vector2::create(4.0, -8.0));
  CHECK(-left == simulation::Vector2::create(-3.0, 2.0));
  CHECK(left * 2.0 == simulation::Vector2::create(6.0, -4.0));
  CHECK(2.0 * left == simulation::Vector2::create(6.0, -4.0));
  CHECK(left / 2.0 == simulation::Vector2::create(1.5, -1.0));

  CHECK_THROWS_AS(left / 0.0, simulation::SimulationValidationError);
  CHECK_THROWS_AS(left * std::numeric_limits<double>::quiet_NaN(),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude, 0.0) +
                      simulation::Vector2::create(1.0, 0.0),
                  simulation::SimulationValidationError);
}

TEST_CASE("EntityId accepts only the protocol-safe integer range",
          "[unit][simulation][entity_id]") {
  const simulation::EntityId minimum = simulation::EntityId::create(simulation::kMinimumEntityId);
  const simulation::EntityId maximum = simulation::EntityId::create(simulation::kMaximumEntityId);

  CHECK(minimum.value() == simulation::kMinimumEntityId);
  CHECK(maximum.value() == simulation::kMaximumEntityId);
  CHECK(minimum < maximum);
  CHECK_THROWS_AS(simulation::EntityId::create(0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::EntityId::create(simulation::kMaximumEntityId + 1),
                  simulation::SimulationValidationError);
}

TEST_CASE("Simulation validation errors expose stable machine-readable context",
          "[unit][simulation][validation]") {
  try {
    static_cast<void>(simulation::EntityId::create(0));
    FAIL("invalid EntityId was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == simulation::SimulationValidationCode::kEntityIdOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.ENTITY_ID_OUT_OF_RANGE"});
    CHECK(error.context() == "entity_id.value");
    CHECK(std::string_view{error.detail()}.find("safe-integer range") != std::string_view::npos);
    CHECK(std::string_view{error.what()}.find("safe-integer range") != std::string_view::npos);
  }
}

TEST_CASE("PhysicsBody replacements preserve value semantics and every unnamed field",
          "[unit][simulation][physics_body]") {
  const simulation::PhysicsBody original_body = simulation::PhysicsBody::create(
      simulation::Vector2::create(1.0, 2.0), simulation::Vector2::create(0.0, 0.0),
      simulation::Vector2::create(0.0, 0.0), 3.0, 7.0, 0b10U, 0b110U, true);
  const simulation::Vector2 new_velocity = simulation::Vector2::create(3.0, 4.0);
  const simulation::PhysicsBody moving_body = original_body.with_velocity(new_velocity);

  CHECK(original_body.velocity() == simulation::Vector2::create(0.0, 0.0));
  CHECK(moving_body.velocity() == new_velocity);
  CHECK(moving_body.position() == original_body.position());
  CHECK(moving_body.radius() == 3.0);
  CHECK(moving_body.mass() == 7.0);
  CHECK(moving_body.drag_scale() == simulation::PhysicsBody::kDefaultDragScale);
  CHECK(moving_body.collision_layer() == 0b10U);
  CHECK(moving_body.collision_mask() == 0b110U);
  CHECK(moving_body.is_static());
}

TEST_CASE("PhysicsBody motion-only creation carries the baseline dynamic disc defaults",
          "[unit][simulation][physics_body]") {
  const simulation::PhysicsBody body = stationary_body(1.0, 2.0);

  CHECK(body.radius() == simulation::PhysicsBody::kUndeclaredRadius);
  CHECK(body.mass() == simulation::PhysicsBody::kDefaultMass);
  CHECK(body.restitution() == simulation::PhysicsBody::kDefaultRestitution);
  CHECK(body.drag_scale() == simulation::PhysicsBody::kDefaultDragScale);
  CHECK(body.collision_layer() == simulation::PhysicsBody::kDefaultCollisionLayer);
  CHECK(body.collision_mask() == simulation::PhysicsBody::kDefaultCollisionMask);
  CHECK_FALSE(body.is_static());
  CHECK(body.bounds_behavior() == simulation::PhysicsBody::kDefaultBoundsBehavior);
  CHECK(body.ground_attachment() == simulation::GroundAttachment::kFloating);
  CHECK_FALSE(body.crosses_bounds());
}

TEST_CASE("every PhysicsBody factory defaults to the baseline physics and to folding",
          "[unit][simulation][physics_body]") {
  // The defaults are what keep this an addition: a body built by any route that existed before
  // restitution, drag scale, and bounds behaviour did is exactly the body ADR 0003's accepted
  // equations describe, so `body_has_baseline_physics` holds for every one of them and phase 1
  // drags every one of them by the arithmetic it always used.
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  const simulation::PhysicsBody complete = simulation::PhysicsBody::create(
      simulation::Vector2::create(1.0, 2.0), zero, zero, 3.0, 7.0, 0b10U, 0b110U, false);
  const simulation::PhysicsBody wall =
      simulation::PhysicsBody::create_static(simulation::Vector2::create(1.0, 2.0));

  CHECK(complete.restitution() == simulation::PhysicsBody::kDefaultRestitution);
  CHECK(complete.drag_scale() == simulation::PhysicsBody::kDefaultDragScale);
  CHECK_FALSE(complete.crosses_bounds());
  CHECK(complete.ground_attachment() == simulation::GroundAttachment::kFloating);
  CHECK(wall.restitution() == simulation::PhysicsBody::kDefaultRestitution);
  CHECK(wall.drag_scale() == simulation::PhysicsBody::kDefaultDragScale);
  CHECK_FALSE(wall.crosses_bounds());
  CHECK(wall.ground_attachment() == simulation::GroundAttachment::kFloating);
  // The default is exactly one, which is the whole of the bit-identity argument: `d * 1.0 == d`
  // for every finite `d` in binary64, so phase 1 forms the identical factor it always did.
  CHECK(simulation::PhysicsBody::kDefaultDragScale == 1.0);
  CHECK(simulation::body_has_baseline_physics(stationary_body(1.0, 2.0)));
  CHECK(simulation::body_has_baseline_physics(wall));
  // A declared mass of seven is not the baseline, which is exactly what the general impulse row is
  // predicated on.
  CHECK_FALSE(simulation::body_has_baseline_physics(complete));
}

TEST_CASE("PhysicsBody withers preserve restitution, drag scale, and bounds behavior",
          "[unit][simulation][physics_body]") {
  // Every wither routes through the one validating factory, so a value set once survives every
  // later replacement. A wither that dropped one of these would silently return a crossing hazard
  // to the arena walls on the first tick that changed its velocity -- and phase 1 replaces a
  // dragged body's velocity on *every* tick, so a dropped drag scale would be undone immediately
  // and permanently.
  const simulation::PhysicsBody hazard =
      stationary_body(1.0, 2.0)
          .with_mass(40.0)
          .with_restitution(0.25)
          .with_drag_scale(0.0)
          .with_bounds_behavior(simulation::BoundsBehavior::kCross);

  CHECK(hazard.mass() == 40.0);
  CHECK(hazard.restitution() == 0.25);
  CHECK(hazard.drag_scale() == 0.0);
  CHECK(hazard.crosses_bounds());
  CHECK_FALSE(simulation::body_has_baseline_physics(hazard));

  const simulation::PhysicsBody moved =
      hazard.with_velocity(simulation::Vector2::create(5.0, 0.0))
          .with_position(simulation::Vector2::create(9.0, 9.0))
          .with_acceleration(simulation::Vector2::create(1.0, 1.0))
          .with_radius(4.0);
  CHECK(moved.mass() == 40.0);
  CHECK(moved.restitution() == 0.25);
  CHECK(moved.drag_scale() == 0.0);
  CHECK(moved.crosses_bounds());
}

TEST_CASE("a declared drag scale is invisible to the collision predicate",
          "[unit][simulation][physics_body]") {
  // `body_has_baseline_physics` gates which *collision* equation phase 3 hands a pair to, and no
  // collision equation reads drag. A unit-mass, perfectly elastic body that merely coasts is still
  // exactly the body ADR 0003 § "Player-pair policy" is written for, so it must keep taking the
  // accepted exchange: the general impulse is deliberately not bit-identical to it, and routing a
  // pair there because one body ignores drag would change contact arithmetic for a reason that has
  // nothing to do with contact. This is the test that fails if a future property is added to the
  // predicate by reflex.
  const simulation::PhysicsBody coasting = stationary_body(1.0, 2.0).with_drag_scale(0.0);

  CHECK(coasting.drag_scale() == 0.0);
  CHECK(simulation::body_has_baseline_physics(coasting));
  // And the converse: a body that differs in something a contact rule *does* read is not baseline,
  // whatever it declares about drag.
  CHECK_FALSE(simulation::body_has_baseline_physics(coasting.with_mass(200.0)));
}

TEST_CASE("ground attachment is validated and preserved by every other body wither",
          "[unit][simulation][physics_body][ground_attachment]") {
  const auto original =
      stationary_body(20, 30).with_ground_attachment(simulation::GroundAttachment::kGroundBound);
  for (const auto& changed :
       {original.with_position(simulation::Vector2::create(21, 31)),
        original.with_velocity(simulation::Vector2::create(2, 3)),
        original.with_acceleration(simulation::Vector2::create(4, 5)), original.with_radius(6),
        original.with_mass(7), original.with_restitution(0.5), original.with_drag_scale(0),
        original.with_bounds_behavior(simulation::BoundsBehavior::kCross)}) {
    CHECK(changed.ground_attachment() == simulation::GroundAttachment::kGroundBound);
  }
  CHECK(original.with_ground_attachment(simulation::GroundAttachment::kFloating) ==
        stationary_body(20, 30));
  CHECK(simulation::body_has_baseline_physics(original));
  try {
    static_cast<void>(
        original.with_ground_attachment(static_cast<simulation::GroundAttachment>(2)));
    FAIL("undeclared ground attachment was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicsBodyGroundAttachmentOutOfRange);
    CHECK(error.context() == "physics_body.ground_attachment");
  }
}

TEST_CASE("PhysicsBody rejects a non-positive mass and an out-of-range restitution",
          "[unit][simulation][physics_body][validation]") {
  // Mass is strictly positive because the general impulse divides by it; restitution is a fraction
  // of the returned closing speed, so above one it manufactures energy and below zero it reverses
  // the impulse.
  const simulation::PhysicsBody baseline = stationary_body(1.0, 2.0);

  CHECK_THROWS_AS(baseline.with_mass(0.0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_mass(-1.0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_mass(std::numeric_limits<double>::quiet_NaN()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_mass(std::numeric_limits<double>::infinity()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_mass(simulation::kMaximumPhysicalComponentMagnitude * 2.0),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_restitution(-0.000'001), simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_restitution(1.000'001), simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_restitution(std::numeric_limits<double>::quiet_NaN()),
                  simulation::SimulationValidationError);
  // A negative drag scale makes the phase 1 factor exceed one, which adds speed on every tick with
  // nothing to bound it. There is deliberately no ceiling: a large scale only saturates the clamp
  // at zero, which is a body held still by drag rather than an error, and it is the same rule
  // `[simulation] drag_per_second` itself obeys.
  CHECK_THROWS_AS(baseline.with_drag_scale(-0.000'001), simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_drag_scale(std::numeric_limits<double>::quiet_NaN()),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(baseline.with_drag_scale(std::numeric_limits<double>::infinity()),
                  simulation::SimulationValidationError);
  CHECK(baseline.with_drag_scale(simulation::PhysicsBody::kMinimumDragScale).drag_scale() == 0.0);
  CHECK(baseline.with_drag_scale(simulation::kMaximumPhysicalComponentMagnitude).drag_scale() ==
        simulation::kMaximumPhysicalComponentMagnitude);

  // The closed interval and the positive mass are accepted at their boundaries.
  CHECK(baseline.with_restitution(simulation::PhysicsBody::kMinimumRestitution).restitution() ==
        0.0);
  CHECK(baseline.with_restitution(simulation::PhysicsBody::kMaximumRestitution).restitution() ==
        1.0);
  CHECK(baseline.with_mass(simulation::kMaximumPhysicalComponentMagnitude).mass() ==
        simulation::kMaximumPhysicalComponentMagnitude);

  // The eight-argument factory validates the same way, so no construction route is a back door.
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  CHECK_THROWS_AS(simulation::PhysicsBody::create(zero, zero, zero, 1.0, 0.0, 1U, 1U, false),
                  simulation::SimulationValidationError);
}

TEST_CASE("a static body may carry zero mass, which nothing divides by",
          "[unit][simulation][physics_body][validation]") {
  // The strict-positivity rule is a requirement of the general impulse, and the general impulse
  // requires a dynamic body on both sides. No equation reads a wall's mass, so zero there is the
  // same kind of structural absence a wall's zero velocity is -- and it is what the accepted
  // protocol v2 golden example publishes, against a schema that types mass as a non-negative
  // scalar. Making it a rejection would regenerate an accepted artifact to state an invariant
  // nothing needs.
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  const simulation::PhysicsBody wall = simulation::PhysicsBody::create(
      simulation::Vector2::create(480.0, 160.0), zero, zero, 40.0, 0.0, 2U, 1U, true);

  CHECK(wall.mass() == 0.0);
  CHECK(wall.is_static());
  // A negative mass is still rejected for either kind, and a static body that becomes dynamic is
  // rejected at that moment rather than silently carrying an impossible mass into the impulse.
  CHECK_THROWS_AS(simulation::PhysicsBody::create(zero, zero, zero, 1.0, -1.0, 1U, 1U, true),
                  simulation::SimulationValidationError);
}

TEST_CASE("a rejected mass, restitution, and drag scale each carry their own code",
          "[unit][simulation][physics_body][validation]") {
  try {
    static_cast<void>(stationary_body(1.0, 2.0).with_mass(0.0));
    FAIL("a zero mass was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicsBodyMassOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.PHYSICS_BODY_MASS_OUT_OF_RANGE"});
    CHECK(error.context() == "physics_body.mass");
  }
  try {
    static_cast<void>(stationary_body(1.0, 2.0).with_restitution(2.0));
    FAIL("a restitution above one was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicsBodyRestitutionOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.PHYSICS_BODY_RESTITUTION_OUT_OF_RANGE"});
    CHECK(error.context() == "physics_body.restitution");
  }
  try {
    static_cast<void>(stationary_body(1.0, 2.0).with_drag_scale(-1.0));
    FAIL("a negative drag scale was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kPhysicsBodyDragScaleOutOfRange);
    CHECK(error.code() == std::string_view{"SIMULATION.PHYSICS_BODY_DRAG_SCALE_OUT_OF_RANGE"});
    CHECK(error.context() == "physics_body.drag_scale");
  }
}

TEST_CASE("ControllerId and TeamId accept only the protocol-safe integer range",
          "[unit][simulation][controller_id][team_id]") {
  CHECK(simulation::ControllerId::create(simulation::kMinimumControllerId).value() ==
        simulation::kMinimumControllerId);
  CHECK(simulation::TeamId::create(simulation::kMaximumTeamId).value() ==
        simulation::kMaximumTeamId);
  CHECK(simulation::ControllerId::create(2) < simulation::ControllerId::create(3));
  CHECK_THROWS_AS(simulation::ControllerId::create(0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ControllerId::create(simulation::kMaximumControllerId + 1),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::TeamId::create(0), simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::TeamId::create(simulation::kMaximumTeamId + 1),
                  simulation::SimulationValidationError);
}

TEST_CASE("GameWorld canonicalizes seeded entities into one ascending roster",
          "[unit][simulation][game_world]") {
  const simulation::GameWorld world =
      simulation::GameWorld::create({seed(9, 9.0), seed(2, 2.0), seed(5, 5.0)});

  REQUIRE(world.entities().size() == 3);
  CHECK(world.entities()[0].value() == 2);
  CHECK(world.entities()[1].value() == 5);
  CHECK(world.entities()[2].value() == 9);
  CHECK(world.contains(simulation::EntityId::create(5)));
  CHECK_FALSE(world.contains(simulation::EntityId::create(6)));
}

TEST_CASE("GameWorld seeds every entity with a body and a controller link in ascending order",
          "[unit][simulation][game_world]") {
  const simulation::GameWorld world =
      simulation::GameWorld::create({seed(9, 9.0), seed(2, 2.0), seed(5, 5.0)});

  REQUIRE(world.store<simulation::PhysicsBody>().size() == 3);
  REQUIRE(world.store<simulation::Controllable>().size() == 3);
  CHECK(world.store<simulation::PhysicsBody>().entries()[0].entity ==
        simulation::EntityId::create(2));
  CHECK(world.store<simulation::PhysicsBody>().entries()[2].entity ==
        simulation::EntityId::create(9));
  REQUIRE(world.store<simulation::PhysicsBody>().find(simulation::EntityId::create(5)) != nullptr);
  CHECK(world.store<simulation::PhysicsBody>().find(simulation::EntityId::create(5))->position() ==
        simulation::Vector2::create(5.0, 0.0));
  CHECK(world.store<simulation::PhysicsBody>().find(simulation::EntityId::create(6)) == nullptr);
  REQUIRE(world.store<simulation::Controllable>().find(simulation::EntityId::create(5)) != nullptr);
  CHECK(world.store<simulation::Controllable>()
            .find(simulation::EntityId::create(5))
            ->controller_id.value() == 5);
}

TEST_CASE("GameWorld seeds an explicit ControllerId when one is supplied",
          "[unit][simulation][game_world]") {
  const simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(4), stationary_body(4.0, 0.0),
          simulation::ControllerId::create(77))});

  REQUIRE(world.store<simulation::Controllable>().find(simulation::EntityId::create(4)) != nullptr);
  CHECK(world.store<simulation::Controllable>()
            .find(simulation::EntityId::create(4))
            ->controller_id.value() == 77);
}

TEST_CASE("GameWorld derives its roster from the component stores, ascending and distinct",
          "[unit][simulation][game_world][entity_roster]") {
  // There is one answer to "which entities exist" and it is the stores: an entity exists exactly
  // while some registered store holds its id, so writing a component is what creates an entity and
  // no roster can drift from the components it is supposed to describe.
  simulation::GameWorld world = simulation::GameWorld::create({seed(5, 5.0)});
  world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(9),
                                                            simulation::Score{1});
  world.mutable_store<simulation::Team>().insert_or_assign(
      simulation::EntityId::create(2), simulation::Team{simulation::TeamId::create(4)});
  // The same entity in two stores appears once.
  world.mutable_store<simulation::Lifetime>().insert_or_assign(simulation::EntityId::create(9),
                                                               simulation::Lifetime{3});

  REQUIRE(world.entities().size() == 3);
  CHECK(world.entities()[0].value() == 2);
  CHECK(world.entities()[1].value() == 5);
  CHECK(world.entities()[2].value() == 9);
  CHECK(world.contains(simulation::EntityId::create(9)));
  CHECK_FALSE(world.contains(simulation::EntityId::create(404)));
  CHECK(world.store<simulation::PhysicsBody>().size() == 1);
}

TEST_CASE("GameWorld create_entity draws from this tick's reservation and fails when exhausted",
          "[unit][simulation][game_world][entity_id_reservation]") {
  // A world outside a tick holds the empty reservation, so nothing but a tick can create. The
  // kernel installs the tick's reservation; exhaustion is a hard failure and never a silent skip.
  simulation::GameWorld world = simulation::GameWorld::create({seed(5, 5.0)});

  CHECK_THROWS_AS(static_cast<void>(world.create_entity()), simulation::SimulationValidationError);
  CHECK(world.entity_id_reservation() == simulation::EntityIdReservation::none());
}

TEST_CASE("GameWorld rejects duplicate IDs and unsafe entity counts",
          "[unit][simulation][game_world]") {
  CHECK_THROWS_AS(simulation::GameWorld::create({seed(1, 0.0), seed(1, 1.0)}),
                  simulation::SimulationValidationError);

  std::vector<simulation::GameWorld::EntitySeed> too_many_seeds;
  too_many_seeds.reserve(simulation::kMaximumEntityCount + 1);
  for (std::size_t index = 0; index <= simulation::kMaximumEntityCount; ++index) {
    too_many_seeds.push_back(seed(static_cast<simulation::EntityId::Value>(index + 1), 0.0));
  }

  CHECK_THROWS_AS(simulation::GameWorld::create(std::move(too_many_seeds)),
                  simulation::SimulationValidationError);
}

TEST_CASE("GameWorld destroy_entity erases the roster seat and every registered store",
          "[unit][simulation][game_world][component_registry]") {
  simulation::GameWorld world =
      simulation::GameWorld::create({seed(2, 2.0), seed(5, 5.0), seed(9, 9.0)});
  world.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(5),
                                                            simulation::Score{11});
  world.mutable_store<simulation::Team>().insert_or_assign(
      simulation::EntityId::create(5), simulation::Team{simulation::TeamId::create(3)});
  world.mutable_store<simulation::Lifetime>().insert_or_assign(simulation::EntityId::create(5),
                                                               simulation::Lifetime{4});

  world.destroy_entity(simulation::EntityId::create(5));

  CHECK_FALSE(world.contains(simulation::EntityId::create(5)));
  CHECK(world.entities().size() == 2);
  CHECK(world.store<simulation::PhysicsBody>().find(simulation::EntityId::create(5)) == nullptr);
  CHECK(world.store<simulation::Controllable>().find(simulation::EntityId::create(5)) == nullptr);
  CHECK(world.store<simulation::Score>().find(simulation::EntityId::create(5)) == nullptr);
  CHECK(world.store<simulation::Team>().find(simulation::EntityId::create(5)) == nullptr);
  CHECK(world.store<simulation::Lifetime>().find(simulation::EntityId::create(5)) == nullptr);
  CHECK(world.store<simulation::PhysicsBody>().size() == 2);
}

TEST_CASE("GameWorld destroy_entity is total for an id no entity holds",
          "[unit][simulation][game_world]") {
  simulation::GameWorld world = simulation::GameWorld::create({seed(2, 2.0)});
  const simulation::GameWorld unchanged = world;

  world.destroy_entity(simulation::EntityId::create(404));

  CHECK(world == unchanged);
}

TEST_CASE("GameWorld equality is generated over every registered component store",
          "[unit][simulation][game_world][component_registry]") {
  const simulation::GameWorld world = simulation::GameWorld::create({seed(2, 2.0), seed(5, 5.0)});
  simulation::GameWorld scored = world;
  scored.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(2),
                                                             simulation::Score{1});
  simulation::GameWorld teamed = world;
  teamed.mutable_store<simulation::Team>().insert_or_assign(
      simulation::EntityId::create(2), simulation::Team{simulation::TeamId::create(1)});
  simulation::GameWorld expired = world;
  expired.mutable_store<simulation::Lifetime>().insert_or_assign(simulation::EntityId::create(2),
                                                                 simulation::Lifetime{9});
  simulation::GameWorld recontrolled = world;
  recontrolled.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(2),
      simulation::Controllable{simulation::ControllerId::create(8)});
  simulation::GameWorld extra_entity = world;
  extra_entity.mutable_store<simulation::Score>().insert_or_assign(simulation::EntityId::create(7),
                                                                   simulation::Score{0});

  CHECK(world == simulation::GameWorld::create({seed(5, 5.0), seed(2, 2.0)}));
  CHECK(world != scored);
  CHECK(world != teamed);
  CHECK(world != expired);
  CHECK(world != recontrolled);
  CHECK(world != extra_entity);
}

TEST_CASE("GameWorld appends events in production order and publishes them unchanged",
          "[unit][simulation][game_world][world_event]") {
  simulation::GameWorld world = simulation::GameWorld::create({seed(2, 2.0), seed(5, 5.0)});

  world.emit(simulation::EliminationEvent{simulation::EntityId::create(2)});
  world.emit(simulation::EliminationEvent{simulation::EntityId::create(5)});
  world.emit(simulation::DespawnEvent{simulation::EntityId::create(2)});

  REQUIRE(world.events().size() == 3);
  CHECK(simulation::world_event_kind_of(world.events()[0]) ==
        simulation::WorldEventKind::kElimination);
  CHECK(simulation::world_event_kind_of(world.events()[1]) ==
        simulation::WorldEventKind::kElimination);
  CHECK(simulation::world_event_kind_of(world.events()[2]) == simulation::WorldEventKind::kDespawn);
  CHECK(world.events()[1] ==
        simulation::WorldEvent{simulation::EliminationEvent{simulation::EntityId::create(5)}});
}

TEST_CASE("GameWorld starts every world with an empty event list",
          "[unit][simulation][game_world][world_event]") {
  // Events are tick-local: a world that has committed nothing has produced nothing, and the kernel
  // clears the list at every commit, so a seeded world and a committed world look alike here.
  const simulation::GameWorld empty_world = simulation::GameWorld::create({});
  const simulation::GameWorld seeded_world = simulation::GameWorld::create({seed(1, 1.0)});

  CHECK(empty_world.events().empty());
  CHECK(seeded_world.events().empty());
}

TEST_CASE("GameWorld rejects an event past the accepted per-tick limit",
          "[unit][simulation][game_world][world_event][validation]") {
  // Overflow is a hard simulation failure, never a silent drop: a dropped event would convert a
  // failure into a differently wrong tick.
  simulation::GameWorld world = simulation::GameWorld::create({seed(1, 1.0)});
  for (std::size_t index = 0; index < simulation::kMaximumWorldEventCount; ++index) {
    world.emit(simulation::EliminationEvent{simulation::EntityId::create(1)});
  }
  REQUIRE(world.events().size() == simulation::kMaximumWorldEventCount);

  CHECK_THROWS_AS(world.emit(simulation::DespawnEvent{simulation::EntityId::create(1)}),
                  simulation::SimulationValidationError);
  CHECK(world.events().size() == simulation::kMaximumWorldEventCount);
}

TEST_CASE("GameWorld equality distinguishes a pending event list",
          "[unit][simulation][game_world][world_event]") {
  const simulation::GameWorld world = simulation::GameWorld::create({seed(2, 2.0)});
  simulation::GameWorld pending = world;
  pending.emit(simulation::EliminationEvent{simulation::EntityId::create(2)});

  CHECK(world != pending);
}

TEST_CASE("GameWorld seats a map's static bodies with the world's own id policy",
          "[unit][simulation][game_world][map_definition]") {
  // The id policy for map content is owned by the world: `kMinimumEntityId + index` in the map's
  // declared order, so a map's entities are a deterministic function of the map file alone and no
  // caller can choose them. A wall carries a PhysicsBody and nothing else, which is what keeps it
  // out of the protocol v1 player projection.
  std::vector<simulation::StaticBodyDeclaration> static_bodies;
  static_bodies.push_back(simulation::StaticBodyDeclaration::create(
      simulation::PhysicsBody::create_static(simulation::Vector2::create(10.0, 20.0)),
      simulation::ContactEffectPolicy::kClosingImpact));
  static_bodies.push_back(simulation::StaticBodyDeclaration::create(
      simulation::PhysicsBody::create_static(simulation::Vector2::create(30.0, 40.0)),
      simulation::ContactEffectPolicy::kClosingImpact));
  const simulation::MapDefinition map = simulation::MapDefinition::create(
      "walled_map", simulation::ArenaBounds::create(100.0, 100.0), std::move(static_bodies), {},
      simulation::MapMetadata::none());

  const simulation::GameWorld world = simulation::GameWorld::create(configuration(), map, 4'242);

  REQUIRE(world.entities().size() == 2);
  CHECK(world.entities()[0].value() == simulation::kMinimumEntityId);
  CHECK(world.entities()[1].value() == simulation::kMinimumEntityId + 1);
  REQUIRE(world.store<simulation::PhysicsBody>().size() == 2);
  CHECK(world.store<simulation::PhysicsBody>().entries()[0].value.position() ==
        simulation::Vector2::create(10.0, 20.0));
  CHECK(world.store<simulation::Controllable>().empty());
  CHECK(world.random(simulation::RandomStreamKind::kHazards).seed() == 4'242);
  CHECK(world.random_draw_counts() == simulation::RandomDrawCounts{});
}

TEST_CASE("GameWorld seats a map's static bodies with the configured player radius",
          "[unit][simulation][game_world][map_definition][physics_body]") {
  // A map declares no size -- no accepted phase reads `PhysicsBody::radius()` and every one of them
  // measures with `SimulationConfig::player_radius()` -- so seating fills in the radius the kernel
  // actually measures with. `physics-body-component.schema.json` requires a positive radius, and a
  // seated `PhysicsBody::kUndeclaredRadius` made every live match unencodable.
  std::vector<simulation::StaticBodyDeclaration> static_bodies;
  static_bodies.push_back(simulation::StaticBodyDeclaration::create(
      simulation::PhysicsBody::create_static(simulation::Vector2::create(10.0, 20.0)),
      simulation::ContactEffectPolicy::kClosingImpact));
  const simulation::MapDefinition map = simulation::MapDefinition::create(
      "radius_map", simulation::ArenaBounds::create(100.0, 100.0), std::move(static_bodies), {},
      simulation::MapMetadata::none());
  REQUIRE(map.static_bodies()[0].body().radius() == simulation::PhysicsBody::kUndeclaredRadius);

  const simulation::GameWorld world = simulation::GameWorld::create(configuration(), map, 0);

  REQUIRE(world.store<simulation::PhysicsBody>().size() == 1);
  CHECK(world.store<simulation::PhysicsBody>().entries()[0].value.radius() ==
        configuration().player_radius());
  // Nothing else about the declared body changes.
  CHECK(world.store<simulation::PhysicsBody>().entries()[0].value.is_static());
  CHECK(world.store<simulation::PhysicsBody>().entries()[0].value.position() ==
        simulation::Vector2::create(10.0, 20.0));
}

TEST_CASE("GameWorld rejects a map whose spawn point cannot seat the configured disc",
          "[unit][simulation][game_world][map_definition][validation]") {
  // A spawn point is content and a radius is configuration. Checking them together at construction
  // turns a mid-match bounds failure into a startup rejection with a named cause.
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.push_back(
      simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(2.0, 50.0)));
  const simulation::MapDefinition map = simulation::MapDefinition::create(
      "edge_spawn_map", simulation::ArenaBounds::create(100.0, 100.0), {}, std::move(markers),
      simulation::MapMetadata::none());

  CHECK_THROWS_AS(simulation::GameWorld::create(configuration(), map, 0),
                  simulation::SimulationValidationError);
}

TEST_CASE("a seeded GameWorld carries an unusable reservation and a zero-seeded generator",
          "[unit][simulation][game_world][entity_id_reservation][deterministic_random]") {
  const simulation::GameWorld world = simulation::GameWorld::create({seed(2, 2.0)});

  CHECK(world.entity_id_reservation().empty());
  CHECK(world.random(simulation::RandomStreamKind::kHazards).seed() == 0);
  CHECK(world.match().phase == simulation::MatchPhase::kLobby);
}
