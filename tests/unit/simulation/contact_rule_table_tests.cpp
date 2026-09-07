#include "candidate_pair.hpp"
#include "contact_rule.hpp"
#include "contact_rule_table.hpp"
#include "entity_id.hpp"
#include "events/contact_event.hpp"
#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_test_fixture.hpp"
#include "simulation_validation_error.hpp"
#include "spatial_grid.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr double kPlayerRadius = 10.0;

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::EntityId entity(const simulation::EntityId::Value value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(
      500.0, 500.0, kPlayerRadius, simulation::SimulationConfig::kRequiredTicksPerSecond, 10, 10);
}

[[nodiscard]] simulation::MapDefinition map() {
  return simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(500.0, 500.0));
}

[[nodiscard]] simulation::PhysicsBody
moving_body(const double x, const double y, const double velocity_x, const double velocity_y) {
  return simulation::PhysicsBody::create(point(x, y), point(velocity_x, velocity_y),
                                         point(0.0, 0.0));
}

// A dynamic body that is not the baseline: it declares its own mass and restitution, which is
// precisely the predicate `variable_impulse` is written against.
[[nodiscard]] simulation::PhysicsBody variable_body(const double x, const double y,
                                                    const double velocity_x,
                                                    const double velocity_y, const double mass,
                                                    const double restitution) {
  return moving_body(x, y, velocity_x, velocity_y).with_mass(mass).with_restitution(restitution);
}

[[nodiscard]] simulation::GameWorld world_of(std::vector<simulation::GameWorld::EntitySeed> seeds) {
  return simulation::GameWorld::create(std::move(seeds));
}

// A world holding one dynamic body at id 1 and one static body at id 2, which is the arrangement
// every orientation test needs. A wall is seated by writing the one component it carries, because
// the roster is derived from the stores and a wall is driven by nobody.
[[nodiscard]] simulation::GameWorld dynamic_then_static_world() {
  simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 1.0, 0.0))});
  testing::seat_static_body(world, entity(2),
                            simulation::PhysicsBody::create_static(point(105.0, 100.0)));
  return world;
}

[[nodiscard]] simulation::GameWorld static_then_dynamic_world() {
  simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(2), moving_body(105.0, 100.0, -1.0, 0.0))});
  testing::seat_static_body(world, entity(1),
                            simulation::PhysicsBody::create_static(point(90.0, 100.0)));
  return world;
}

// A context is what a response reads for the configured common radius. Both the map and the index
// it carries are match-owned values, so the test owns them for the whole assertion.
class ContextFixture final {
public:
  explicit ContextFixture(const simulation::GameWorld& world)
      : map_(map()), grid_(simulation::SpatialGrid::create(configuration(), map_.bounds(), world)),
        context_(simulation::TickContext::create(simulation::TickSequence::create(7),
                                                 simulation::FixedDelta::canonical(),
                                                 configuration(), map_, grid_)) {}

  // The context references the map and the index this fixture owns, so the fixture is pinned.
  ContextFixture(const ContextFixture&) = delete;
  ContextFixture(ContextFixture&&) = delete;
  ContextFixture& operator=(const ContextFixture&) = delete;
  ContextFixture& operator=(ContextFixture&&) = delete;
  ~ContextFixture() = default;

  [[nodiscard]] const simulation::TickContext& context() const noexcept { return context_; }

private:
  simulation::MapDefinition map_;
  simulation::SpatialGrid grid_;
  simulation::TickContext context_;
};

// Named test rows. Each is a free function pointer exactly like a real row, which is what makes
// the purity of the seam structural rather than a convention the test could break.
[[nodiscard]] bool always(const simulation::GameWorld&, const simulation::EntityId) { return true; }

[[nodiscard]] bool never(const simulation::GameWorld&, const simulation::EntityId) { return false; }

[[nodiscard]] bool is_entity_one(const simulation::GameWorld&, const simulation::EntityId probed) {
  return probed == entity(1);
}

[[nodiscard]] bool is_entity_two(const simulation::GameWorld&, const simulation::EntityId probed) {
  return probed == entity(2);
}

