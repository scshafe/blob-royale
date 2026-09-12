#include "shared/lethal_hazard_contact_rule.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "contact_rule.hpp"
#include "contact_rule_table.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/contact_event.hpp"
#include "events/elimination_event.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_mode.hpp"
#include "royale/royale_mode_state.hpp"
#include "seat_roster.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] simulation::EntityId entity(const simulation::EntityId::Value value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

// A hazard as the spawner builds one: a dynamic body with its own mass and restitution, carrying
// the marker and no `Controllable`, because nobody drives a comet.
[[nodiscard]] simulation::PhysicsBody hazard_body(const double x, const double y) {
  return simulation::PhysicsBody::create(point(x, y), point(40.0, 0.0), point(0.0, 0.0), 26.0, 40.0,
                                         simulation::PhysicsBody::kDefaultCollisionLayer,
                                         simulation::PhysicsBody::kDefaultCollisionMask, false)
      .with_restitution(0.2)
      .with_bounds_behavior(simulation::BoundsBehavior::kCross);
}

[[nodiscard]] simulation::PhysicsBody player_body(const double x, const double y) {
  return simulation::PhysicsBody::create(point(x, y), point(0.0, 0.0), point(0.0, 0.0));
}

void seat_hazard(simulation::GameWorld& world, const simulation::EntityId id,
                 const simulation::PhysicsBody& body) {
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(id, body);
  world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
      id, simulation::LethalOnContact{});
}

// Every world below is a match in progress unless a test says otherwise: the row is lethal only
// while the committed phase is `running`, and a default-constructed `MatchState` is in `lobby`.
void start_running(simulation::GameWorld& world) {
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
}

// A player and a hazard, with the player at the lower id so the canonical pair is (player, hazard)
// and the row has to match in the swapped orientation to fire at all.
[[nodiscard]] simulation::GameWorld player_then_hazard_world() {
  simulation::GameWorld world =
      simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
          entity(1), player_body(100.0, 100.0), simulation::ControllerId::create(1))});
  seat_hazard(world, entity(2), hazard_body(130.0, 100.0));
  start_running(world);
  return world;
}

[[nodiscard]] simulation::ContactRuleTable royale_table() {
  return simulation::ContactRuleTable::with_rows_above_built_in(
      {gameplay::lethal_hazard_contact_rule()});
}

[[nodiscard]] std::size_t elimination_count(const std::vector<simulation::WorldEvent>& events) {
  std::size_t count = 0;
  for (const simulation::WorldEvent& event : events) {
    if (std::holds_alternative<simulation::EliminationEvent>(event)) {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST_CASE("lethal_hazard matches a hazard against a player in either orientation",
          "[unit][gameplay][shared][lethal_hazard]") {
  const simulation::GameWorld world = player_then_hazard_world();
  const simulation::ContactRuleTable table = royale_table();

  // The canonical pair is (1, 2) with the player first, so the hazard predicate matches the higher
  // id and the row fires swapped. Row 0 is the whole point: it is above every impulse row.
  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));
  REQUIRE(match.has_value());
  CHECK(match->row_index == 0);
  CHECK(match->orientation == simulation::ContactOrientation::kSwapped);
  CHECK(table.rows()[match->row_index].name() == gameplay::kLethalHazardContactRuleName);
}

TEST_CASE("lethal_hazard wins over the impulse rows that would also match the pair",
          "[unit][gameplay][shared][lethal_hazard]") {
  const simulation::GameWorld world = player_then_hazard_world();

  // Precedence is the whole mechanism, so this states the counterfactual rather than trusting it.
  // The same pair offered to the built-in table alone matches `variable_impulse`, because a hazard
  // is a dynamic body whose mass differs from the baseline. Declared below that row,
  // `lethal_hazard` would be unreachable and a comet would shove a player aside instead of killing
  // them.
  const simulation::ContactRuleTable built_in = simulation::ContactRuleTable::built_in();
  const std::optional<simulation::ContactRuleTable::Match> without_the_row =
      built_in.first_match(world, entity(1), entity(2));
  REQUIRE(without_the_row.has_value());
  CHECK(built_in.rows()[without_the_row->row_index].name() ==
        simulation::kVariableImpulseContactRuleName);

  const simulation::ContactRuleTable table = royale_table();
  const std::optional<simulation::ContactRuleTable::Match> with_the_row =
      table.first_match(world, entity(1), entity(2));
  REQUIRE(with_the_row.has_value());
  CHECK(table.rows()[with_the_row->row_index].name() == gameplay::kLethalHazardContactRuleName);
}

TEST_CASE("a hazard cannot eliminate another hazard", "[unit][gameplay][shared][lethal_hazard]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  start_running(world);
  seat_hazard(world, entity(1), hazard_body(100.0, 100.0));
  seat_hazard(world, entity(2), hazard_body(130.0, 100.0));

  const simulation::ContactRuleTable table = royale_table();
  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(world, entity(1), entity(2));

  // It matches something -- two dynamic non-baseline bodies are a `variable_impulse` pair -- but
  // never the lethal row, because neither side carries a `Controllable`. Two comets crossing bounce
  // off each other; neither is eliminated, and neither could be, since eliminating an entity with
  // no controller is a hard failure in `placement_recorder`.
  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == simulation::kVariableImpulseContactRuleName);
  CHECK_FALSE(gameplay::body_is_player_driven(world, entity(1)));
  CHECK_FALSE(gameplay::body_is_player_driven(world, entity(2)));
}

