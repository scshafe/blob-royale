#include "shared/guarded_pair_contact_rule.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/shield_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "contact_rule.hpp"
#include "contact_rule_table.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/contact_event.hpp"
#include "events/elimination_event.hpp"
#include "events/stun_request_event.hpp"
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
#include "shared/guarded_pair_contact.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

// This file is `lethal_hazard_contact_rule_tests.cpp` retargeted. Plan Step 18 deleted that row and
// folded its behaviour into the one live `guarded_pair` adapter, so every proof it carried is kept
// here and re-asserted against the adapter rather than dropped. Two of them necessarily changed
// shape, and each says so where it is written: admission is no longer what discriminates a lethal
// pair from an ordinary one -- the row's predicates are a symmetric presence test, so *every* pair
// matches row zero and the discrimination moved into the response -- and a declined lethal effect
// is now a composed response with a `guarded_pair` diagnostic rather than `ContactResponse::
// unchanged()`.

namespace {

// Long enough that no test's shield lapses inside it, and distinct from every other tick constant
// below so a window comparison that read the wrong one would not accidentally still pass.
constexpr std::uint64_t kShieldDurationTicks = 160;
constexpr std::uint64_t kPerfectDurationTicks = 32;
constexpr std::uint64_t kCooldownDurationTicks = 360;

[[nodiscard]] simulation::EntityId entity(const simulation::EntityId::Value value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::TickSequence tick(const std::uint64_t value) {
  return simulation::TickSequence::create(value);
}

// The tick every response below is observed on, and the two activations that place a shield's
// windows around it: a shield raised on this tick is inside its perfect opening, and one raised
// four ticks earlier with a one-tick opening is past it and merely ordinary.
constexpr std::uint64_t kContactTick = 7;
constexpr std::uint64_t kEarlierActivationTick = 3;

[[nodiscard]] simulation::Shield perfect_shield(const std::uint64_t parry_stun_duration_ticks) {
  return simulation::Shield::activate(tick(kContactTick), kShieldDurationTicks,
                                      kPerfectDurationTicks, kCooldownDurationTicks,
                                      parry_stun_duration_ticks);
}

// Still protecting on `kContactTick`, but its one-tick opening closed four ticks ago.
[[nodiscard]] simulation::Shield ordinary_shield(const std::uint64_t parry_stun_duration_ticks) {
  return simulation::Shield::activate(tick(kEarlierActivationTick), kShieldDurationTicks, 1,
                                      kCooldownDurationTicks, parry_stun_duration_ticks);
}

// A hazard as the spawner builds one: a dynamic body with its own mass and restitution, carrying
// the marker and no `Controllable`, because nobody drives a comet.
[[nodiscard]] simulation::PhysicsBody hazard_body(const double x, const double y) {
  return simulation::PhysicsBody::create(point(x, y), point(40.0, 0.0), point(0.0, 0.0), 26.0, 40.0,
                                         simulation::PhysicsBody::kDefaultCollisionLayer,
                                         simulation::PhysicsBody::kDefaultCollisionMask, false)
      .with_restitution(0.2);
}

[[nodiscard]] simulation::PhysicsBody player_body(const double x, const double y) {
  return simulation::PhysicsBody::create(point(x, y), point(0.0, 0.0), point(0.0, 0.0));
}

// A baseline disc of radius 10 travelling along x, which is the shape the accepted pair equations
// are stated in: two of them exactly touch at 20 apart, so the composed velocities below are the
// same literals `guarded_pair_contact_tests.cpp` pins for the pure core.
[[nodiscard]] simulation::PhysicsBody baseline_body(const double x, const double speed) {
  return simulation::PhysicsBody::create(point(x, 100.0), point(speed, 0.0), point(0.0, 0.0), 10.0,
                                         1.0, simulation::PhysicsBody::kDefaultCollisionLayer,
                                         simulation::PhysicsBody::kDefaultCollisionMask, false);
}

[[nodiscard]] simulation::ContactRuleTable guarded_table() {
  return simulation::ContactRuleTable::with_rows_above_built_in(
      {gameplay::guarded_pair_contact_rule()});
}

// Two seated bodies in a running match, plus the levers each test pulls: the marker that makes one
// lethal, the driver that makes one a victim, and the committed shield the adapter projects.
//
// It is constructed in place and never returned by value, because `TickHarness` deletes both its
// copy and its move constructors; the named arrangement below is a derived fixture rather than a
// factory function for exactly that reason.
struct GuardedRowFixture {
  GuardedRowFixture(simulation::PhysicsBody first_body, simulation::PhysicsBody second_body,
                    const double contact_distance)
      : world(simulation::GameWorld::create({})), first{entity(1), std::move(first_body)},
        second{entity(2), std::move(second_body)}, distance(contact_distance),
        harness(tick(kContactTick)) {
    world.mutable_match().phase = simulation::MatchPhase::kRunning;
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(first.entity, first.body);
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(second.entity, second.body);
  }

  [[nodiscard]] simulation::PlayerPairContact contact() const {
    return simulation::detect_pair_contact(first.body, second.body, distance);
  }

  // An admitted closing impact with both sources eligible, which is what the solver hands a row
  // once its own closing-impact admission has passed.
  [[nodiscard]] simulation::PairContactObservation impact_observation() const {
    const simulation::PlayerPairContact touch = contact();
    return {touch, std::optional{touch}, true, true};
  }

  [[nodiscard]] simulation::ContactResponse
  respond(const simulation::PairContactObservation& observation) const {
    return gameplay::guarded_pair_response(world, first, second, observation, harness.context());
  }

  [[nodiscard]] simulation::ContactResponse respond_to_impact() const {
    return respond(impact_observation());
  }

  void make_lethal(const simulation::EntityId id) {
    world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
        id, simulation::LethalOnContact{});
  }

  void make_driven(const simulation::EntityId id) {
    world.mutable_store<simulation::Controllable>().insert_or_assign(
        id, simulation::Controllable{simulation::ControllerId::create(id.value())});
  }

  void give_shield(const simulation::EntityId id, const simulation::Shield& shield) {
    world.mutable_store<simulation::Shield>().insert_or_assign(id, shield);
  }

  simulation::GameWorld world;
  simulation::ContactRule::Subject first;
  simulation::ContactRule::Subject second;
  double distance;
  testing::TickHarness harness;
};

// A hazard at the higher id and a driven player at the lower one, exactly overlapping, which is the
// orientation the deleted row could only fire in swapped. The adapter's predicates are symmetric,
// so it now fires canonically; the *response* is what still distinguishes source from victim.
struct PlayerThenHazardFixture final : GuardedRowFixture {
  PlayerThenHazardFixture()
      : GuardedRowFixture(player_body(100.0, 100.0), hazard_body(130.0, 100.0), 36.0) {
    make_driven(entity(1));
    make_lethal(entity(2));
  }
};

[[nodiscard]] std::vector<simulation::WorldEvent>
events_of(const simulation::ContactResponse& response) {
  return {response.events().begin(), response.events().end()};
}

template <class Event>
[[nodiscard]] std::size_t event_count(const std::vector<simulation::WorldEvent>& events) {
  std::size_t count = 0;
  for (const simulation::WorldEvent& event : events) {
    if (std::holds_alternative<Event>(event)) {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST_CASE("the guarded row is declared first and matches a hazard against a player canonically",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  const PlayerThenHazardFixture fixture;
  const simulation::ContactRuleTable table = guarded_table();

  // The deleted row matched this pair only in the swapped orientation, because its first predicate
  // was `LethalOnContact` presence and the hazard holds the higher id. The replacement's two
  // predicates are the same total presence test, so the canonical orientation matches first and the
  // swapped mapping is unreachable. Row 0 is still the whole point: it is above every impulse row.
  const std::optional<simulation::ContactRuleTable::Match> match =
      table.first_match(fixture.world, entity(1), entity(2));
  REQUIRE(match.has_value());
  CHECK(match->row_index == 0);
  CHECK(match->orientation == simulation::ContactOrientation::kCanonical);
  CHECK(table.rows()[match->row_index].name() == gameplay::kGuardedPairContactRuleName);
}

TEST_CASE("the guarded row wins over the impulse rows that would also match the pair",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  const PlayerThenHazardFixture fixture;

  // Precedence is the whole mechanism, so this states the counterfactual rather than trusting it.
  // The same pair offered to the built-in table alone matches `variable_impulse`, because a hazard
  // is a dynamic body whose mass differs from the baseline. Declared below that row, the guarded
  // row would never fire and a comet would shove a player aside instead of killing them.
  const simulation::ContactRuleTable built_in = simulation::ContactRuleTable::built_in();
  const std::optional<simulation::ContactRuleTable::Match> without_the_row =
      built_in.first_match(fixture.world, entity(1), entity(2));
  REQUIRE(without_the_row.has_value());
  CHECK(built_in.rows()[without_the_row->row_index].name() ==
        simulation::kVariableImpulseContactRuleName);

  const simulation::ContactRuleTable table = guarded_table();
  const std::optional<simulation::ContactRuleTable::Match> with_the_row =
      table.first_match(fixture.world, entity(1), entity(2));
  REQUIRE(with_the_row.has_value());
  CHECK(table.rows()[with_the_row->row_index].name() == gameplay::kGuardedPairContactRuleName);
}

TEST_CASE("the guarded row matches dynamic pairs and dynamic-static pairs alike",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(1),
                                                                  baseline_body(100.0, 10.0));
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(2),
                                                                  baseline_body(120.0, -5.0));
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(3), simulation::PhysicsBody::create_static(point(140.0, 100.0)));
  // The zone carries no body at all, which is a pair the broad phase would never offer and the
  // predicates must still be total over.
  world.mutable_store<simulation::Zone>().insert_or_assign(
      entity(4), simulation::Zone{point(480.0, 320.0), 200.0});

  const simulation::ContactRuleTable table = guarded_table();
  for (const auto& pair :
       std::array{std::pair{entity(1), entity(2)}, std::pair{entity(2), entity(3)},
                  std::pair{entity(1), entity(3)}}) {
    CAPTURE(pair.first.value(), pair.second.value());
    const std::optional<simulation::ContactRuleTable::Match> match =
        table.first_match(world, pair.first, pair.second);
    REQUIRE(match.has_value());
    CHECK(match->row_index == 0);
    CHECK(match->orientation == simulation::ContactOrientation::kCanonical);
  }
  // Static/static never reaches a row -- the broad phase drops it -- so the symmetric predicate is
  // never asked about that pair in production; a bodyless entity ends the walk with no row.
  CHECK_FALSE(table.first_match(world, entity(1), entity(4)).has_value());
  CHECK_FALSE(gameplay::body_has_contact_presence(world, entity(4)));
  CHECK(gameplay::body_has_contact_presence(world, entity(3)));
}

TEST_CASE("an eligible lethal contact terminates the player and preserves both stored body values",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  const PlayerThenHazardFixture fixture;
  const simulation::PlayerPairContact touch = fixture.contact();

  // Source-oriented: the hazard is `second` here, so its eligibility to affect `first` is the
  // second flag. The touch carries no impact, which is the deleted row's own situation -- it never
  // computed physics -- and lethality still applies.
  const simulation::ContactResponse response = fixture.respond({touch, std::nullopt, false, true});

  REQUIRE(response.replaces_bodies());
  // The hazard keeps travelling: a comet that staggered off what it killed would read as a bug to
  // anyone watching, and the player's velocity is about to stop existing.
  CHECK(response.first_body() == fixture.first.body);
  CHECK(response.second_body() == fixture.second.body);
  CHECK(response.first_result().disposition == simulation::MotionDisposition::kTerminate);
  CHECK(response.second_result().disposition == simulation::MotionDisposition::kContinue);

  const std::vector<simulation::WorldEvent> events = events_of(response);
  REQUIRE(events.size() == 2);
  REQUIRE(std::holds_alternative<simulation::EliminationEvent>(events[0]));
  // The *player* is named, never the hazard.
  CHECK(std::get<simulation::EliminationEvent>(events[0]).entity == entity(1));
  REQUIRE(std::holds_alternative<simulation::ContactEvent>(events[1]));
  // One row, two diagnostic names: the lethal branch keeps the identity every consumer of the
  // deleted row already matched against.
  CHECK(std::get<simulation::ContactEvent>(events[1]).rule_name.value() ==
        gameplay::kLethalHazardContactRuleName);
}

TEST_CASE("lethal eligibility belongs to the hazard source not the recipient player",
          "[unit][gameplay][shared][guarded_pair_rule][contact_effect_policy]") {
  const PlayerThenHazardFixture fixture;
  const simulation::PlayerPairContact touch = fixture.contact();

  // The victim's own policy admits nothing. The deleted row expressed this as
  // `ContactResponse::unchanged()`; the composition expresses it as a response that changes neither
  // body and reports the touch under the ordinary name, because a matched pair that computed
  // nothing still owes a diagnostic and `unchanged()` emits none.
  const simulation::ContactResponse declined = fixture.respond({touch, std::nullopt, true, false});
  REQUIRE(declined.replaces_bodies());
  CHECK(declined.first_body() == fixture.first.body);
  CHECK(declined.second_body() == fixture.second.body);
  CHECK(declined.first_result().disposition == simulation::MotionDisposition::kContinue);
  CHECK(declined.second_result().disposition == simulation::MotionDisposition::kContinue);
  const std::vector<simulation::WorldEvent> declined_events = events_of(declined);
  REQUIRE(declined_events.size() == 1);
  CHECK(event_count<simulation::EliminationEvent>(declined_events) == 0);
  CHECK(std::get<simulation::ContactEvent>(declined_events[0]).rule_name.value() ==
        gameplay::kGuardedPairContactRuleName);

  const simulation::ContactResponse admitted = fixture.respond({touch, std::nullopt, false, true});
  REQUIRE(admitted.replaces_bodies());
  CHECK(admitted.first_result().disposition == simulation::MotionDisposition::kTerminate);
  CHECK(admitted.second_result().disposition == simulation::MotionDisposition::kContinue);
  CHECK(admitted.events().size() == 2);
}

TEST_CASE("the guarded row kills only while the match is running",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  // The deleted row expressed the gate by not matching, so the pair fell through to
  // `variable_impulse`. The replacement matches every pair, so the gate now shows up one layer in:
  // the row still fires, the composition still resolves the impulse, and no elimination is emitted.
  for (const simulation::MatchPhase phase :
       std::array{simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                  simulation::MatchPhase::kEnded}) {
    INFO("phase " << static_cast<int>(phase));
    PlayerThenHazardFixture fixture;
    fixture.world.mutable_match().phase = phase;
    CHECK_FALSE(gameplay::body_is_lethal_hazard(fixture.world, entity(2)));

    const std::vector<simulation::WorldEvent> events = events_of(fixture.respond_to_impact());
    CHECK(event_count<simulation::EliminationEvent>(events) == 0);
    REQUIRE(events.size() == 1);
    CHECK(std::get<simulation::ContactEvent>(events[0]).rule_name.value() ==
          gameplay::kGuardedPairContactRuleName);
  }

  const PlayerThenHazardFixture running;
  CHECK(gameplay::body_is_lethal_hazard(running.world, entity(2)));
  const std::vector<simulation::WorldEvent> events = events_of(running.respond_to_impact());
  CHECK(event_count<simulation::EliminationEvent>(events) == 1);
}

TEST_CASE("a hazard cannot eliminate another hazard, a wall, or the zone",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  SECTION("two comets crossing bounce off each other and neither is eliminated") {
    GuardedRowFixture fixture{hazard_body(100.0, 100.0), hazard_body(130.0, 100.0), 52.0};
    fixture.make_lethal(entity(1));
    fixture.make_lethal(entity(2));

    // Neither side carries a `Controllable`, and eliminating an entity with no controller is a hard
    // failure in `placement_recorder`, so requiring a driver on the victim's side is what keeps a
    // rule mistake from becoming a tick failure at kLifecycle.
    CHECK_FALSE(gameplay::body_is_player_driven(fixture.world, entity(1)));
    CHECK_FALSE(gameplay::body_is_player_driven(fixture.world, entity(2)));
    const std::vector<simulation::WorldEvent> events = events_of(fixture.respond_to_impact());
    CHECK(event_count<simulation::EliminationEvent>(events) == 0);
    CHECK(event_count<simulation::ContactEvent>(events) == 1);
  }

  SECTION("a map's static wall is never eliminated and is never moved") {
    const simulation::PhysicsBody wall =
        simulation::PhysicsBody::create_static(point(120.0, 100.0));
    // A baseline-mass hazard, because the marker is what makes a body lethal and using one keeps
    // the reflected velocity the accepted literal rather than a general-impulse computation.
    GuardedRowFixture fixture{baseline_body(100.0, 10.0), wall, 20.0};
    fixture.make_lethal(entity(1));

    const simulation::ContactResponse response = fixture.respond_to_impact();
    CHECK(response.second_body() == wall);
    const std::vector<simulation::WorldEvent> events = events_of(response);
    CHECK(event_count<simulation::EliminationEvent>(events) == 0);
  }

  SECTION("the zone entity is not a pair the row can be asked about") {
    simulation::GameWorld world = simulation::GameWorld::create({});
    world.mutable_match().phase = simulation::MatchPhase::kRunning;
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(1),
                                                                    hazard_body(100.0, 100.0));
    world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
        entity(1), simulation::LethalOnContact{});
    world.mutable_store<simulation::Zone>().insert_or_assign(
        entity(3), simulation::Zone{point(480.0, 320.0), 200.0});
    CHECK_FALSE(guarded_table().first_match(world, entity(1), entity(3)).has_value());
  }
}