[[nodiscard]] simulation::ContactResponse
declining_response(const simulation::ContactRule::Subject&, const simulation::ContactRule::Subject&,
                   const simulation::PlayerPairContact&, const simulation::TickContext&) {
  return simulation::ContactResponse::unchanged();
}

[[nodiscard]] const simulation::ContactEvent&
only_contact_event(const simulation::ContactResponse& response) {
  REQUIRE(response.events().size() == 1);
  const auto* contact_event = std::get_if<simulation::ContactEvent>(&response.events()[0]);
  REQUIRE(contact_event != nullptr);
  return *contact_event;
}

} // namespace

TEST_CASE("the built-in table declares variable_impulse, then elastic_disc, then reflect_static",
          "[unit][simulation][contact_rule_table]") {
  // Row order is the declared precedence and ADR 0003 names the last two, so it is asserted rather
  // than assumed. `variable_impulse` is above both on purpose: it matches only a pair the accepted
  // equal-unit-mass equation is not written for, and declaring it below `elastic_disc` would make
  // it unreachable, because `elastic_disc` matches every dynamic pair including a variable one.
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  REQUIRE(table.size() == 3);
  CHECK(table.rows()[0].name() == simulation::kVariableImpulseContactRuleName);
  CHECK(table.rows()[1].name() == simulation::kElasticDiscContactRuleName);
  CHECK(table.rows()[2].name() == simulation::kReflectStaticContactRuleName);
}

TEST_CASE("the built-in predicates are total over an entity carrying no body",
          "[unit][simulation][contact_rule_table]") {
  const simulation::GameWorld world = dynamic_then_static_world();

  CHECK(simulation::body_is_dynamic(world, entity(1)));
  CHECK_FALSE(simulation::body_is_static(world, entity(1)));
  CHECK(simulation::body_is_static(world, entity(2)));
  CHECK_FALSE(simulation::body_is_dynamic(world, entity(2)));
  CHECK_FALSE(simulation::body_is_dynamic(world, entity(99)));
  CHECK_FALSE(simulation::body_is_static(world, entity(99)));
}

TEST_CASE("a dynamic pair matches elastic_disc in the canonical orientation",
          "[unit][simulation][contact_rule_table][orientation]") {
  const simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 1.0, 0.0)),
       simulation::GameWorld::EntitySeed::create(entity(2), moving_body(105.0, 100.0, -1.0, 0.0))});
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == simulation::kElasticDiscContactRuleName);
  CHECK(match->orientation == simulation::ContactOrientation::kCanonical);
}

TEST_CASE("a dynamic-then-static pair matches reflect_static in the canonical orientation",
          "[unit][simulation][contact_rule_table][orientation]") {
  const simulation::GameWorld world = dynamic_then_static_world();
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == simulation::kReflectStaticContactRuleName);
  CHECK(match->orientation == simulation::ContactOrientation::kCanonical);
}

TEST_CASE("a static-then-dynamic pair matches reflect_static in the swapped orientation",
          "[unit][simulation][contact_rule_table][orientation]") {
  // Canonical pairs are `(lower id, higher id)` and a row declares its own argument order, so the
  // swapped orientation is how one row covers both arrangements of one interaction.
  const simulation::GameWorld world = static_then_dynamic_world();
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == simulation::kReflectStaticContactRuleName);
  CHECK(match->orientation == simulation::ContactOrientation::kSwapped);
}

TEST_CASE("a static pair matches no built-in row and is therefore unchanged",
          "[unit][simulation][contact_rule_table][orientation]") {
  // The table is total without a default row: an unmatched pair is defined, not an error.
  simulation::GameWorld world = world_of({});
  testing::seat_static_body(world, entity(1),
                            simulation::PhysicsBody::create_static(point(90.0, 100.0)));
  testing::seat_static_body(world, entity(2),
                            simulation::PhysicsBody::create_static(point(105.0, 100.0)));

  CHECK_FALSE(simulation::ContactRuleTable::built_in()
                  .first_match(world, entity(1), entity(2))
                  .has_value());
}