TEST_CASE("a hazard cannot eliminate a wall or the zone entity",
          "[unit][gameplay][shared][lethal_hazard]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  start_running(world);
  seat_hazard(world, entity(1), hazard_body(100.0, 100.0));
  // A wall: a body and no controller.
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(2), simulation::PhysicsBody::create_static(point(130.0, 100.0)));
  // The zone: neither a body nor a controller, which is why it is a pair the broad phase would
  // never offer and the predicates must still be total over.
  world.mutable_store<simulation::Zone>().insert_or_assign(
      entity(3), simulation::Zone{point(480.0, 320.0), 200.0});

  const simulation::ContactRuleTable table = royale_table();

  const std::optional<simulation::ContactRuleTable::Match> against_wall =
      table.first_match(world, entity(1), entity(2));
  REQUIRE(against_wall.has_value());
  CHECK(table.rows()[against_wall->row_index].name() == simulation::kReflectStaticContactRuleName);

  // Nothing matches a pair the zone is half of: it carries no PhysicsBody, so every predicate in
  // the table is false for it and the walk ends with no row rather than with a lookup failure.
  CHECK_FALSE(table.first_match(world, entity(1), entity(3)).has_value());
}

TEST_CASE("eligible lethal_hazard terminates the player and preserves both stored body values",
          "[unit][gameplay][shared][lethal_hazard]") {
  const simulation::GameWorld world = player_then_hazard_world();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};

  const simulation::PhysicsBody hazard = hazard_body(130.0, 100.0);
  const simulation::PhysicsBody player = player_body(100.0, 100.0);
  const simulation::ContactRule::Subject hazard_subject{entity(2), hazard};
  const simulation::ContactRule::Subject player_subject{entity(1), player};
  // The real detector rather than a hand-made contact value: the pair genuinely overlaps at
  // `r_hazard + r_player`, which is the distance the kernel measures a variable pair at.
  const simulation::PlayerPairContact contact = simulation::detect_pair_contact(
      hazard, player, simulation::pair_contact_distance(hazard, player, 10.0));

  const simulation::ContactResponse response =
      gameplay::lethal_hazard_response(world, hazard_subject, player_subject,
                                       {contact, std::nullopt, true, false}, harness.context());

  REQUIRE(response.replaces_bodies());
  // The hazard keeps travelling: a comet that staggered off what it killed would read as a bug to
  // anyone watching, and the player's velocity is about to stop existing.
  CHECK(response.first_body() == hazard);
  CHECK(response.second_body() == player);
  CHECK(response.first_result().disposition == simulation::MotionDisposition::kContinue);
  CHECK(response.second_result().disposition == simulation::MotionDisposition::kTerminate);

  const std::vector<simulation::WorldEvent> events{response.events().begin(),
                                                   response.events().end()};
  REQUIRE(events.size() == 2);
  REQUIRE(std::holds_alternative<simulation::EliminationEvent>(events[0]));
  // The *player* is named, never the hazard.
  CHECK(std::get<simulation::EliminationEvent>(events[0]).entity == entity(1));
  REQUIRE(std::holds_alternative<simulation::ContactEvent>(events[1]));
  CHECK(std::get<simulation::ContactEvent>(events[1]).rule_name.value() ==
        gameplay::kLethalHazardContactRuleName);
}

