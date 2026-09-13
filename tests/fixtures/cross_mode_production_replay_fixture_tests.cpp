#include "replay_fixture.hpp"

#include "components/charge_component.hpp"
#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_states/race_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "terrain_queries.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

// canonical: cross_mode_production_replay_fixture_tests -- authored commands exercise the
// production ability, continuous contact, terrain and race pipelines without seeded motion.
using Snapshots = std::vector<simulation::WorldSnapshot>;
constexpr std::uint64_t kChargeTick = 3;
constexpr double kSpawnX = 300.0;
constexpr double kCenterY = 320.0;
constexpr double kPlayerRadius = 2.0;
constexpr double kBurstSpeed = 7'500.0;
constexpr double kDampedSpeed = 7'462.5;
constexpr double kDampedDisplacement = 18.65625;
constexpr double kContactTravel = 6.0;
constexpr double kAnalyticalMargin = 1e-9;

[[nodiscard]] simulation::Vector2 point(const double x, const double y = kCenterY) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] const simulation::WorldSnapshot& at_tick(const Snapshots& snapshots,
                                                       const std::uint64_t tick) {
  REQUIRE(tick >= 1);
  REQUIRE(tick <= snapshots.size());
  return snapshots[static_cast<std::size_t>(tick) - 1];
}

template <typename Component>
[[nodiscard]] std::optional<Component> component_of(const simulation::WorldSnapshot& snapshot,
                                                    const simulation::EntityId entity) {
  for (const auto& entry : snapshot.components<Component>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] const simulation::PhysicsBody& body_of(const simulation::WorldSnapshot& snapshot,
                                                     const simulation::EntityId entity) {
  const simulation::PhysicsBody* body = nullptr;
  for (const auto& entry : snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == entity) {
      body = &entry.value;
      break;
    }
  }
  REQUIRE(body != nullptr);
  return *body;
}

[[nodiscard]] const simulation::RaceModeState& race_of(const simulation::WorldSnapshot& snapshot) {
  const auto* race = std::get_if<simulation::RaceModeState>(&snapshot.match().mode_state());
  REQUIRE(race != nullptr);
  return *race;
}

void check_position(const simulation::PhysicsBody& body, const double x, const double y) {
  CHECK(body.position().x() == Catch::Approx(x).epsilon(0.0).margin(kAnalyticalMargin));
  CHECK(body.position().y() == y);
}

void check_charge(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity,
                  const std::uint64_t activation) {
  const auto charge = component_of<simulation::Charge>(snapshot, entity);
  REQUIRE(charge.has_value());
  CHECK(charge->activation_tick() == simulation::TickSequence::create(activation));
  CHECK(charge->cooldown_window().expiry_tick() ==
        simulation::TickSequence::create(activation + 480));
}

void check_damped_witness_configuration(const testing::ReplayFixture& fixture) {
  CHECK(fixture.configuration().player_radius() == kPlayerRadius);
  CHECK(fixture.configuration().drag_per_second() == 2.0);
  CHECK(fixture.movement().normal_top_speed() == 10'000.0);
  CHECK(fixture.mode_configuration().abilities.charge_speed_fraction() == 0.75);
  // Derived independently from the written .75 burst, 400 Hz, and deployed drag 2.
  CHECK(kBurstSpeed * (1.0 - 2.0 / 400.0) == kDampedSpeed);
  CHECK(kDampedSpeed / 400.0 == kDampedDisplacement);
}

void require_one_hundred_identical_runs(const testing::ReplayFixture& fixture,
                                        const Snapshots& reference) {
  REQUIRE(reference.size() == fixture.tick_count());
  for (std::size_t run = 2; run <= 100; ++run) {
    const auto divergent_tick = testing::first_divergent_tick(reference, fixture.run());
    INFO(fixture.name() << ": run " << run << ", first divergent tick " << divergent_tick);
    REQUIRE(divergent_tick == 0);
  }
}

} // namespace