TEST_CASE("first match wins in declared row order",
          "[unit][simulation][contact_rule_table][precedence]") {
  // Two rows both match this pair. Precedence is the declared order and nothing else, so reversing
  // the declaration reverses the winner with no other change.
  const simulation::GameWorld world = dynamic_then_static_world();
  const simulation::ContactRule earlier =
      simulation::ContactRule::create("earlier", always, always, declining_response);
  const simulation::ContactRule later =
      simulation::ContactRule::create("later", always, always, declining_response);

  const simulation::ContactRuleTable earlier_first =
      simulation::ContactRuleTable::create({earlier, later});
  const simulation::ContactRuleTable later_first =
      simulation::ContactRuleTable::create({later, earlier});

  REQUIRE(earlier_first.first_match(world, entity(1), entity(2)).has_value());
  CHECK(earlier_first.rows()[earlier_first.first_match(world, entity(1), entity(2))->row_index]
            .name() == "earlier");
  REQUIRE(later_first.first_match(world, entity(1), entity(2)).has_value());
  CHECK(
      later_first.rows()[later_first.first_match(world, entity(1), entity(2))->row_index].name() ==
      "later");
}

TEST_CASE("the canonical orientation of a row is tried before its swapped orientation",
          "[unit][simulation][contact_rule_table][orientation]") {
  // One row that matches both ways must report canonical. Trying swapped first would silently
  // reverse which subject a response receives as `first`.
  const simulation::GameWorld world = dynamic_then_static_world();
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::create(
      {simulation::ContactRule::create("both_ways", always, always, declining_response)});

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(match->orientation == simulation::ContactOrientation::kCanonical);
}

TEST_CASE("an earlier row's swapped orientation outranks a later row's canonical one",
          "[unit][simulation][contact_rule_table][orientation][precedence]") {
  // Orientation is tried inside a row, not across the table: the walk is row-major, so row 0 in
  // either orientation beats row 1 in any orientation.
  const simulation::GameWorld world = dynamic_then_static_world();
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::create(
      {simulation::ContactRule::create("swapped_only", is_entity_two, is_entity_one,
                                       declining_response),
       simulation::ContactRule::create("canonical_only", is_entity_one, is_entity_two,
                                       declining_response)});

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(match->row_index == 0);
  CHECK(match->orientation == simulation::ContactOrientation::kSwapped);
}

TEST_CASE("an empty table matches nothing at all", "[unit][simulation][contact_rule_table]") {
  const simulation::GameWorld world = dynamic_then_static_world();

  CHECK(simulation::ContactRuleTable::empty().size() == 0);
  CHECK_FALSE(
      simulation::ContactRuleTable::empty().first_match(world, entity(1), entity(2)).has_value());
}

TEST_CASE("a row that matches neither orientation is skipped",
          "[unit][simulation][contact_rule_table]") {
  const simulation::GameWorld world = dynamic_then_static_world();
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::create(
      {simulation::ContactRule::create("never_matches", never, always, declining_response),
       simulation::ContactRule::create("always_matches", always, always, declining_response)});

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(match->row_index == 1);
}

TEST_CASE("ContactRule rejects a null predicate, a null response, and an unusable name",
          "[unit][simulation][contact_rule][validation]") {
  CHECK_THROWS_AS(simulation::ContactRule::create("row", nullptr, always, declining_response),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ContactRule::create("row", always, nullptr, declining_response),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ContactRule::create("row", always, always, nullptr),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ContactRule::create("", always, always, declining_response),
                  simulation::SimulationValidationError);
  CHECK_THROWS_AS(simulation::ContactRule::create("Row", always, always, declining_response),
                  simulation::SimulationValidationError);
}

TEST_CASE("ContactRuleTable rejects a duplicate row name",
          "[unit][simulation][contact_rule_table][validation]") {
  // A duplicate name would make a ContactEvent's `rule_name` ambiguous for the system consuming
  // it, so the table names one colliding row at construction instead.
  CHECK_THROWS_AS(simulation::ContactRuleTable::create(
                      {simulation::ContactRule::create("row", always, always, declining_response),
                       simulation::ContactRule::create("row", is_entity_one, is_entity_two,
                                                       declining_response)}),
                  simulation::SimulationValidationError);
}

