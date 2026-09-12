#include "replay_fixture.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "entity_id.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics_body.hpp"
#include "royale/zone_shrink_system.hpp"
#include "shared/thrust_steering_system.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;
namespace testing = blob_royale::testing;

namespace {

using Snapshots = std::vector<simulation::WorldSnapshot>;

// The snapshot of a committed tick, one-based, exactly as a fixture's `commands.csv` names ticks.
[[nodiscard]] const simulation::WorldSnapshot& at_tick(const Snapshots& snapshots,
                                                       const std::uint64_t tick) {
  REQUIRE(tick >= 1);
  REQUIRE(tick <= snapshots.size());
  return snapshots[static_cast<std::size_t>(tick) - 1];
}

[[nodiscard]] std::optional<simulation::PhysicsBody>
body_of(const simulation::WorldSnapshot& snapshot, const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::PhysicsBody>::Entry& entry :
       snapshot.components<simulation::PhysicsBody>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool carries_controllable(const simulation::WorldSnapshot& snapshot,
                                        const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       snapshot.components<simulation::Controllable>()) {
    if (entry.entity == entity) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<simulation::Zone> zone_of(const simulation::WorldSnapshot& snapshot) {
  const auto zones = snapshot.components<simulation::Zone>();
  if (zones.empty()) {
    return std::nullopt;
  }
  return zones.front().value;
}

// Absent reads as zero exposure, which is the component's own rule, so this returns nullopt for
// "inside" and a count for "outside" rather than conflating the two into a zero.
[[nodiscard]] std::optional<std::uint64_t> exposure_of(const simulation::WorldSnapshot& snapshot,
                                                       const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::ZoneExposure>::Entry& entry :
       snapshot.components<simulation::ZoneExposure>()) {
    if (entry.entity == entity) {
      return entry.value.outside_ticks;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<simulation::RoyalePlacement>
placements_of(const simulation::WorldSnapshot& snapshot) {
  const auto* held =
      std::get_if<simulation::RoyalePlacementsModeState>(&snapshot.match().mode_state());
  REQUIRE(held != nullptr);
  return held->placements;
}

[[nodiscard]] std::size_t alive_count_of(const simulation::WorldSnapshot& snapshot) {
  return snapshot.players().size();
}

// The greatest distance between any pair of published bodies is not the question; the question is
// whether any pair is inside the baseline contact predicate, which is what "two entities are never
// seated in contact" means (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Player-pair policy").
[[nodiscard]] bool any_pair_in_contact(const simulation::WorldSnapshot& snapshot,
                                       const double player_radius) {
  const auto bodies = snapshot.components<simulation::PhysicsBody>();
  for (std::size_t first = 0; first < bodies.size(); ++first) {
    for (std::size_t second = first + 1; second < bodies.size(); ++second) {
      const double offset_x =
          bodies[first].value.position().x() - bodies[second].value.position().x();
      const double offset_y =
          bodies[first].value.position().y() - bodies[second].value.position().y();
      const double distance = std::sqrt((offset_x * offset_x) + (offset_y * offset_y));
      if (simulation::less_than_or_approximately_equal(distance, 2.0 * player_radius,
                                                       simulation::kPositionTolerance)) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::optional<simulation::Shield> shield_of(const simulation::WorldSnapshot& snapshot,
                                                          const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::Shield>::Entry& entry :
       snapshot.components<simulation::Shield>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<simulation::Stun> stun_of(const simulation::WorldSnapshot& snapshot,
                                                      const simulation::EntityId entity) {
  for (const simulation::ComponentStore<simulation::Stun>::Entry& entry :
       snapshot.components<simulation::Stun>()) {
    if (entry.entity == entity) {
      return entry.value;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("the replay format reads a directory into a runnable match",
          "[fixtures][replay][royale]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-thrust-integration");

  CHECK(fixture.mode_name() == std::string{"royale"});
  CHECK(fixture.seed() == 0);
  CHECK(fixture.tick_count() == 60);
  CHECK(fixture.configuration().drag_per_second() == 0.0);
  CHECK(fixture.map().spawn_points().size() == 3);
  CHECK(fixture.movement().acceleration() == 400.0);
  CHECK(fixture.lobby_seat_count() == 3);

  const Snapshots snapshots = fixture.run();
  REQUIRE(snapshots.size() == fixture.tick_count());
  CHECK(at_tick(snapshots, 60).tick_sequence().value() == 60);
}

TEST_CASE("a replay directory that is malformed is a rejection naming the cause",
          "[fixtures][replay][royale][validation]") {
  CHECK_THROWS_AS(testing::ReplayFixture::named("royale-no-such-fixture"),
                  testing::ReplayFixtureError);
}

TEST_CASE("a seeded royale world adds the zone entity and nothing else while it stays in lobby",
          "[fixtures][replay][royale][zone]") {
  // `docs/architecture/0005-royale-mode.md` § "Fixture expectations": a fixture that never leaves
  // lobby holds the zone at `R_full` and never evaluates elimination, so no royale system touches a
  // body. A match leaves `lobby` only when every seat is filled *and* a start has been requested,
  // and this fixture's `commands.csv` deliberately says neither -- an elimination clock has no
  // place in a fixture whose subject is one integration. The two additions that remain are expected
  // rather than asserted away: the zone entity and the one EntityId drawn to create it.
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-thrust-integration");
  const Snapshots snapshots = fixture.run();

  const simulation::EntityId zone_entity =
      simulation::EntityId::create(fixture.spawned_entity_id(1, 0).value() + 1);
  const double full_radius = gameplay::zone_full_radius(fixture.map().bounds());

  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    const simulation::WorldSnapshot& snapshot = at_tick(snapshots, tick);
    REQUIRE(snapshot.match().phase() == simulation::MatchPhase::kLobby);
    REQUIRE(snapshot.entities().size() == 2);
    const std::optional<simulation::Zone> zone = zone_of(snapshot);
    REQUIRE(zone.has_value());
    CHECK(zone->center == simulation::Vector2::create(480.0, 320.0));
    CHECK(zone->radius == full_radius);
    CHECK_FALSE(body_of(snapshot, zone_entity).has_value());
    CHECK_FALSE(carries_controllable(snapshot, zone_entity));
    CHECK(snapshot.components<simulation::ZoneExposure>().empty());
  }
}

TEST_CASE("thrust integration reaches thrust_max times t and persists with no further command",
          "[fixtures][replay][royale][thrust]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-thrust-integration");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId player = fixture.spawned_entity_id(1, 0);
  const double thrust_max = fixture.movement().acceleration();

  // The command lands on tick 2, so the acceleration is applied on ticks 2 through 29 and the
  // velocity is the accumulation of `thrust_max * dt` over those ticks. Writing the recurrence out
  // is a second implementation of one line of the kernel, which is what makes this an oracle rather
  // than a restatement.
  double expected_velocity_x = 0.0;
  for (std::uint64_t tick = 2; tick <= 29; ++tick) {
    INFO("tick " << tick);
    expected_velocity_x += thrust_max * simulation::kFixedDeltaSeconds;
    const std::optional<simulation::PhysicsBody> body = body_of(at_tick(snapshots, tick), player);
    REQUIRE(body.has_value());
    CHECK(body->acceleration() == simulation::Vector2::create(thrust_max, 0.0));
    CHECK(body->velocity().x() == expected_velocity_x);
    CHECK(body->velocity().y() == 0.0);
  }
  // `thrust_max * t` with `t` the elapsed simulated seconds, which is the figure the ADR states.
  CHECK(simulation::approximately_equal(expected_velocity_x,
                                        thrust_max * (28.0 * simulation::kFixedDeltaSeconds),
                                        simulation::kVelocityTolerance));

  // Ticks 3 through 29 carry no command at all, so the acceleration above persisted unchanged:
  // there is no per-tick decay, reset, or implicit zeroing.
  const std::optional<simulation::PhysicsBody> unchanged = body_of(at_tick(snapshots, 29), player);
  REQUIRE(unchanged.has_value());
  CHECK(unchanged->acceleration() == simulation::Vector2::create(thrust_max, 0.0));
}

TEST_CASE("a diagonal thrust of (1, 1) yields an acceleration of magnitude thrust_max",
          "[fixtures][replay][royale][thrust]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-thrust-integration");
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId player = fixture.spawned_entity_id(1, 0);
  const double thrust_max = fixture.movement().acceleration();

  const std::optional<simulation::PhysicsBody> body = body_of(at_tick(snapshots, 30), player);
  REQUIRE(body.has_value());
  // The one clamp, applied once, at `steered_acceleration`: `(1, 1)` normalizes to the unit disc so
  // diagonal movement carries no advantage.
  CHECK(body->acceleration() ==
        gameplay::steered_acceleration(simulation::Vector2::create(1.0, 1.0), thrust_max));
  CHECK(body->acceleration().x() == body->acceleration().y());
  CHECK(simulation::approximately_equal(body->acceleration().magnitude(), thrust_max,
                                        simulation::kAccelerationTolerance));

  // And it persists to the end of the replay with no further command.
  const std::optional<simulation::PhysicsBody> final_body =
      body_of(at_tick(snapshots, fixture.tick_count()), player);
  REQUIRE(final_body.has_value());
  CHECK(final_body->acceleration() == body->acceleration());
}

TEST_CASE("drag decays velocity by the exact discrete factor and settles at the discrete fixed "
          "point rather than the continuous limit",
          "[fixtures][replay][royale][drag]") {
  // This is the one fixture in the tree that runs at a nonzero drag, which is what
  // `docs/architecture/0005-royale-mode.md` § "Fixture expectations" asks of the drag scenario.
  // Every other fixture keeps `drag_per_second = 0`, so every accepted ADR 0003 horizon stays
  // bit-identical.
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-drag-decay");
  REQUIRE(fixture.configuration().drag_per_second() == 2.0);
  const Snapshots snapshots = fixture.run();
  const simulation::EntityId player = fixture.spawned_entity_id(1, 0);

  const double damping =
      1.0 - (fixture.configuration().drag_per_second() * simulation::kFixedDeltaSeconds);
  CHECK(damping == 0.995);

  // The fixed point of `v <- (v + a * dt) * damping` is `thrust_max * damping / drag`, which is
  // 199 wu/s at the proposed values -- a 0.5 % shortfall against the 200 wu/s continuous limit.
  const double discrete_fixed_point =
      fixture.movement().acceleration() * damping / fixture.configuration().drag_per_second();
  const double continuous_limit =
      fixture.movement().acceleration() / fixture.configuration().drag_per_second();
  CHECK(discrete_fixed_point == 199.0);
  CHECK(continuous_limit == 200.0);

  const std::optional<simulation::PhysicsBody> settled = body_of(at_tick(snapshots, 6000), player);
  REQUIRE(settled.has_value());
  CHECK(simulation::approximately_equal(settled->velocity().x(), discrete_fixed_point,
                                        simulation::kVelocityTolerance));
  CHECK_FALSE(simulation::approximately_equal(settled->velocity().x(), continuous_limit,
                                              simulation::kVelocityTolerance));

  // From tick 6001 the stored acceleration is the zero the coast command wrote, so each committed
  // velocity is exactly the previous one times the damping factor.
  const std::optional<simulation::PhysicsBody> coasting = body_of(at_tick(snapshots, 6002), player);
  const std::optional<simulation::PhysicsBody> coasted = body_of(at_tick(snapshots, 6003), player);
  REQUIRE(coasting.has_value());
  REQUIRE(coasted.has_value());
  CHECK(coasting->acceleration() == simulation::Vector2::create(0.0, 0.0));
  CHECK(coasted->velocity().x() == coasting->velocity().x() * damping);
  CHECK(coasted->velocity().x() < coasting->velocity().x());
}

TEST_CASE("spawning takes consecutive ring points, defers a full ring, and defers during running",
          "[fixtures][replay][royale][spawn]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-spawn-order");
  const Snapshots snapshots = fixture.run();
  const double player_radius = fixture.configuration().player_radius();

  // Three joiners in one tick take three consecutive points from the rotation counter rather than
  // contending for one.
  const simulation::WorldSnapshot& first = at_tick(snapshots, 1);
  CHECK(body_of(first, fixture.spawned_entity_id(1, 0))->position() ==
        simulation::Vector2::create(300.0, 320.0));
  CHECK(body_of(first, fixture.spawned_entity_id(1, 1))->position() ==
        simulation::Vector2::create(480.0, 200.0));
  CHECK(body_of(first, fixture.spawned_entity_id(1, 2))->position() ==
        simulation::Vector2::create(660.0, 320.0));

  // Two more joiners on tick 2: the fourth point is free, the ring is then full, and the fifth
  // joiner is deferred -- it exists carrying only its controller link.
  const simulation::WorldSnapshot& second = at_tick(snapshots, 2);
  const simulation::EntityId fourth = fixture.spawned_entity_id(2, 0);
  const simulation::EntityId fifth = fixture.spawned_entity_id(2, 1);
  CHECK(body_of(second, fourth)->position() == simulation::Vector2::create(480.0, 440.0));
  CHECK(carries_controllable(second, fifth));
  CHECK_FALSE(body_of(second, fifth).has_value());

  // A despawn frees a point at phase 0 of tick 3, and the deferred joiner is seated on that same
  // tick: deferral costs exactly one tick once a point frees.
  const simulation::WorldSnapshot& third = at_tick(snapshots, 3);
  CHECK_FALSE(body_of(third, fixture.spawned_entity_id(1, 0)).has_value());
  CHECK(body_of(third, fifth)->position() == simulation::Vector2::create(300.0, 320.0));

  // The phase timeline this fixture's `start_match` tick was chosen to reproduce, pinned rather
  // than implied. The seats fill and Start is pressed at phase 0 of tick 2, the lifecycle system
  // commits `lobby -> countdown` at the end of that same tick, and `countdown_seconds=0` makes tick
  // 3 `running` -- which is exactly where the retired alive-count threshold put them, and which is
  // what keeps the two seating assertions above meaningful: tick 3's despawn frees a ring point
  // during `countdown`, where the spawn policy still seats, and tick 4's joiner arrives during
  // `running`, where it does not.
  CHECK(at_tick(snapshots, 1).match().phase() == simulation::MatchPhase::kLobby);
  CHECK(at_tick(snapshots, 2).match().phase() == simulation::MatchPhase::kCountdown);
  CHECK(at_tick(snapshots, 3).match().phase() == simulation::MatchPhase::kRunning);

  // A joiner who arrives while the match is running is deferred for as long as it runs, which is
  // what makes a match a closed field.
  const simulation::EntityId late = fixture.spawned_entity_id(4, 0);
  for (std::uint64_t tick = 4; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    const simulation::WorldSnapshot& snapshot = at_tick(snapshots, tick);
    REQUIRE(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    CHECK(carries_controllable(snapshot, late));
    CHECK_FALSE(body_of(snapshot, late).has_value());
  }

  // And no seating ever put two entities in contact.
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    CHECK_FALSE(any_pair_in_contact(at_tick(snapshots, tick), player_radius));
  }
}

TEST_CASE("elimination lands on exactly the Gth consecutive outside tick and re-entry loses the "
          "partial grace",
          "[fixtures][replay][royale][elimination]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-elimination-timing");
  const Snapshots snapshots = fixture.run();
  const std::uint64_t grace = fixture.royale().elimination_grace_ticks();
  REQUIRE(grace == 12);

  const simulation::EntityId on_boundary = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId drifts_out = fixture.spawned_entity_id(1, 1);
  const simulation::EntityId returns = fixture.spawned_entity_id(1, 2);

  // A centre exactly on the boundary is inside, consistent with the baseline's inclusive contact
  // rule: this entity never thrusts, sits at exactly `R_min` from the centre, and is never counted
  // outside or eliminated.
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    const simulation::WorldSnapshot& snapshot = at_tick(snapshots, tick);
    CHECK_FALSE(exposure_of(snapshot, on_boundary).has_value());
    CHECK(body_of(snapshot, on_boundary).has_value());
  }

  // The zone holds exactly `R_min` by assignment once `e >= T`, which for `T = 0` is every
  // evaluated running tick.
  CHECK(zone_of(at_tick(snapshots, 3))->radius == fixture.royale().zone_minimum_radius());

  // The entity that leaves and stays out accumulates one exposure tick per committed tick and is
  // eliminated on exactly the `G`th, which is the first tick it stops being published.
  std::uint64_t first_outside_tick = 0;
  std::uint64_t elimination_tick = 0;
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    const simulation::WorldSnapshot& snapshot = at_tick(snapshots, tick);
    if (first_outside_tick == 0 && exposure_of(snapshot, drifts_out).has_value()) {
      first_outside_tick = tick;
    }
    if (elimination_tick == 0 && first_outside_tick != 0 &&
        !body_of(snapshot, drifts_out).has_value()) {
      elimination_tick = tick;
    }
  }
  REQUIRE(first_outside_tick != 0);
  REQUIRE(elimination_tick != 0);
  CHECK(elimination_tick == first_outside_tick + grace - 1);
  // Its last published exposure is `G - 1`, because the increment that reaches `G` is the tick it
  // is destroyed on.
  CHECK(exposure_of(at_tick(snapshots, elimination_tick - 1), drifts_out) == grace - 1);

  const std::vector<simulation::RoyalePlacement> placements =
      placements_of(at_tick(snapshots, elimination_tick));
  REQUIRE(placements.size() == 1);
  CHECK(placements.front().entity == drifts_out);
  CHECK(placements.front().elimination_tick.value() == elimination_tick);
  // Two entities survive it, so `alive_after + 1` is 3.
  CHECK(placements.front().placement == 3);

  // The entity that leaves and comes back loses its partial grace: its counter climbs, disappears
  // on the tick its centre is inside again, and it is never named in the placement list.
  std::uint64_t highest_exposure = 0;
  std::uint64_t reset_tick = 0;
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    const std::optional<std::uint64_t> exposure = exposure_of(at_tick(snapshots, tick), returns);
    if (exposure.has_value()) {
      highest_exposure = *exposure;
      continue;
    }
    if (highest_exposure >= 2 && reset_tick == 0) {
      reset_tick = tick;
    }
  }
  CHECK(highest_exposure >= 2);
  REQUIRE(reset_tick != 0);
  CHECK(highest_exposure < grace);
  for (std::uint64_t tick = reset_tick; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    CHECK(body_of(at_tick(snapshots, tick), returns).has_value());
  }
  for (const simulation::RoyalePlacement& placement :
       placements_of(at_tick(snapshots, fixture.tick_count()))) {
    CHECK(placement.entity != returns);
  }
}

TEST_CASE("entities eliminated on one tick share one placement, and a mutual finish is a draw at "
          "placement one",
          "[fixtures][replay][royale][elimination][outcome]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-simultaneous-draw");
  const Snapshots snapshots = fixture.run();

  const simulation::EntityId first = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId second = fixture.spawned_entity_id(1, 1);
  const simulation::EntityId third = fixture.spawned_entity_id(1, 2);
  const simulation::EntityId fourth = fixture.spawned_entity_id(1, 3);

  const std::vector<simulation::RoyalePlacement> ranking =
      placements_of(at_tick(snapshots, fixture.tick_count()));
  REQUIRE(ranking.size() == 4);

  // Two entities leaving together share one placement, appended in ascending EntityId order.
  CHECK(ranking[0].entity == third);
  CHECK(ranking[1].entity == fourth);
  CHECK(ranking[0].placement == 3);
  CHECK(ranking[1].placement == 3);
  CHECK(ranking[0].elimination_tick == ranking[1].elimination_tick);

  // The final two share placement 1, because `alive_after` is zero, and the committed outcome is a
  // draw rather than a win.
  CHECK(ranking[2].entity == first);
  CHECK(ranking[3].entity == second);
  CHECK(ranking[2].placement == 1);
  CHECK(ranking[3].placement == 1);
  CHECK(ranking[2].elimination_tick == ranking[3].elimination_tick);
  CHECK(ranking[2].elimination_tick > ranking[0].elimination_tick);

  const std::uint64_t final_elimination_tick = ranking[2].elimination_tick.value();
  const simulation::WorldSnapshot& deciding = at_tick(snapshots, final_elimination_tick);
  // The elimination and the end it causes commit on the same tick, so the placement list a snapshot
  // carries always agrees with the alive count that snapshot reports.
  CHECK(alive_count_of(deciding) == 0);
  CHECK(deciding.match().phase() == simulation::MatchPhase::kEnded);
  CHECK(deciding.match().outcome() == simulation::MatchOutcome::drawn());
  CHECK(placements_of(deciding).size() == 4);

  // The list survives the whole `ended` phase and the `lobby` that follows it.
  CHECK(placements_of(at_tick(snapshots, fixture.tick_count())).size() == 4);
}

TEST_CASE("an all-zero duration configuration advances one phase per tick and cycles instead of "
          "hanging",
          "[fixtures][replay][royale][lifecycle]") {
  const testing::ReplayFixture fixture =
      testing::ReplayFixture::named("royale-transition-per-tick");
  REQUIRE(fixture.royale().countdown_ticks() == 0);
  REQUIRE(fixture.royale().restart_delay_ticks() == 0);
  REQUIRE(fixture.lobby_seat_count() == 1);
  const Snapshots snapshots = fixture.run();

  // Three complete cycles of the same five-tick period: one transition per tick through
  // `countdown -> running -> ended -> lobby`, then the single zero-alive `lobby` tick the restart
  // wipe exists to produce, then the next joiner.
  const std::vector<simulation::MatchPhase> expected{
      simulation::MatchPhase::kCountdown, simulation::MatchPhase::kRunning,
      simulation::MatchPhase::kEnded,     simulation::MatchPhase::kLobby,
      simulation::MatchPhase::kLobby,     simulation::MatchPhase::kCountdown,
      simulation::MatchPhase::kRunning,   simulation::MatchPhase::kEnded,
      simulation::MatchPhase::kLobby,     simulation::MatchPhase::kLobby,
      simulation::MatchPhase::kCountdown, simulation::MatchPhase::kRunning,
      simulation::MatchPhase::kEnded,     simulation::MatchPhase::kLobby,
      simulation::MatchPhase::kLobby};
  REQUIRE(snapshots.size() == expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    INFO("tick " << index + 1);
    CHECK(at_tick(snapshots, index + 1).match().phase() == expected[index]);
  }

  // The restart wipe leaves exactly one zero-alive `lobby` tick after every match, and the winner
  // of a one-player match receives no placement entry at all.
  CHECK(alive_count_of(at_tick(snapshots, 4)) == 1);
  CHECK(alive_count_of(at_tick(snapshots, 5)) == 0);
  CHECK(alive_count_of(at_tick(snapshots, 6)) == 1);
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    CHECK(placements_of(at_tick(snapshots, tick)).empty());
  }
  // Each match names its winner, and the second match's winner is a new EntityId under a new
  // controller rather than the survivor of the first.
  CHECK(at_tick(snapshots, 3).match().outcome() ==
        simulation::MatchOutcome::won_by_entity(fixture.spawned_entity_id(1, 0)));
  CHECK(at_tick(snapshots, 8).match().outcome() ==
        simulation::MatchOutcome::won_by_entity(fixture.spawned_entity_id(6, 0)));
}

TEST_CASE("a recorded shield pulse parries a closing attacker inside its perfect opening",
          "[fixtures][replay][royale][shield]") {
  // The replay-level half of human/bot/replay symmetry. A `shield` row in `commands.csv` is an
  // ordinary `ShieldCommand` in the tick's batch, so it reaches `ability`'s admission through the
  // same path a browser frame or a scripted controller does; nothing in the simulation can tell
  // which produced it. The fixture's own header explains the geometry and the timing.
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-shield-parry");
  const Snapshots snapshots = fixture.run();
  REQUIRE(snapshots.size() == fixture.tick_count());

  const simulation::EntityId attacker = fixture.spawned_entity_id(1, 0);
  const simulation::EntityId defender = fixture.spawned_entity_id(1, 1);
  REQUIRE(attacker < defender);

  // The pulse is admitted on the tick it was recorded on, and every published endpoint is the
  // authored default tuning converted once: 160, 32, and 360 ticks, with a captured 240-tick stun.
  const std::optional<simulation::Shield> raised = shield_of(at_tick(snapshots, 60), defender);
  REQUIRE(raised.has_value());
  CHECK(raised->activation_tick() == simulation::TickSequence::create(60));
  CHECK(raised->shield_window().expiry_tick() == simulation::TickSequence::create(220));
  CHECK(raised->perfect_window().expiry_tick() == simulation::TickSequence::create(92));
  CHECK(raised->cooldown_window().expiry_tick() == simulation::TickSequence::create(420));
  CHECK(raised->parry_stun_duration_ticks() == 240);
  // The attacker never pulsed, so it carries no shield at all: admission is per entity, not
  // per tick.
  CHECK_FALSE(shield_of(at_tick(snapshots, 60), attacker).has_value());
  // Nothing is guarded before the pulse, which is what makes tick 60 the activation rather than
  // the discovery of a shield the world already had.
  CHECK_FALSE(shield_of(at_tick(snapshots, 59), defender).has_value());

  // Locate the parry rather than hard-coding the contact tick, then assert the thing the fixture
  // exists to prove: it landed inside the opening, and it stunned the attacker for the duration
  // the DEFENDER captured.
  std::optional<std::uint64_t> parry_tick;
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    if (stun_of(at_tick(snapshots, tick), attacker).has_value()) {
      parry_tick = tick;
      break;
    }
  }
  REQUIRE(parry_tick.has_value());
  INFO("parry committed on tick " << *parry_tick);
  CHECK(raised->perfect_window().contains(simulation::TickSequence::create(*parry_tick)));