TEST_CASE("lethality still ignores the zone grace period",
          "[unit][gameplay][shared][guarded_pair_rule]") {
  PlayerThenHazardFixture fixture;
  // A player with the whole grace period still ahead of it: zero consecutive ticks outside, and a
  // zone it is comfortably inside. `zone_elimination` would not name this entity for many ticks.
  fixture.world.mutable_store<simulation::ZoneExposure>().insert_or_assign(
      entity(1), simulation::ZoneExposure{0});
  fixture.world.mutable_store<simulation::Zone>().insert_or_assign(
      entity(3), simulation::Zone{point(100.0, 100.0), 400.0});

  // Lethality and the zone are two different rules with two different causes. A grace period on
  // "a comet hit you" would mean walking through a comet unharmed, so the count is one regardless
  // of how much grace the player has left.
  const std::vector<simulation::WorldEvent> events = events_of(fixture.respond_to_impact());
  CHECK(event_count<simulation::EliminationEvent>(events) == 1);
  CHECK(fixture.world.store<simulation::ZoneExposure>().find(entity(1))->outside_ticks == 0);
}

TEST_CASE("a shielded player is not eliminated by a lethal hazard",
          "[unit][gameplay][shared][guarded_pair_rule][shield]") {
  for (const simulation::Shield& shield : std::array{ordinary_shield(240), perfect_shield(240)}) {
    PlayerThenHazardFixture fixture;
    fixture.give_shield(entity(1), shield);

    const simulation::ContactResponse response = fixture.respond_to_impact();
    const std::vector<simulation::WorldEvent> events = events_of(response);
    // A guard of either strength removes the victim from the lethal branch entirely, so the pair
    // falls into the ordinary physical composition and nobody is destroyed.
    CHECK(event_count<simulation::EliminationEvent>(events) == 0);
    CHECK(response.first_result().disposition == simulation::MotionDisposition::kContinue);
    CHECK(response.second_result().disposition == simulation::MotionDisposition::kContinue);
    CHECK(std::get<simulation::ContactEvent>(events.back()).rule_name.value() ==
          gameplay::kGuardedPairContactRuleName);
  }
}