TEST_CASE("an unchanged response names no body", "[unit][simulation][contact_rule]") {
  const simulation::ContactResponse response = simulation::ContactResponse::unchanged();

  CHECK_FALSE(response.replaces_bodies());
  CHECK(response.events().empty());
  CHECK_THROWS_AS(response.first_body(), simulation::SimulationValidationError);
  CHECK_THROWS_AS(response.second_body(), simulation::SimulationValidationError);
}

TEST_CASE("the collision admission predicate is symmetric and defaults to admitting",
          "[unit][simulation][physics_body][collision_admission]") {
  // Every baseline body carries the single default layer and mask, so the accepted fixtures admit
  // exactly the pairs they always did.
  const simulation::PhysicsBody baseline = moving_body(0.0, 0.0, 0.0, 0.0);
  CHECK(simulation::collision_masks_admit(baseline, baseline));

  const simulation::PhysicsBody players = simulation::PhysicsBody::create(
      point(0.0, 0.0), point(0.0, 0.0), point(0.0, 0.0), 0.0, 1.0, 0b01U, 0b01U, false);
  const simulation::PhysicsBody scenery = simulation::PhysicsBody::create(
      point(0.0, 0.0), point(0.0, 0.0), point(0.0, 0.0), 0.0, 1.0, 0b10U, 0b10U, true);
  // Disjoint layers: neither side names the other, so the pair is not admitted either way round.
  CHECK_FALSE(simulation::collision_masks_admit(players, scenery));
  CHECK_FALSE(simulation::collision_masks_admit(scenery, players));

  // One-sided interest is not enough: both directions must be nonzero.
  const simulation::PhysicsBody one_way_watcher = simulation::PhysicsBody::create(
      point(0.0, 0.0), point(0.0, 0.0), point(0.0, 0.0), 0.0, 1.0, 0b01U, 0b11U, false);
  CHECK_FALSE(simulation::collision_masks_admit(one_way_watcher, scenery));
  CHECK_FALSE(simulation::collision_masks_admit(scenery, one_way_watcher));
}

TEST_CASE("elastic_disc reproduces resolve_player_pair_collision exactly",
          "[unit][simulation][contact_rule_table][built_in][physics]") {
  // The row selects the accepted equation rather than transcribing it, and this is the assertion
  // that says so: bit-exact equality against the pure function the accepted narrow phase calls.
  const simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 3.0, 1.0)),
       simulation::GameWorld::EntitySeed::create(entity(2), moving_body(105.0, 100.0, -2.0, 0.5))});
  const ContextFixture fixture(world);
  const simulation::PhysicsBody first_body = moving_body(90.0, 100.0, 3.0, 1.0);
  const simulation::PhysicsBody second_body = moving_body(105.0, 100.0, -2.0, 0.5);
  const simulation::PlayerPairContact contact =
      simulation::detect_player_pair_contact(first_body, second_body, kPlayerRadius);
  const simulation::PlayerPairCollisionResult expected =
      simulation::resolve_player_pair_collision(first_body, second_body, kPlayerRadius);
  REQUIRE(expected.impulse_applied());

  const simulation::ContactResponse response = simulation::elastic_disc_response(
      simulation::ContactRule::Subject{entity(1), first_body},
      simulation::ContactRule::Subject{entity(2), second_body}, contact, fixture.context());

  REQUIRE(response.replaces_bodies());
  CHECK(response.first_body().velocity() == expected.first_velocity());
  CHECK(response.second_body().velocity() == expected.second_velocity());
  CHECK(response.first_body().position() == first_body.position());
  CHECK(response.second_body().position() == second_body.position());
}