  const std::optional<simulation::Stun> stun = stun_of(at_tick(snapshots, *parry_tick), attacker);
  REQUIRE(stun.has_value());
  CHECK(stun->window.activation_tick() == simulation::TickSequence::create(*parry_tick));
  CHECK(stun->window.expiry_tick() == simulation::TickSequence::create(*parry_tick + 240));
  // "Negate momentum" means cancel it, not reverse it: the perfect response zeroes the incoming
  // body's velocity and acceleration, and the stun then holds it there.
  const std::optional<simulation::PhysicsBody> stopped =
      body_of(at_tick(snapshots, *parry_tick), attacker);
  REQUIRE(stopped.has_value());
  CHECK(stopped->velocity() == simulation::Vector2::create(0.0, 0.0));
  CHECK(stopped->acceleration() == simulation::Vector2::create(0.0, 0.0));

  // The defender is never the one stunned -- it did not ram anybody -- and neither body leaves the
  // match: a parry is a defensive outcome, not an elimination.
  CHECK_FALSE(stun_of(at_tick(snapshots, *parry_tick), defender).has_value());
  const simulation::WorldSnapshot& last = at_tick(snapshots, fixture.tick_count());
  CHECK(alive_count_of(last) == 2);
  CHECK(carries_controllable(last, attacker));
  CHECK(carries_controllable(last, defender));