TEST_CASE("an ordinary post-opening shield blocks the kill and quarters the knockback it takes",
          "[unit][gameplay][shared][guarded_pair_rule][shield]") {
  // Both discs are baseline, so the accepted equal-mass exchange applies and the numbers are the
  // literals the pure core is pinned to: the lethal marker changes who may die, never which
  // equation runs. Unguarded the victim would leave at 30; quartering the received delta leaves it
  // at 0 + (30 - 0) * 0.25.
  GuardedRowFixture fixture{baseline_body(100.0, 30.0), baseline_body(120.0, 0.0), 20.0};
  fixture.make_lethal(entity(1));
  fixture.make_driven(entity(2));
  fixture.give_shield(entity(2), ordinary_shield(240));

  const simulation::ContactResponse response = fixture.respond_to_impact();
  CHECK(response.first_body().velocity() == point(0.0, 0.0));
  CHECK(response.second_body().velocity() == point(7.5, 0.0));
  const std::vector<simulation::WorldEvent> events = events_of(response);
  CHECK(event_count<simulation::EliminationEvent>(events) == 0);
  // Ordinary protection is not a parry: the opening has closed, so no momentum is cancelled and no
  // stun is owed.
  CHECK(event_count<simulation::StunRequest>(events) == 0);
  CHECK(event_count<simulation::ContactEvent>(events) == 1);
}