TEST_CASE("lethal_hazard eliminates during the zone grace period",
          "[unit][gameplay][shared][lethal_hazard]") {
  simulation::GameWorld world = player_then_hazard_world();
  // A player with the whole grace period still ahead of it: zero consecutive ticks outside, and a
  // zone it is comfortably inside. `zone_elimination` would not name this entity for many ticks.
  world.mutable_store<simulation::ZoneExposure>().insert_or_assign(entity(1),
                                                                   simulation::ZoneExposure{0});
  world.mutable_store<simulation::Zone>().insert_or_assign(
      entity(3), simulation::Zone{point(100.0, 100.0), 400.0});

  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  const simulation::PhysicsBody hazard = hazard_body(130.0, 100.0);
  const simulation::PhysicsBody player = player_body(100.0, 100.0);
  const simulation::ContactResponse response = gameplay::lethal_hazard_response(
      world, simulation::ContactRule::Subject{entity(2), hazard},
      simulation::ContactRule::Subject{entity(1), player},
      {simulation::detect_pair_contact(hazard, player,
                                       simulation::pair_contact_distance(hazard, player, 10.0)),
       std::nullopt, true, false},
      harness.context());

  // Lethality and the zone are two different rules with two different causes. A grace period on
  // "a comet hit you" would mean walking through a comet unharmed, so the count is one regardless
  // of how much grace the player has left.
  const std::vector<simulation::WorldEvent> events{response.events().begin(),
                                                   response.events().end()};
  CHECK(elimination_count(events) == 1);
  CHECK(world.store<simulation::ZoneExposure>().find(entity(1))->outside_ticks == 0);
}

TEST_CASE("lethal hazard eligibility belongs to the hazard source not the recipient player",
          "[unit][gameplay][shared][lethal_hazard][contact_effect_policy]") {
  const auto world = player_then_hazard_world();
  const testing::TickHarness harness{simulation::TickSequence::create(7)};
  const auto hazard = hazard_body(130, 100);
  const auto player = player_body(100, 100);
  const auto contact = simulation::detect_pair_contact(
      hazard, player, simulation::pair_contact_distance(hazard, player, 10));
  const auto declined =
      gameplay::lethal_hazard_response(world, {entity(2), hazard}, {entity(1), player},
                                       {contact, std::nullopt, false, true}, harness.context());
  CHECK_FALSE(declined.replaces_bodies());
  CHECK(declined.events().empty());
  const auto admitted =
      gameplay::lethal_hazard_response(world, {entity(2), hazard}, {entity(1), player},
                                       {contact, std::nullopt, true, false}, harness.context());
  REQUIRE(admitted.replaces_bodies());
  CHECK(admitted.first_result().disposition == simulation::MotionDisposition::kContinue);
  CHECK(admitted.second_result().disposition == simulation::MotionDisposition::kTerminate);
  CHECK(admitted.events().size() == 2);
}

TEST_CASE("lethal_hazard matches only while the match is running",
          "[unit][gameplay][shared][lethal_hazard]") {
  const simulation::ContactRuleTable table = royale_table();

  // The same pair, the same marker, and the same driver in every other phase resolves to the
  // impulse row: a comet in the lobby, in the countdown, or during the restart delay is a heavy
  // disc that shoves and nothing more, because a hazard outlives the phase it was spawned in by its
  // whole `Lifetime` and the recorder ranks whatever is emitted in any phase.
  for (const simulation::MatchPhase phase :
       {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
        simulation::MatchPhase::kEnded}) {
    INFO("phase " << static_cast<int>(phase));
    simulation::GameWorld world = player_then_hazard_world();
    world.mutable_match().phase = phase;
    CHECK_FALSE(gameplay::body_is_lethal_hazard(world, entity(2)));
    const std::optional<simulation::ContactRuleTable::Match> match =
        table.first_match(world, entity(1), entity(2));
    REQUIRE(match.has_value());
    CHECK(table.rows()[match->row_index].name() == simulation::kVariableImpulseContactRuleName);
  }

  const simulation::GameWorld running = player_then_hazard_world();
  CHECK(gameplay::body_is_lethal_hazard(running, entity(2)));
  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(running, entity(1), entity(2));
  REQUIRE(match.has_value());
  CHECK(table.rows()[match->row_index].name() == gameplay::kLethalHazardContactRuleName);
}