  // The stun outlives the replay, so a held thrust never resumes inside it, and the guard is still
  // ordinary protection after its opening closed: one activation, three windows, one lifetime.
  CHECK(stun_of(last, attacker).has_value());
  const std::optional<simulation::PhysicsBody> held = body_of(last, attacker);
  REQUIRE(held.has_value());
  CHECK(held->acceleration() == simulation::Vector2::create(0.0, 0.0));
  const std::optional<simulation::Shield> guarding = shield_of(last, defender);
  REQUIRE(guarding.has_value());
  CHECK(*guarding == *raised);
  CHECK(guarding->shield_window().contains(simulation::TickSequence::create(fixture.tick_count())));
  CHECK_FALSE(
      guarding->perfect_window().contains(simulation::TickSequence::create(fixture.tick_count())));
}

TEST_CASE("the scripted multi-entity match runs a whole royale and ranks its losers",
          "[fixtures][replay][royale][match]") {
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-scripted-match");
  const Snapshots snapshots = fixture.run();
  REQUIRE(snapshots.size() == fixture.tick_count());

  const simulation::WorldSnapshot& last = at_tick(snapshots, fixture.tick_count());
  const std::vector<simulation::RoyalePlacement> ranking = placements_of(last);
  CHECK_FALSE(ranking.empty());

  // The ranking is ordered by elimination tick and, within one tick, by ascending EntityId; a
  // placement is one-based and never exceeds the roster the match started with.
  for (std::size_t index = 1; index < ranking.size(); ++index) {
    INFO("placement entry " << index);
    CHECK(ranking[index - 1].elimination_tick <= ranking[index].elimination_tick);
    if (ranking[index - 1].elimination_tick == ranking[index].elimination_tick) {
      CHECK(ranking[index - 1].entity < ranking[index].entity);
      CHECK(ranking[index - 1].placement == ranking[index].placement);
    } else {
      CHECK(ranking[index - 1].placement > ranking[index].placement);
    }
  }
  for (const simulation::RoyalePlacement& placement : ranking) {
    CHECK(placement.placement >= 1);
    CHECK(placement.placement <= 4);
  }

  // Every published tick carries a zone whose radius is between the configured floor and the
  // arena's circumscribed radius, and the zone entity is never a player.
  const double full_radius = gameplay::zone_full_radius(fixture.map().bounds());
  for (std::uint64_t tick = 1; tick <= fixture.tick_count(); ++tick) {
    INFO("tick " << tick);
    const std::optional<simulation::Zone> zone = zone_of(at_tick(snapshots, tick));
    REQUIRE(zone.has_value());
    CHECK(zone->radius >= fixture.royale().zone_minimum_radius());
    CHECK(zone->radius <= full_radius);
  }
}

TEST_CASE("100 fresh runs of the scripted multi-entity match produce bit-identical ordered "
          "snapshots",
          "[fixtures][replay][royale][determinism]") {
  // `docs/architecture/0003-deterministic-simulation-contract.md`'s 100-fresh-run rule, extended
  // for the first time from a physics horizon to a whole match: four entities, contacts, a wall
  // bounce, a shrinking zone, eliminations, and a committed outcome. Each run constructs a fresh
  // simulation from `(map, mode configuration, seed, command log)` and nothing else.
  const testing::ReplayFixture fixture = testing::ReplayFixture::named("royale-scripted-match");
  const Snapshots reference = fixture.run();
  REQUIRE(reference.size() == fixture.tick_count());

  std::size_t divergent_run = 0;
  std::size_t divergent_tick = 0;
  for (std::size_t run = 2; run <= 100; ++run) {
    const std::size_t tick = testing::first_divergent_tick(reference, fixture.run());
    if (tick != 0 && divergent_run == 0) {
      divergent_run = run;
      divergent_tick = tick;
    }
  }
  INFO("first divergent run " << divergent_run << " at tick " << divergent_tick);
  CHECK(divergent_run == 0);
  CHECK(divergent_tick == 0);
}