TEST_CASE("a perfect defender's stun request carries the defender's own captured duration",
          "[unit][gameplay][shared][guarded_pair_rule][shield]") {
  constexpr std::uint64_t kDefenderParryTicks = 240;
  GuardedRowFixture fixture{baseline_body(100.0, 10.0), baseline_body(120.0, -5.0), 20.0};
  fixture.make_driven(entity(1));
  fixture.make_driven(entity(2));
  fixture.give_shield(entity(2), perfect_shield(kDefenderParryTicks));

  const simulation::ContactResponse response = fixture.respond_to_impact();
  const std::vector<simulation::WorldEvent> events = events_of(response);
  REQUIRE(events.size() == 2);
  REQUIRE(std::holds_alternative<simulation::StunRequest>(events[0]));
  // The stunned body is the attacker; the duration is the defender's, read off the shield that
  // actually parried rather than off a configuration this noncapturing response could not hold.
  CHECK(std::get<simulation::StunRequest>(events[0]).entity == entity(1));
  CHECK(std::get<simulation::StunRequest>(events[0]).duration_ticks == kDefenderParryTicks);
  CHECK(response.first_body().velocity() == point(0.0, 0.0));
  CHECK(response.first_body().acceleration() == point(0.0, 0.0));
}

TEST_CASE("mutual perfects stun both bodies, each with the opposite defender's duration",
          "[unit][gameplay][shared][guarded_pair_rule][shield]") {
  constexpr std::uint64_t kFirstParryTicks = 120;
  constexpr std::uint64_t kSecondParryTicks = 240;
  GuardedRowFixture fixture{baseline_body(100.0, 10.0), baseline_body(120.0, -5.0), 20.0};
  fixture.make_driven(entity(1));
  fixture.make_driven(entity(2));
  fixture.give_shield(entity(1), perfect_shield(kFirstParryTicks));
  fixture.give_shield(entity(2), perfect_shield(kSecondParryTicks));

  const simulation::ContactResponse response = fixture.respond_to_impact();
  const std::vector<simulation::WorldEvent> events = events_of(response);
  REQUIRE(events.size() == 3);
  // Recipients in ascending EntityId, then the one canonical contact: the adapter preserves the
  // core's production order rather than grouping by event kind. The two durations are distinct on
  // purpose, so a translation that read the recipient's own shield instead of the defender's would
  // swap them and fail here.
  REQUIRE(std::holds_alternative<simulation::StunRequest>(events[0]));
  CHECK(std::get<simulation::StunRequest>(events[0]).entity == entity(1));
  CHECK(std::get<simulation::StunRequest>(events[0]).duration_ticks == kSecondParryTicks);
  REQUIRE(std::holds_alternative<simulation::StunRequest>(events[1]));
  CHECK(std::get<simulation::StunRequest>(events[1]).entity == entity(2));
  CHECK(std::get<simulation::StunRequest>(events[1]).duration_ticks == kFirstParryTicks);
  CHECK(std::holds_alternative<simulation::ContactEvent>(events[2]));
  CHECK(response.first_body().velocity() == point(0.0, 0.0));
  CHECK(response.second_body().velocity() == point(0.0, 0.0));
}