namespace {

// A royale match frozen in `phase`, holding one seated player and one lethal hazard that already
// overlaps it and is moving toward it, so the next kernel contact phase has to decide the pair
// whatever the phase is. Ids start at 10 so the tick's reservation at 100 is above every committed
// entity, and every tick field is zero so no phase duration elapses inside the one tick stepped.
[[nodiscard]] simulation::GameSimulation royale_match_in_phase(const simulation::MatchPhase phase) {
  const simulation::SimulationConfig configuration = testing::gameplay_configuration();
  const simulation::MapDefinition map = testing::gameplay_map(4);
  simulation::GameWorld world = simulation::GameWorld::create(configuration, map, 0);

  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(10),
                                                                  player_body(400.0, 320.0));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(10), simulation::Controllable{simulation::ControllerId::create(10)});
  seat_hazard(world, entity(11), hazard_body(430.0, 320.0).with_velocity(point(-40.0, 0.0)));

  simulation::MatchState& match = world.mutable_match();
  match.phase = phase;
  match.outcome = phase == simulation::MatchPhase::kEnded
                      ? simulation::MatchOutcome::won_by_entity(entity(10))
                      : simulation::MatchOutcome::undecided();
  // A one-seat lobby the player holds and has started, so `countdown` is a phase the machine stays
  // in; the request is absent in `lobby`, so nothing transitions there.
  match.seats = simulation::SeatRoster::of_size(1);
  if (phase == simulation::MatchPhase::kCountdown) {
    match.seats.assign_seat(
        0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(10)}});
    match.seats.request_start();
  }
  match.previous_phase = phase;

  return simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::of_mode(
          map, gameplay::RoyaleMode::create(gameplay::RoyaleConfiguration::defaults())));
}

[[nodiscard]] simulation::WorldSnapshot step_once(simulation::GameSimulation& game) {
  game.step(testing::kGameplayFixedDelta, testing::gameplay_batch(game, {}, 100, 8));
  return game.snapshot();
}

[[nodiscard]] std::size_t placement_count(const simulation::WorldSnapshot& snapshot) {
  const auto* held =
      std::get_if<simulation::RoyalePlacementsModeState>(&snapshot.match().mode_state());
  return held == nullptr ? 0 : held->placements.size();
}

} // namespace

TEST_CASE("a lethal hazard kills during running and only shoves in every other phase",
          "[unit][gameplay][shared][lethal_hazard]") {
  SECTION("running: the player is eliminated and ranked") {
    simulation::GameSimulation game = royale_match_in_phase(simulation::MatchPhase::kRunning);
    const simulation::WorldSnapshot after = step_once(game);
    CHECK_FALSE(testing::published_body(after, 10).has_value());
    CHECK(placement_count(after) == 1);
  }

  SECTION("ended: the committed winner survives the restart delay, shoved but alive") {
    simulation::GameSimulation game = royale_match_in_phase(simulation::MatchPhase::kEnded);
    const simulation::WorldSnapshot after = step_once(game);
    CHECK(after.match().phase() == simulation::MatchPhase::kEnded);
    CHECK(after.match().outcome() == simulation::MatchOutcome::won_by_entity(entity(10)));
    const std::optional<simulation::PhysicsBody> winner = testing::published_body(after, 10);
    REQUIRE(winner.has_value());
    // The impulse row resolved the pair: a forty-mass disc moving left pushed a unit-mass blob
    // left.
    CHECK(winner->velocity().x() < 0.0);
    CHECK(placement_count(after) == 0);
  }

  SECTION("lobby: a seated player is shoved and nothing is appended to any ranking") {
    simulation::GameSimulation game = royale_match_in_phase(simulation::MatchPhase::kLobby);
    const simulation::WorldSnapshot after = step_once(game);
    CHECK(after.match().phase() == simulation::MatchPhase::kLobby);
    const std::optional<simulation::PhysicsBody> player = testing::published_body(after, 10);
    REQUIRE(player.has_value());
    CHECK(player->velocity().x() < 0.0);
    CHECK(placement_count(after) == 0);
  }

  SECTION("countdown: the field stays complete and the countdown keeps running") {
    simulation::GameSimulation game = royale_match_in_phase(simulation::MatchPhase::kCountdown);
    const simulation::WorldSnapshot after = step_once(game);
    CHECK(after.match().phase() == simulation::MatchPhase::kCountdown);
    CHECK(testing::published_body(after, 10).has_value());
    CHECK(placement_count(after) == 0);
  }
}