TEST_CASE("elastic_disc publishes one canonical contact event",
          "[unit][simulation][contact_rule_table][built_in][world_event]") {
  const simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 1.0, 0.0)),
       simulation::GameWorld::EntitySeed::create(entity(2), moving_body(105.0, 100.0, -1.0, 0.0))});
  const ContextFixture fixture(world);
  const simulation::PhysicsBody first_body = moving_body(90.0, 100.0, 1.0, 0.0);
  const simulation::PhysicsBody second_body = moving_body(105.0, 100.0, -1.0, 0.0);
  const simulation::PlayerPairContact contact =
      simulation::detect_player_pair_contact(first_body, second_body, kPlayerRadius);

  const simulation::ContactResponse response = simulation::elastic_disc_response(
      simulation::ContactRule::Subject{entity(1), first_body},
      simulation::ContactRule::Subject{entity(2), second_body}, contact, fixture.context());

  const simulation::ContactEvent& event = only_contact_event(response);
  CHECK(event.pair == simulation::CandidatePair::create(entity(1), entity(2)));
  CHECK(event.normal == point(1.0, 0.0));
  CHECK(event.relative_normal_speed == contact.relative_normal_speed());
  CHECK(event.rule_name == simulation::kElasticDiscContactRuleName);
}

TEST_CASE("reflect_static reflects the normal component and leaves the static body untouched",
          "[unit][simulation][contact_rule_table][built_in][physics]") {
  // ADR 0003 § "Wall policy" applied to a body: speed magnitude on the contact normal is
  // preserved, the tangential component stays attached to the moving body, and the wall does not
  // move. The normal here is exactly `(1, 0)`, so the reflection is a sign flip on x.
  const simulation::GameWorld world = dynamic_then_static_world();
  const ContextFixture fixture(world);
  const simulation::PhysicsBody dynamic_body = moving_body(90.0, 100.0, 3.0, 1.5);
  const simulation::PhysicsBody static_body =
      simulation::PhysicsBody::create_static(point(105.0, 100.0));
  const simulation::PlayerPairContact contact =
      simulation::detect_player_pair_contact(dynamic_body, static_body, kPlayerRadius);
  REQUIRE(contact.is_contact());
  REQUIRE(contact.normal() == point(1.0, 0.0));

  const simulation::ContactResponse response = simulation::reflect_static_response(
      simulation::ContactRule::Subject{entity(1), dynamic_body},
      simulation::ContactRule::Subject{entity(2), static_body}, contact, fixture.context());

  REQUIRE(response.replaces_bodies());
  CHECK(response.first_body().velocity() == point(-3.0, 1.5));
  CHECK(response.first_body().position() == dynamic_body.position());
  CHECK(response.second_body() == static_body);
}

TEST_CASE("reflect_static preserves speed on an oblique contact normal",
          "[unit][simulation][contact_rule_table][built_in][physics]") {
  // A 45-degree normal exercises the general reflection rather than the axis-aligned special case.
  const simulation::GameWorld world = dynamic_then_static_world();
  const ContextFixture fixture(world);
  const simulation::PhysicsBody dynamic_body = moving_body(100.0, 100.0, 2.0, 0.0);
  const simulation::PhysicsBody static_body =
      simulation::PhysicsBody::create_static(point(112.0, 112.0));
  const simulation::PlayerPairContact contact =
      simulation::detect_player_pair_contact(dynamic_body, static_body, kPlayerRadius);
  REQUIRE(contact.is_contact());

  const simulation::ContactResponse response = simulation::reflect_static_response(
      simulation::ContactRule::Subject{entity(1), dynamic_body},
      simulation::ContactRule::Subject{entity(2), static_body}, contact, fixture.context());

  REQUIRE(response.replaces_bodies());
  const simulation::Vector2& reflected = response.first_body().velocity();
  // Speed is preserved because reflection only changes the sign of the normal component.
  CHECK(reflected.magnitude() ==
        Catch::Approx(dynamic_body.velocity().magnitude()).margin(simulation::kVelocityTolerance));
  // The normal component reversed sign and the tangential component did not move.
  const double before_normal = dynamic_body.velocity().dot(contact.normal());
  const double after_normal = reflected.dot(contact.normal());
  CHECK(after_normal == Catch::Approx(-before_normal).margin(simulation::kVelocityTolerance));
  CHECK(response.second_body() == static_body);
}