TEST_CASE("a touch without incoming motion manufactures no parry",
          "[unit][gameplay][shared][guarded_pair_rule][shield]") {
  // The perfect defender is the one that closes the gap. The counterpart is stationary in one case
  // and retreating in the other, and neither is "an incoming opponent", so neither is stunned even
  // though the pair genuinely closes and the opening is genuinely open.
  for (const double counterpart_speed : std::array{0.0, 9.0}) {
    CAPTURE(counterpart_speed);
    GuardedRowFixture fixture{baseline_body(100.0, 10.0), baseline_body(120.0, counterpart_speed),
                              20.0};
    fixture.make_driven(entity(1));
    fixture.make_driven(entity(2));
    fixture.give_shield(entity(1), perfect_shield(240));

    const std::vector<simulation::WorldEvent> events = events_of(fixture.respond_to_impact());
    CHECK(event_count<simulation::StunRequest>(events) == 0);
    CHECK(event_count<simulation::ContactEvent>(events) == 1);
  }
}

TEST_CASE("a static subject never carries a guard into the composition",
          "[unit][gameplay][shared][guarded_pair_rule][shield]") {
  // `compose_guarded_pair` fails the whole tick for a guarded static subject, so the adapter forces
  // `kNone` for one rather than trusting that no `Shield` was ever left on a wall. This plants one
  // there deliberately: the wall must still be an ordinary reflector and must not be stunned.
  const simulation::PhysicsBody wall = simulation::PhysicsBody::create_static(point(120.0, 100.0));
  GuardedRowFixture fixture{baseline_body(100.0, 10.0), wall, 20.0};
  fixture.make_driven(entity(1));
  fixture.give_shield(entity(2), perfect_shield(240));

  simulation::ContactResponse response = simulation::ContactResponse::unchanged();
  REQUIRE_NOTHROW(response = fixture.respond_to_impact());
  CHECK(response.second_body() == wall);
  CHECK(response.second_result().disposition == simulation::MotionDisposition::kContinue);
  const std::vector<simulation::WorldEvent> events = events_of(response);
  CHECK(event_count<simulation::StunRequest>(events) == 0);
  CHECK(event_count<simulation::ContactEvent>(events) == 1);
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
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(11), hazard_body(430.0, 320.0)
                      .with_velocity(point(-40.0, 0.0))
                      .with_bounds_behavior(simulation::BoundsBehavior::kCross));
  world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
      entity(11), simulation::LethalOnContact{});

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
          "[unit][gameplay][shared][guarded_pair_rule]") {
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
    // The composition selected the general impulse, exactly as `variable_impulse` used to: a
    // forty-mass disc moving left pushed a unit-mass blob left.
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