TEST_CASE("a recorded charge transfers momentum before its free endpoint passes the entire body",
          "[fixtures][replay][production][royale][charge][contact]") {
  const auto fixture = testing::ReplayFixture::named("royale-charge-contact");
  check_damped_witness_configuration(fixture);
  const auto snapshots = fixture.run();
  const auto charger = fixture.spawned_entity_id(1, 0);
  const auto target = fixture.spawned_entity_id(1, 1);
  const auto& before = at_tick(snapshots, kChargeTick - 1);
  REQUIRE(before.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(body_of(before, charger).position() == point(kSpawnX));
  CHECK(body_of(before, target).position() == point(kSpawnX + 10.0));
  CHECK(body_of(before, charger).velocity() == point(0.0, 0.0));
  CHECK(body_of(before, target).velocity() == point(0.0, 0.0));
  CHECK_FALSE(component_of<simulation::Charge>(before, charger).has_value());
  REQUIRE(kDampedDisplacement > 10.0 + 2.0 * kPlayerRadius);

  const auto& contact = at_tick(snapshots, kChargeTick);
  check_charge(contact, charger, kChargeTick);
  const auto stopped = body_of(contact, charger);
  const auto pushed = body_of(contact, target);
  check_position(stopped, kSpawnX + kContactTravel, kCenterY);
  check_position(pushed, kSpawnX + 10.0 + kDampedDisplacement - kContactTravel, kCenterY);
  CHECK(stopped.velocity() == point(0.0, 0.0));
  CHECK(pushed.velocity() == point(kDampedSpeed, 0.0));
  CHECK(stopped.acceleration() == point(0.0, 0.0));
  CHECK(pushed.acceleration() == point(0.0, 0.0));
  CHECK_FALSE(component_of<simulation::Charge>(contact, target).has_value());
  CHECK(contact.match().phase() == simulation::MatchPhase::kRunning);
  require_one_hundred_identical_runs(fixture, snapshots);
}

TEST_CASE("a recorded charge falls through a hole even when its whole free endpoint is supported",
          "[fixtures][replay][production][royale][charge][falling]") {
  const auto fixture = testing::ReplayFixture::named("royale-charge-hole");
  check_damped_witness_configuration(fixture);
  const auto snapshots = fixture.run();
  const auto charger = fixture.spawned_entity_id(1, 0);
  const auto survivor = fixture.spawned_entity_id(1, 1);
  const auto& before = at_tick(snapshots, kChargeTick - 1);
  REQUIRE(before.match().phase() == simulation::MatchPhase::kRunning);
  CHECK(body_of(before, charger).position() == point(kSpawnX));
  CHECK(body_of(before, charger).velocity() == point(0.0, 0.0));
  CHECK(simulation::terrain_supports_point(fixture.map().terrain(), point(kSpawnX)));
  CHECK_FALSE(simulation::terrain_supports_point(fixture.map().terrain(), point(310.0)));
  CHECK(simulation::terrain_supports_point(fixture.map().terrain(),
                                           point(kSpawnX + kDampedDisplacement)));

  const auto& fallen = at_tick(snapshots, kChargeTick);
  const auto* royale =
      std::get_if<simulation::RoyalePlacementsModeState>(&fallen.match().mode_state());
  REQUIRE(royale != nullptr);
  REQUIRE(royale->placements.size() == 1);
  CHECK(royale->placements.front().entity == charger);
  CHECK(royale->placements.front().controller == simulation::ControllerId::create(1));
  CHECK(royale->placements.front().placement == 2);
  CHECK(royale->placements.front().elimination_tick ==
        simulation::TickSequence::create(kChargeTick));
  CHECK(fallen.match().outcome() == simulation::MatchOutcome::won_by_entity(survivor));
  for (std::uint64_t tick = kChargeTick; tick <= fixture.tick_count(); ++tick) {
    CAPTURE(tick);
    const auto& snapshot = at_tick(snapshots, tick);
    CHECK_FALSE(component_of<simulation::PhysicsBody>(snapshot, charger).has_value());
    CHECK_FALSE(component_of<simulation::Charge>(snapshot, charger).has_value());
    CHECK_FALSE(component_of<simulation::Controllable>(snapshot, charger).has_value());
    CHECK(body_of(snapshot, survivor) == body_of(before, survivor));
  }
  require_one_hundred_identical_runs(fixture, snapshots);
}

TEST_CASE("a recorded charged racer takes three gates in one quantum and stops before contact",
          "[fixtures][replay][production][race][charge][chronology]") {
  const auto fixture = testing::ReplayFixture::named("race-charge-finish");
  check_damped_witness_configuration(fixture);
  const auto snapshots = fixture.run();
  const auto racer = fixture.spawned_entity_id(1, 0);
  const auto wall = simulation::EntityId::create(simulation::kMinimumEntityId);
  REQUIRE(wall < racer);
  const auto& before = at_tick(snapshots, kChargeTick - 1);
  REQUIRE(before.match().phase() == simulation::MatchPhase::kRunning);
  CHECK_FALSE(component_of<simulation::RaceProgress>(before, racer).has_value());
  CHECK(race_of(before).standings.empty());
  CHECK(body_of(before, racer).position() == point(kSpawnX));
  CHECK(body_of(before, racer).velocity() == point(0.0, 0.0));
  CHECK(body_of(before, wall).position() == point(318.0));

  const auto& finish = at_tick(snapshots, kChargeTick);
  check_charge(finish, racer, kChargeTick);
  CHECK(component_of<simulation::RaceProgress>(finish, racer) == simulation::RaceProgress{3});
  const auto& standings = race_of(finish).standings;
  REQUIRE(standings.size() == 1);
  CHECK(standings.front().entity == racer);
  CHECK(standings.front().controller == simulation::ControllerId::create(1));
  CHECK(standings.front().placement == 1);
  CHECK(standings.front().finished_tick == simulation::TickSequence::create(kChargeTick));
  // Gate circles include the published positional tolerance. Final entry precedes the
  // wall contact at x314; an endpoint collision response would reverse the charged body.
  const double finish_x = 312.0 - (1.0 + simulation::kPositionTolerance);
  CHECK(standings.front().finished_tick_offset.value() ==
        Catch::Approx((finish_x - kSpawnX) / kDampedDisplacement)
            .epsilon(0.0)
            .margin(kAnalyticalMargin));
  CHECK(finish.match().outcome() == simulation::MatchOutcome::won_by_entity(racer));
  for (std::uint64_t tick = kChargeTick; tick <= fixture.tick_count(); ++tick) {
    CAPTURE(tick);
    const auto& snapshot = at_tick(snapshots, tick);
    const auto stopped = body_of(snapshot, racer);
    check_position(stopped, finish_x, kCenterY);
    CHECK(stopped.velocity() == point(0.0, 0.0));
    CHECK(stopped.acceleration() == point(0.0, 0.0));
    CHECK_FALSE(component_of<simulation::RespawnTimer>(snapshot, racer).has_value());
    CHECK(body_of(snapshot, wall) == body_of(before, wall));
    CHECK(race_of(snapshot).standings == standings);
  }
  require_one_hundred_identical_runs(fixture, snapshots);
}

TEST_CASE("a charged race fall retains earlier gates and suppresses later contact and finish",
          "[fixtures][replay][production][race][charge][falling][respawn][chronology]") {
  const auto fixture = testing::ReplayFixture::named("race-charge-fall");
  check_damped_witness_configuration(fixture);
  const auto snapshots = fixture.run();
  const auto racer = fixture.spawned_entity_id(1, 0);
  const auto peer = fixture.spawned_entity_id(1, 1);
  const auto& before = at_tick(snapshots, kChargeTick - 1);
  REQUIRE(before.match().phase() == simulation::MatchPhase::kRunning);
  CHECK_FALSE(component_of<simulation::RaceProgress>(before, racer).has_value());
  CHECK(body_of(before, racer).position() == point(kSpawnX));
  CHECK(body_of(before, racer).velocity() == point(0.0, 0.0));
  CHECK(body_of(before, peer).position() == point(318.0));
  CHECK(simulation::terrain_supports_point(fixture.map().terrain(),
                                           point(kSpawnX + kDampedDisplacement)));
  // Last credited checkpoint x305 is five from the hole center, leaving three for the
  // radius-two returning body. Its seat is also thirteen from the untouched peer.
  CHECK(simulation::terrain_supports_disc(fixture.map().terrain(), point(305.0), kPlayerRadius));
  REQUIRE(fixture.race().respawn_delay_ticks() == 8);
  constexpr std::uint64_t kTimerExpiry = kChargeTick + 8;
  constexpr std::uint64_t kReturnTick = kTimerExpiry + 1;
  for (std::uint64_t tick = kChargeTick; tick <= fixture.tick_count(); ++tick) {
    CAPTURE(tick);
    const auto& snapshot = at_tick(snapshots, tick);
    CHECK(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    CHECK(component_of<simulation::RaceProgress>(snapshot, racer) == simulation::RaceProgress{2});
    CHECK(race_of(snapshot).standings.empty());
    CHECK(body_of(snapshot, peer) == body_of(before, peer));
    CHECK(component_of<simulation::Controllable>(snapshot, racer).has_value());
    CHECK_FALSE(component_of<simulation::Charge>(snapshot, racer).has_value());
    CHECK_FALSE(component_of<simulation::Shield>(snapshot, racer).has_value());
    CHECK_FALSE(component_of<simulation::Stun>(snapshot, racer).has_value());
    const auto timer = component_of<simulation::RespawnTimer>(snapshot, racer);
    if (tick < kTimerExpiry) {
      REQUIRE(timer.has_value());
      CHECK(timer->ticks_remaining == kTimerExpiry - tick);
    } else {
      CHECK_FALSE(timer.has_value());
    }
    if (tick < kReturnTick) {
      CHECK_FALSE(component_of<simulation::PhysicsBody>(snapshot, racer).has_value());
    } else {
      const auto returned = body_of(snapshot, racer);
      CHECK(returned.position() == point(305.0));
      CHECK(returned.velocity() == point(0.0, 0.0));
      CHECK(returned.acceleration() == point(0.0, 0.0));
    }
  }
  require_one_hundred_identical_runs(fixture, snapshots);
}

TEST_CASE("ordinary shields quarter charged impacts on both protected boundaries but not expiry",
          "[fixtures][replay][production][royale][charge][shield][chronology]") {
  const auto fixture = testing::ReplayFixture::named("royale-shield-boundaries");
  const auto snapshots = fixture.run();
  CHECK(fixture.configuration().drag_per_second() == 0.0);
  CHECK(fixture.mode_configuration().abilities.shield_perfect_window_ticks() == 32);
  CHECK(fixture.mode_configuration().abilities.shield_duration_ticks() == 160);
  CHECK(fixture.mode_configuration().abilities.shield_cooldown_ticks() == 360);
  struct ShieldBoundaryCase final {
    std::uint64_t attacker_index;
    std::uint64_t contact_tick;
    double center_y;
    double impulse_fraction;
  };
  constexpr std::array kBoundaries{
      ShieldBoundaryCase{0, 35, 1'000.0, 0.25},
      ShieldBoundaryCase{2, 162, 2'000.0, 0.25},
      ShieldBoundaryCase{4, 163, 3'000.0, 1.0},
  };
  constexpr double kAttackerX = 20'000.0;
  constexpr double kDefenderX = 20'010.0;
  constexpr double kUndampedDisplacement = 18.75;
  for (const auto& expected : kBoundaries) {
    CAPTURE(expected.contact_tick);
    const auto attacker = fixture.spawned_entity_id(1, expected.attacker_index);
    const auto defender = fixture.spawned_entity_id(1, expected.attacker_index + 1);
    const auto& raised = at_tick(snapshots, kChargeTick);
    const auto shield = component_of<simulation::Shield>(raised, defender);
    REQUIRE(shield.has_value());
    CHECK(shield->activation_tick() == simulation::TickSequence::create(3));
    CHECK(shield->perfect_window().expiry_tick() == simulation::TickSequence::create(35));
    CHECK(shield->shield_window().expiry_tick() == simulation::TickSequence::create(163));
    CHECK(shield->cooldown_window().expiry_tick() == simulation::TickSequence::create(363));
    const auto contact_tick = simulation::TickSequence::create(expected.contact_tick);
    CHECK_FALSE(shield->perfect_window().contains(contact_tick));
    CHECK(shield->shield_window().contains(contact_tick) == (expected.impulse_fraction == 0.25));
    const auto& before = at_tick(snapshots, expected.contact_tick - 1);
    CHECK(body_of(before, attacker).position() == point(kAttackerX, expected.center_y));
    CHECK(body_of(before, defender).position() == point(kDefenderX, expected.center_y));
    CHECK(body_of(before, attacker).velocity() == point(0.0, 0.0));
    CHECK(body_of(before, defender).velocity() == point(0.0, 0.0));
    const auto& contact = at_tick(snapshots, expected.contact_tick);
    check_charge(contact, attacker, expected.contact_tick);
    const auto stopped = body_of(contact, attacker);
    const auto pushed = body_of(contact, defender);
    check_position(stopped, kAttackerX + kContactTravel, expected.center_y);
    check_position(
        pushed, kDefenderX + (kUndampedDisplacement - kContactTravel) * expected.impulse_fraction,
        expected.center_y);
    CHECK(stopped.velocity() == point(0.0, 0.0));
    CHECK(pushed.velocity() == point(kBurstSpeed * expected.impulse_fraction, 0.0));
    CHECK_FALSE(component_of<simulation::Stun>(contact, attacker).has_value());
    CHECK_FALSE(component_of<simulation::Stun>(contact, defender).has_value());
    CHECK(component_of<simulation::Shield>(contact, defender) == shield);
    CHECK(contact.match().phase() == simulation::MatchPhase::kRunning);
  }
  require_one_hundred_identical_runs(fixture, snapshots);
}