TEST_CASE("a swapped match still publishes the canonical pair and normal",
          "[unit][simulation][contact_rule_table][orientation][world_event]") {
  // The row is written once, in row orientation, and the event it publishes is expressed in the
  // canonical `(lower -> higher)` sense whichever way the row matched. That is what lets a
  // consuming system read one convention.
  const simulation::GameWorld world = static_then_dynamic_world();
  const ContextFixture fixture(world);
  const simulation::PhysicsBody static_body =
      simulation::PhysicsBody::create_static(point(90.0, 100.0));
  const simulation::PhysicsBody dynamic_body = moving_body(105.0, 100.0, -3.0, 0.0);
  // Row orientation is (dynamic, static), which is the higher id first here.
  const simulation::PlayerPairContact row_contact =
      simulation::detect_player_pair_contact(dynamic_body, static_body, kPlayerRadius);
  REQUIRE(row_contact.normal() == point(-1.0, 0.0));

  const simulation::ContactResponse response = simulation::reflect_static_response(
      simulation::ContactRule::Subject{entity(2), dynamic_body},
      simulation::ContactRule::Subject{entity(1), static_body}, row_contact, fixture.context());

  REQUIRE(response.replaces_bodies());
  CHECK(response.first_body().velocity() == point(3.0, 0.0));
  const simulation::ContactEvent& event = only_contact_event(response);
  CHECK(event.pair == simulation::CandidatePair::create(entity(1), entity(2)));
  // Row orientation was (2 -> 1), so the canonical (1 -> 2) normal is its negation.
  CHECK(event.normal == point(1.0, 0.0));
  CHECK(event.rule_name == simulation::kReflectStaticContactRuleName);
}

TEST_CASE("body_is_variable_dynamic names exactly the bodies the baseline equation does not cover",
          "[unit][simulation][contact_rule_table][variable_impulse]") {
  // Total in the same way as the other two predicates, and false for a static body: a wall meeting
  // a hazard is still `reflect_static`, whatever mass the hazard declares.
  simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 1.0, 0.0)),
       simulation::GameWorld::EntitySeed::create(entity(2),
                                                 variable_body(105.0, 100.0, -1.0, 0.0, 40.0, 1.0)),
       simulation::GameWorld::EntitySeed::create(entity(3),
                                                 variable_body(200.0, 100.0, 0.0, 0.0, 1.0, 0.0))});
  testing::seat_static_body(world, entity(4),
                            simulation::PhysicsBody::create_static(point(300.0, 100.0)));

  CHECK_FALSE(simulation::body_is_variable_dynamic(world, entity(1)));
  // Mass alone is enough, and so is restitution alone.
  CHECK(simulation::body_is_variable_dynamic(world, entity(2)));
  CHECK(simulation::body_is_variable_dynamic(world, entity(3)));
  CHECK_FALSE(simulation::body_is_variable_dynamic(world, entity(4)));
  CHECK_FALSE(simulation::body_is_variable_dynamic(world, entity(99)));
}

TEST_CASE("a pair of baseline blobs falls through variable_impulse to elastic_disc",
          "[unit][simulation][contact_rule_table][variable_impulse][precedence]") {
  // This is the assertion the accepted fixtures rest on. Every body in every accepted fixture is a
  // baseline body, so every accepted pair reaches `elastic_disc` and nothing else, and the general
  // equation -- which agrees with the baseline but is not bit-identical to it -- is never
  // evaluated.
  const simulation::GameWorld world = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 1.0, 0.0)),
       simulation::GameWorld::EntitySeed::create(entity(2), moving_body(105.0, 100.0, -1.0, 0.0))});
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == simulation::kElasticDiscContactRuleName);
}

TEST_CASE("a variable body outranks elastic_disc in whichever orientation it appears",
          "[unit][simulation][contact_rule_table][variable_impulse][orientation]") {
  // The row is `(variable dynamic, dynamic)`, so `(variable, baseline)` matches canonically and
  // `(baseline, variable)` matches swapped. One row therefore covers both arrangements without the
  // caller having to know which body declared the odd mass.
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  const simulation::GameWorld variable_first = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1),
                                                 variable_body(90.0, 100.0, 1.0, 0.0, 40.0, 1.0)),
       simulation::GameWorld::EntitySeed::create(entity(2), moving_body(105.0, 100.0, -1.0, 0.0))});
  const std::optional<simulation::ContactRuleTable::Match> canonical_match =
      table.first_match(variable_first, entity(1), entity(2));
  REQUIRE(canonical_match.has_value());
  CHECK(table.rows()[canonical_match->row_index].name() ==
        simulation::kVariableImpulseContactRuleName);
  CHECK(canonical_match->orientation == simulation::ContactOrientation::kCanonical);

  const simulation::GameWorld variable_second = world_of(
      {simulation::GameWorld::EntitySeed::create(entity(1), moving_body(90.0, 100.0, 1.0, 0.0)),
       simulation::GameWorld::EntitySeed::create(
           entity(2), variable_body(105.0, 100.0, -1.0, 0.0, 40.0, 1.0))});
  const std::optional<simulation::ContactRuleTable::Match> swapped_match =
      table.first_match(variable_second, entity(1), entity(2));
  REQUIRE(swapped_match.has_value());
  CHECK(table.rows()[swapped_match->row_index].name() ==
        simulation::kVariableImpulseContactRuleName);
  CHECK(swapped_match->orientation == simulation::ContactOrientation::kSwapped);
}

TEST_CASE("a variable body meeting a wall still matches reflect_static",
          "[unit][simulation][contact_rule_table][variable_impulse][orientation]") {
  // `variable_impulse` requires a dynamic body on both sides, so declaring an odd mass does not
  // change how a body meets a wall. Precedence is only about which equation two moving bodies use.
  simulation::GameWorld world = world_of({simulation::GameWorld::EntitySeed::create(
      entity(1), variable_body(90.0, 100.0, 1.0, 0.0, 40.0, 0.5))});
  testing::seat_static_body(world, entity(2),
                            simulation::PhysicsBody::create_static(point(105.0, 100.0)));
  const simulation::ContactRuleTable table = simulation::ContactRuleTable::built_in();

  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == simulation::kReflectStaticContactRuleName);
}

TEST_CASE("variable_impulse reproduces resolve_general_pair_collision exactly",
          "[unit][simulation][contact_rule_table][variable_impulse][physics]") {
  // As with `elastic_disc`, the row selects the equation rather than transcribing it, so this is
  // bit-exact equality against the pure function -- and it is a *different* pure function from the
  // accepted one, which is the whole point of the row.
  const simulation::PhysicsBody first_body = variable_body(90.0, 100.0, 3.0, 1.0, 40.0, 0.25);
  const simulation::PhysicsBody second_body = moving_body(105.0, 100.0, -2.0, 0.5);
  const simulation::GameWorld world =
      world_of({simulation::GameWorld::EntitySeed::create(entity(1), first_body),
                simulation::GameWorld::EntitySeed::create(entity(2), second_body)});
  const ContextFixture fixture(world);
  const simulation::PlayerPairContact contact =
      simulation::detect_player_pair_contact(first_body, second_body, kPlayerRadius);
  const simulation::PlayerPairCollisionResult expected =
      simulation::resolve_general_pair_collision(first_body, second_body, kPlayerRadius);
  REQUIRE(expected.impulse_applied());

  const simulation::ContactResponse response = simulation::variable_impulse_response(
      simulation::ContactRule::Subject{entity(1), first_body},
      simulation::ContactRule::Subject{entity(2), second_body}, contact, fixture.context());

  REQUIRE(response.replaces_bodies());
  CHECK(response.first_body().velocity() == expected.first_velocity());
  CHECK(response.second_body().velocity() == expected.second_velocity());
  // A response may change only the two bodies' velocities: the declared physics survives the row.
  CHECK(response.first_body().position() == first_body.position());
  CHECK(response.first_body().mass() == 40.0);
  CHECK(response.first_body().restitution() == 0.25);
  CHECK(response.second_body().position() == second_body.position());

  const simulation::ContactEvent& event = only_contact_event(response);
  CHECK(event.pair == simulation::CandidatePair::create(entity(1), entity(2)));
  CHECK(event.rule_name == simulation::kVariableImpulseContactRuleName);
}
