#include "shared/ability_system.hpp"

#include "commands/charge_command.hpp"
#include "commands/shield_command.hpp"
#include "components/charge_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "events/elimination_event.hpp"
#include "gameplay_test_fixture.hpp"
#include "race/course_publisher_system.hpp"
#include "race/race_test_fixture.hpp"
#include "shared/ability_configuration.hpp"
#include "shared/match_reset_system.hpp"
#include "shared/respawn_system.hpp"
#include "simulation_limits.hpp"
#include "spawn_seating.hpp"
#include "tick_window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kEntity = 1;
constexpr std::uint64_t kController = 7;
constexpr std::uint64_t kStaticEntity = 2;
constexpr std::uint64_t kBodylessEntity = 3;
// The guard is raised on tick 7, so with the authored tuning protection is [7, 167), the perfect
// opening is [7, 39) and the cooldown is [7, 367).
constexpr std::uint64_t kActivation = 7;
constexpr std::uint64_t kProtectedTick = 100;
constexpr std::uint64_t kAfterShieldTick = 167;
constexpr std::uint64_t kAfterCooldownTick = 367;
constexpr std::uint64_t kStunDuration = 3;
constexpr std::uint64_t kFinishedCheckpointCount = 2;
constexpr double kRadius = 10.0;
constexpr double kSpawnX = 100.0;
constexpr double kSpawnY = 320.0;
constexpr double kStaticX = 500.0;
// The charge cooldown is 1.2 s, which is 480 ticks, so a charge admitted on tick 7 is refused
// through tick 486 and admitted again on 487.
constexpr std::uint64_t kAfterChargeCooldownTick = 487;
// The authored gain against the fixture world's ceiling: `MovementTuning::defaults()` is 400/600
// and 0.75 * 600 is 450 wu/s of additive burst per activation.
constexpr double kNormalTopSpeed = 600.0;
constexpr double kBurstSpeed = 450.0;
constexpr double kLateralSpeed = 200.0;
// One whole tick of charge cooldown, which is the shortest legal one: the key must be positive.
constexpr double kOneTickCooldownSeconds = 0.0025;

[[nodiscard]] simulation::EntityId entity(const std::uint64_t value = kEntity) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::TickSequence tick(const std::uint64_t value = kActivation) {
  return simulation::TickSequence::create(value);
}

[[nodiscard]] simulation::Vector2 zero() { return simulation::Vector2::create(0.0, 0.0); }

[[nodiscard]] gameplay::AbilityConfiguration tuning() {
  return gameplay::AbilityConfiguration::defaults();
}

[[nodiscard]] std::unique_ptr<const simulation::SimulationSystem> ability() {
  return gameplay::AbilitySystem::create(tuning());
}

// The authored section with only the two charge values a case needs to move. Every shield value
// stays at the default, so a charge case that fails has failed on the key it varied.
[[nodiscard]] gameplay::AbilityConfiguration charge_tuning(const double charge_cooldown_seconds,
                                                           const double safety_envelope_speed) {
  return gameplay::AbilityConfiguration::create(
      gameplay::AbilityConfiguration::kDefaultShieldDurationSeconds,
      gameplay::AbilityConfiguration::kDefaultShieldPerfectWindowSeconds,
      gameplay::AbilityConfiguration::kDefaultShieldCooldownSeconds,
      gameplay::AbilityConfiguration::kDefaultParryStunDurationSeconds, charge_cooldown_seconds,
      gameplay::AbilityConfiguration::kDefaultChargeSpeedFraction, safety_envelope_speed);
}

[[nodiscard]] std::unique_ptr<const simulation::SimulationSystem>
ability_with(const gameplay::AbilityConfiguration& configuration) {
  return gameplay::AbilitySystem::create(configuration);
}

[[nodiscard]] simulation::Shield raised(const std::uint64_t activation = kActivation) {
  return simulation::Shield::activate(
      tick(activation), tuning().shield_duration_ticks(), tuning().shield_perfect_window_ticks(),
      tuning().shield_cooldown_ticks(), tuning().parry_stun_duration_ticks());
}

// One live, controllable, dynamic body in a running match: the smallest world in which a pulse is
// admissible, so every refusal a case asserts is attributable to the one gate it varies.
[[nodiscard]] simulation::GameWorld running_world() {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      entity(),
      simulation::PhysicsBody::create(simulation::Vector2::create(kSpawnX, kSpawnY), zero(), zero())
          .with_radius(kRadius),
      simulation::ControllerId::create(kController)));
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_match().previous_phase = simulation::MatchPhase::kRunning;
  return world;
}

// Writes one entity's whole recorded list for the tick, so a case can also state what the system
// does with a list carrying more than the one command of each kind phase 0 would ever record.
void record_pulses(simulation::GameWorld& world, const simulation::EntityId target,
                   std::vector<simulation::Command> commands) {
  world.mutable_store<simulation::Controllable>().mutable_find(target)->commands_this_tick =
      std::move(commands);
}

// Records one pulse the way kernel phase 0 records it: onto the entity's own recorded list, with
// the generation the submitter stamped. Absence is the initial generation.
void record_pulse(simulation::GameWorld& world, const simulation::EntityId target,
                  const std::optional<simulation::TickSequence> generation = std::nullopt) {
  record_pulses(world, target, {simulation::ShieldCommand{target, generation}});
}

// The same, for the directed one-shot pulse. The direction is the client's whole say in the
// activation: the gain is the server's.
void record_charge(simulation::GameWorld& world, const simulation::EntityId target,
                   const simulation::Vector2& direction,
                   const std::optional<simulation::TickSequence> generation = std::nullopt) {
  record_pulses(world, target, {simulation::ChargeCommand{target, direction, generation}});
}

[[nodiscard]] simulation::Vector2 east() { return simulation::Vector2::create(1.0, 0.0); }

[[nodiscard]] const simulation::Shield* shield_of(const simulation::GameWorld& world,
                                                  const simulation::EntityId target = entity()) {
  return world.store<simulation::Shield>().find(target);
}

[[nodiscard]] const simulation::Charge* charge_of(const simulation::GameWorld& world,
                                                  const simulation::EntityId target = entity()) {
  return world.store<simulation::Charge>().find(target);
}

[[nodiscard]] simulation::Vector2 velocity_of(const simulation::GameWorld& world,
                                              const simulation::EntityId target = entity()) {
  return world.store<simulation::PhysicsBody>().find(target)->velocity();
}

void seed_velocity(simulation::GameWorld& world, const simulation::Vector2& velocity,
                   const simulation::EntityId target = entity()) {
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      target, world.store<simulation::PhysicsBody>().find(target)->with_velocity(velocity));
}

// Applies the system to a copy of a world that must refuse, and asserts the refusal cost nothing at
// all: no cooldown consumed, no queued activation, no component written, no event emitted. The copy
// is what lets a caller go on to prove the same world admits the pulse once the gate is lifted.
//
// **`REQUIRE_NOTHROW` is part of the assertion, not defensive test writing.** A throw escaping
// `AbilitySystem::apply` escapes `GameSimulation::step`, the runtime worker records a worker
// failure and returns, and the simulation thread stops permanently -- one client's refused pulse
// would end the match for the whole room. Every refusal in this file therefore proves both halves:
// nothing changed, and nothing was raised.
void check_refusal_changes_nothing(const simulation::GameWorld& before,
                                   const simulation::TickSequence at,
                                   const gameplay::AbilityConfiguration& configuration = tuning()) {
  simulation::GameWorld world = before;
  const testing::TickHarness harness{at};
  REQUIRE_NOTHROW(ability_with(configuration)->apply(world, harness.context()));
  CHECK(world == before);
}

} // namespace

TEST_CASE("an admitted pulse writes all three windows and the configured parry stun duration",
          "[unit][gameplay][ability][shield]") {
  simulation::GameWorld world = running_world();
  record_pulse(world, entity());
  const testing::TickHarness harness{tick()};
  const auto system = ability();
  CHECK(system->name() == "ability");
  system->apply(world, harness.context());
  const simulation::Shield* shield = shield_of(world);
  REQUIRE(shield != nullptr);
  // The three windows share the one positive activation, and every duration is the configuration's
  // rather than a literal, so a transposed argument would land the cooldown in the shield slot.
  CHECK(shield->activation_tick() == tick());
  CHECK(shield->shield_window() ==
        simulation::TickWindow::create(tick(), tuning().shield_duration_ticks()));
  CHECK(shield->perfect_window() ==
        simulation::TickWindow::create(tick(), tuning().shield_perfect_window_ticks()));
  CHECK(shield->cooldown_window() ==
        simulation::TickWindow::create(tick(), tuning().shield_cooldown_ticks()));
  // Captured on activation so the frozen defender a contact response reads later supplies the
  // effect it owns; a noncapturing response cannot hold the configuration.
  CHECK(shield->parry_stun_duration_ticks() == tuning().parry_stun_duration_ticks());
}

TEST_CASE("a pulse outside the running phase changes nothing at all",
          "[unit][gameplay][ability][shield][validation]") {
  for (const simulation::MatchPhase phase : simulation::kMatchPhases) {
    if (phase == simulation::MatchPhase::kRunning) {
      continue;
    }
    CAPTURE(simulation::match_phase_name(phase));
    simulation::GameWorld world = running_world();
    world.mutable_match().phase = phase;
    world.mutable_match().previous_phase = phase;
    record_pulse(world, entity());
    check_refusal_changes_nothing(world, tick());
  }
}

TEST_CASE("a pulse at the loaded initial tick changes nothing at all",
          "[unit][gameplay][ability][shield][validation]") {
  // Tick zero is the loaded initial state and no activation may claim it: the three windows share a
  // positive activation. Refusing here keeps a first-tick pulse a silent refusal rather than a
  // thrown SIMULATION.SHIELD_ACTIVATION_INVALID that would fail the whole tick.
  simulation::GameWorld world = running_world();
  record_pulse(world, entity());
  check_refusal_changes_nothing(world, simulation::TickSequence::zero());
}

TEST_CASE("a static body and a bodyless entity never activate an ability",
          "[unit][gameplay][ability][shield][validation]") {
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(kStaticEntity),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(kStaticX, kSpawnY)));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(kStaticEntity),
      simulation::Controllable{simulation::ControllerId::create(kStaticEntity)});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(kBodylessEntity),
      simulation::Controllable{simulation::ControllerId::create(kBodylessEntity)});
  record_pulse(world, entity(kStaticEntity));
  record_pulse(world, entity(kBodylessEntity));
  // Scenery cannot be guarded -- the guarded composition throws for a guarded static subject -- and
  // an entity with no body has nothing for a guard to protect; the two-store join skips the second
  // case without a null check of its own.
  check_refusal_changes_nothing(world, tick());
}

TEST_CASE("an active stun refuses a pulse through the canonical input lock",
          "[unit][gameplay][ability][shield][stun][validation]") {
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::Stun>().insert_or_assign(
      entity(), simulation::Stun{simulation::TickWindow::create(tick(), kStunDuration)});
  record_pulse(world, entity());
  check_refusal_changes_nothing(world, tick());
  // The same world one tick past the stun's expiry admits the same pulse, so the refusal was the
  // lock and not the presence of the component.
  const testing::TickHarness harness{tick(kActivation + kStunDuration)};
  ability()->apply(world, harness.context());
  REQUIRE(shield_of(world) != nullptr);
  CHECK(shield_of(world)->activation_tick() == tick(kActivation + kStunDuration));
}

TEST_CASE("a racer who has completed the published course cannot activate an ability",
          "[unit][gameplay][ability][shield][race][validation]") {
  const testing::TickHarness harness{tick(), testing::race_test_map()};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(kSpawnX, kSpawnY)});
  // The course must be published before admission reads the lock, which is why the ability system
  // is declared after `course_publisher` at kPreKernel (`race/course_publisher_system.hpp`).
  const gameplay::RaceCourse course =
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration());
  gameplay::CoursePublisherSystem::create(course, testing::race_test_configuration())
      ->apply(world, harness.context());
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      entity(), simulation::RaceProgress{kFinishedCheckpointCount});
  record_pulse(world, entity());
  const simulation::GameWorld finished = world;
  ability()->apply(world, harness.context());
  CHECK(world == finished);
  // An unfinished racer with the same retained progress component is admitted, so the gate is the
  // completed course and not the component's presence.
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      entity(), simulation::RaceProgress{kFinishedCheckpointCount - 1});
  ability()->apply(world, harness.context());
  CHECK(shield_of(world) != nullptr);
}

TEST_CASE("a pulse activates only on exact generation equality, absence included",
          "[unit][gameplay][ability][shield][validation]") {
  // Exact optional equality, the thrust-steering idiom. Absent matches absent -- that is the
  // initial generation -- and a pulse stamped before a missed stun invalidated input is stale and
  // cannot activate, whichever side of the comparison the absence sits on.
  simulation::GameWorld stale = running_world();
  stale.mutable_store<simulation::Controllable>().mutable_find(entity())->input_generation = tick();
  record_pulse(stale, entity());
  check_refusal_changes_nothing(stale, tick());

  simulation::GameWorld unstamped = running_world();
  record_pulse(unstamped, entity(), tick());
  check_refusal_changes_nothing(unstamped, tick());

  simulation::GameWorld matched = running_world();
  matched.mutable_store<simulation::Controllable>().mutable_find(entity())->input_generation =
      tick();
  record_pulse(matched, entity(), tick());
  const testing::TickHarness harness{tick()};
  ability()->apply(matched, harness.context());
  CHECK(shield_of(matched) != nullptr);

  // Absent against absent is the ordinary first activation and must match rather than be treated as
  // two unknowns that cannot be compared.
  simulation::GameWorld initial = running_world();
  record_pulse(initial, entity());
  ability()->apply(initial, harness.context());
  CHECK(shield_of(initial) != nullptr);
}

TEST_CASE("active protection and a live cooldown each refuse the next pulse",
          "[unit][gameplay][ability][shield][validation]") {
  simulation::GameWorld protecting = running_world();
  protecting.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  record_pulse(protecting, entity());
  check_refusal_changes_nothing(protecting, tick(kProtectedTick));

  // Protection has ended and the cooldown has not. The refusal must leave the cooldown exactly
  // where it was: a refused pulse consumes no cooldown, so it cannot push the next opening later.
  simulation::GameWorld cooling = running_world();
  cooling.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  record_pulse(cooling, entity());
  check_refusal_changes_nothing(cooling, tick(kAfterShieldTick));

  // One tick past the cooldown the same pulse is admitted, which is what makes the two cases above
  // gates rather than a system that never activates twice.
  const testing::TickHarness harness{tick(kAfterCooldownTick)};
  ability()->apply(cooling, harness.context());
  REQUIRE(shield_of(cooling) != nullptr);
  CHECK(shield_of(cooling)->activation_tick() == tick(kAfterCooldownTick));
}

TEST_CASE("the sweep removes a shield only once protection and cooldown have both expired",
          "[unit][gameplay][ability][shield]") {
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  const testing::TickHarness protecting{tick(kProtectedTick)};
  ability()->apply(world, protecting.context());
  CHECK(shield_of(world) != nullptr);
  // Protection over, cooldown live: the component stays, because the cooldown is the only thing
  // left that can refuse the next pulse and erasing it here would hand the ability back early.
  const testing::TickHarness cooling{tick(kAfterShieldTick)};
  ability()->apply(world, cooling.context());
  REQUIRE(shield_of(world) != nullptr);
  CHECK(*shield_of(world) == raised());
  const testing::TickHarness done{tick(kAfterCooldownTick)};
  ability()->apply(world, done.context());
  CHECK(shield_of(world) == nullptr);
}

TEST_CASE("empty cancelled protection with a live cooldown survives the sweep and still refuses",
          "[unit][gameplay][ability][shield][stun]") {
  // This is the state a stun leaves behind (`shared/status_system.hpp`): protection cancelled to
  // nothing while the cooldown runs on. Being stunned mid-guard must cost the guard, never the
  // cooldown, so the component has to outlive its own protection.
  simulation::GameWorld world = running_world();
  const simulation::Shield cancelled = raised().canceled_at(tick());
  world.mutable_store<simulation::Shield>().insert_or_assign(entity(), cancelled);
  record_pulse(world, entity());
  check_refusal_changes_nothing(world, tick(kProtectedTick));
  const testing::TickHarness harness{tick(kProtectedTick)};
  ability()->apply(world, harness.context());
  REQUIRE(shield_of(world) != nullptr);
  CHECK(*shield_of(world) == cancelled);
  CHECK(shield_of(world)->cooldown_window() == raised().cooldown_window());
}

TEST_CASE("body loss clears the shield through the body-bound trait and zero delay returns ready",
          "[unit][gameplay][ability][shield][respawn]") {
  // No per-owner cleanup: the shield is declared body-bound, so the shared sweep that follows body
  // erasure removes it exactly as it removes a stun, and this system contributes nothing to it.
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  world.emit(simulation::EliminationEvent{entity()});
  const testing::TickHarness harness{tick(kProtectedTick)};
  gameplay::RespawnSystem::create(0)->apply(world, harness.context());
  CHECK(world.store<simulation::Shield>().empty());
  CHECK(world.store<simulation::PhysicsBody>().find(entity()) == nullptr);
  // A zero-delay return leaves the ability ready rather than serving out the cooldown of a guard
  // whose body no longer exists.
  simulation::seat_body_at_rest(world, entity(), simulation::Vector2::create(kSpawnX, kSpawnY),
                                kRadius);
  record_pulse(world, entity());
  ability()->apply(world, harness.context());
  REQUIRE(shield_of(world) != nullptr);
  CHECK(shield_of(world)->activation_tick() == tick(kProtectedTick));
}

TEST_CASE("a round reset destroys the shield with its participant entity",
          "[unit][gameplay][ability][shield][reset]") {
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  world.mutable_match().phase = simulation::MatchPhase::kLobby;
  world.mutable_match().previous_phase = simulation::MatchPhase::kEnded;
  const testing::TickHarness harness{tick(kProtectedTick)};
  gameplay::MatchResetSystem::create()->apply(world, harness.context());
  CHECK_FALSE(world.contains(entity()));
  CHECK(world.store<simulation::Shield>().empty());
}

TEST_CASE("one entity makes at most one activation attempt per tick",
          "[unit][gameplay][ability][shield]") {
  // Phase 0 records at most one command of each kind per entity, and this system reads the first
  // recorded pulse without sorting the list. A list that somehow carried two therefore still makes
  // one attempt: the second is never read, whichever way round the two are written.
  simulation::GameWorld coalesced = running_world();
  record_pulses(coalesced, entity(),
                {simulation::ShieldCommand{entity(), std::nullopt},
                 simulation::ShieldCommand{entity(), tick()}});
  const testing::TickHarness harness{tick()};
  ability()->apply(coalesced, harness.context());
  REQUIRE(shield_of(coalesced) != nullptr);
  CHECK(shield_of(coalesced)->activation_tick() == tick());
  CHECK(coalesced.store<simulation::Shield>().size() == 1);

  // Reversed, the stale pulse is the one read and the whole tick is a refusal: the second pulse is
  // never a second attempt, so nothing recovers the activation the first one lost.
  simulation::GameWorld stale_first = running_world();
  record_pulses(stale_first, entity(),
                {simulation::ShieldCommand{entity(), tick()},
                 simulation::ShieldCommand{entity(), std::nullopt}});
  const simulation::GameWorld before = stale_first;
  ability()->apply(stale_first, harness.context());
  CHECK(stale_first == before);
}

TEST_CASE("an admitted charge publishes one cooldown and adds the burst to the body's velocity",
          "[unit][gameplay][ability][charge]") {
  simulation::GameWorld world = running_world();
  record_charge(world, entity(), east());
  const testing::TickHarness harness{tick()};
  ability()->apply(world, harness.context());
  const simulation::Charge* charge = charge_of(world);
  REQUIRE(charge != nullptr);
  // One window and no other: charge is one-shot, so there is no active window to publish and no
  // captured effect parameter. The cooldown is the whole committed state.
  CHECK(charge->activation_tick() == tick());
  CHECK(charge->cooldown_window() ==
        simulation::TickWindow::create(tick(), tuning().charge_cooldown_ticks()));
  // The gain is the configuration's fraction of the *current* ceiling, never an authored speed.
  REQUIRE(world.match().movement.current.normal_top_speed() == kNormalTopSpeed);
  CHECK(velocity_of(world) ==
        simulation::Vector2::create(tuning().charge_speed_fraction() * kNormalTopSpeed, 0.0));
  CHECK(velocity_of(world) == simulation::Vector2::create(kBurstSpeed, 0.0));
  // The burst touches velocity and nothing else: no acceleration, no position, no radius, no mass.
  // It is not a teleport and it is not a change of what the body can collide with.
  const simulation::PhysicsBody* body = world.store<simulation::PhysicsBody>().find(entity());
  CHECK(body->acceleration() == zero());
  CHECK(body->position() == simulation::Vector2::create(kSpawnX, kSpawnY));
  CHECK(body->radius() == kRadius);
  // No shield was written, so the two abilities are genuinely separate components.
  CHECK(shield_of(world) == nullptr);
}

TEST_CASE("the charge burst is additive, so lateral velocity survives it",
          "[unit][gameplay][ability][charge]") {
  // ADR 0008's charge "never erases lateral velocity". A body already crossing the arena is
  // deflected by the burst rather than snapped onto the direction it charged in.
  simulation::GameWorld world = running_world();
  seed_velocity(world, simulation::Vector2::create(0.0, kLateralSpeed));
  record_charge(world, entity(), east());
  const testing::TickHarness harness{tick()};
  ability()->apply(world, harness.context());
  REQUIRE(charge_of(world) != nullptr);
  CHECK(velocity_of(world) == simulation::Vector2::create(kBurstSpeed, kLateralSpeed));
  // Explicitly not the replacing answer, which is the bug this case exists to catch.
  CHECK(velocity_of(world) != simulation::Vector2::create(kBurstSpeed, 0.0));
}

TEST_CASE("charge strength is the server's and the direction is the client's only say",
          "[unit][gameplay][ability][charge]") {
  // A subunit direction is legal on the wire -- the payload reuses `set_thrust`'s per-component
  // unit bound -- and must produce the *same* burst as a unit one. Routed through the magnitude
  // clamp `normalized_thrust_intent` instead, this pulse would deliver half the burst and pointer
  // distance would become strength (`shared/locomotion.hpp`).
  simulation::GameWorld subunit = running_world();
  record_charge(subunit, entity(), simulation::Vector2::create(0.5, 0.0));
  const testing::TickHarness harness{tick()};
  ability()->apply(subunit, harness.context());
  REQUIRE(charge_of(subunit) != nullptr);
  CHECK(velocity_of(subunit) == simulation::Vector2::create(kBurstSpeed, 0.0));

  // A longer direction is normalized rather than scaled up, and it aims where it points: (3, 4)
  // has an exact magnitude of five, so the components are exact.
  simulation::GameWorld oversize = running_world();
  record_charge(oversize, entity(), simulation::Vector2::create(3.0, 4.0));
  ability()->apply(oversize, harness.context());
  REQUIRE(charge_of(oversize) != nullptr);
  CHECK(velocity_of(oversize) == simulation::Vector2::create(0.6 * kBurstSpeed, 0.8 * kBurstSpeed));
  const simulation::Vector2 committed = velocity_of(oversize);
  CHECK(std::sqrt(committed.dot(committed)) == kBurstSpeed);
}

TEST_CASE("a zero or subnormal charge direction changes nothing at all",
          "[unit][gameplay][ability][charge][validation]") {
  // `1e-200` is the case a client can actually send: it passes the decoder and `InputBatch`, and
  // its squared magnitude underflows to zero. There is no unit direction to charge along, so the
  // pulse is a silent refusal -- not a zero-length burst, and not a thrown tick.
  for (const auto& direction :
       {simulation::Vector2::create(0.0, 0.0), simulation::Vector2::create(-0.0, 0.0),
        simulation::Vector2::create(1e-200, 0.0), simulation::Vector2::create(0.0, -1e-200),
        simulation::Vector2::create(1e-200, 1e-200)}) {
    CAPTURE(direction.x(), direction.y());
    simulation::GameWorld world = running_world();
    record_charge(world, entity(), direction);
    check_refusal_changes_nothing(world, tick());
  }
}

TEST_CASE("a charge outside the running phase changes nothing at all",
          "[unit][gameplay][ability][charge][validation]") {
  for (const simulation::MatchPhase phase : simulation::kMatchPhases) {
    if (phase == simulation::MatchPhase::kRunning) {
      continue;
    }
    CAPTURE(simulation::match_phase_name(phase));
    simulation::GameWorld world = running_world();
    world.mutable_match().phase = phase;
    world.mutable_match().previous_phase = phase;
    record_charge(world, entity(), east());
    check_refusal_changes_nothing(world, tick());
  }
}

TEST_CASE("a charge at the loaded initial tick changes nothing at all",
          "[unit][gameplay][ability][charge][validation]") {
  // Tick zero is the loaded initial state: `Charge` dates its cooldown from a positive activation
  // and rejects a zero one, so refusing here keeps a first-tick pulse silent rather than thrown.
  simulation::GameWorld world = running_world();
  record_charge(world, entity(), east());
  check_refusal_changes_nothing(world, simulation::TickSequence::zero());
}

TEST_CASE("a static body and a bodyless entity never charge",
          "[unit][gameplay][ability][charge][validation]") {
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      entity(kStaticEntity),
      simulation::PhysicsBody::create_static(simulation::Vector2::create(kStaticX, kSpawnY)));
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(kStaticEntity),
      simulation::Controllable{simulation::ControllerId::create(kStaticEntity)});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      entity(kBodylessEntity),
      simulation::Controllable{simulation::ControllerId::create(kBodylessEntity)});
  record_charge(world, entity(kStaticEntity), east());
  record_charge(world, entity(kBodylessEntity), east());
  // Scenery is not launched by writing a velocity onto it, and an entity with no body has no
  // velocity to add a burst to; the two-store join skips the second case with no null check here.
  check_refusal_changes_nothing(world, tick());
}

TEST_CASE("an active stun refuses a charge through the canonical input lock",
          "[unit][gameplay][ability][charge][stun][validation]") {
  simulation::GameWorld world = running_world();
  world.mutable_store<simulation::Stun>().insert_or_assign(
      entity(), simulation::Stun{simulation::TickWindow::create(tick(), kStunDuration)});
  record_charge(world, entity(), east());
  check_refusal_changes_nothing(world, tick());
  // The same world one tick past the stun's expiry admits the same pulse, so the refusal was the
  // lock and not the presence of the component.
  const testing::TickHarness harness{tick(kActivation + kStunDuration)};
  ability()->apply(world, harness.context());
  REQUIRE(charge_of(world) != nullptr);
  CHECK(charge_of(world)->activation_tick() == tick(kActivation + kStunDuration));
  CHECK(velocity_of(world) == simulation::Vector2::create(kBurstSpeed, 0.0));
}

TEST_CASE("a racer who has completed the published course cannot charge",
          "[unit][gameplay][ability][charge][race][validation]") {
  const testing::TickHarness harness{tick(), testing::race_test_map()};
  simulation::GameWorld world =
      testing::race_test_world({simulation::Vector2::create(kSpawnX, kSpawnY)});
  const gameplay::RaceCourse course =
      gameplay::RaceCourse::create(harness.map(), testing::race_test_configuration());
  gameplay::CoursePublisherSystem::create(course, testing::race_test_configuration())
      ->apply(world, harness.context());
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      entity(), simulation::RaceProgress{kFinishedCheckpointCount});
  record_charge(world, entity(), east());
  const simulation::GameWorld finished = world;
  REQUIRE_NOTHROW(ability()->apply(world, harness.context()));
  CHECK(world == finished);
  // An unfinished racer with the same retained progress component is admitted, so the gate is the
  // completed course and not the component's presence.
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      entity(), simulation::RaceProgress{kFinishedCheckpointCount - 1});
  ability()->apply(world, harness.context());
  CHECK(charge_of(world) != nullptr);
}

TEST_CASE("a charge activates only on exact generation equality, absence included",
          "[unit][gameplay][ability][charge][validation]") {
  // Exact optional equality, the same rule the shield pulse obeys. A directed pulse is no more
  // trusted than an undirected one: a charge stamped before a missed stun invalidated input is
  // stale and cannot activate, whichever side of the comparison the absence sits on.
  simulation::GameWorld stale = running_world();
  stale.mutable_store<simulation::Controllable>().mutable_find(entity())->input_generation = tick();
  record_charge(stale, entity(), east());
  check_refusal_changes_nothing(stale, tick());

  simulation::GameWorld unstamped = running_world();
  record_charge(unstamped, entity(), east(), tick());
  check_refusal_changes_nothing(unstamped, tick());

  simulation::GameWorld matched = running_world();
  matched.mutable_store<simulation::Controllable>().mutable_find(entity())->input_generation =
      tick();
  record_charge(matched, entity(), east(), tick());
  const testing::TickHarness harness{tick()};
  ability()->apply(matched, harness.context());
  CHECK(charge_of(matched) != nullptr);

  simulation::GameWorld initial = running_world();
  record_charge(initial, entity(), east());
  ability()->apply(initial, harness.context());
  CHECK(charge_of(initial) != nullptr);
}

TEST_CASE("a live charge cooldown refuses the next charge and the sweep clears an expired one",
          "[unit][gameplay][ability][charge][validation]") {
  simulation::GameWorld world = running_world();
  record_charge(world, entity(), east());
  const testing::TickHarness first{tick()};
  ability()->apply(world, first.context());
  REQUIRE(charge_of(world) != nullptr);
  const simulation::Charge committed = *charge_of(world);

  // Mid-cooldown: the pulse is refused and the cooldown does not move. A refused charge consumes
  // no cooldown, so it cannot push the next opening later.
  record_charge(world, entity(), east());
  check_refusal_changes_nothing(world, tick(kProtectedTick));

  // One tick before expiry is still a refusal; the very tick of expiry is an admission, which is
  // what makes the window half-open rather than inclusive.
  check_refusal_changes_nothing(world, tick(kAfterChargeCooldownTick - 1));
  CHECK(*charge_of(world) == committed);
  const testing::TickHarness ready{tick(kAfterChargeCooldownTick)};
  ability()->apply(world, ready.context());
  REQUIRE(charge_of(world) != nullptr);
  CHECK(charge_of(world)->activation_tick() == tick(kAfterChargeCooldownTick));
  // Two bursts, both additive, so the body is travelling at twice the gain.
  CHECK(velocity_of(world) == simulation::Vector2::create(2.0 * kBurstSpeed, 0.0));

  // With no pulse at all, an expired cooldown is swept: a `Charge` carries one window and nothing
  // that has to outlive it.
  simulation::GameWorld sweeping = running_world();
  record_charge(sweeping, entity(), east());
  ability()->apply(sweeping, first.context());
  REQUIRE(charge_of(sweeping) != nullptr);
  record_pulses(sweeping, entity(), {});
  const testing::TickHarness expired{tick(kAfterChargeCooldownTick)};
  ability()->apply(sweeping, expired.context());
  CHECK(charge_of(sweeping) == nullptr);
}

TEST_CASE("active protection refuses a charge, and a live shield cooldown does not",
          "[unit][gameplay][ability][charge][shield][validation]") {
  // A body may not charge out of its own live guard, which is the one state the two abilities
  // share. The *cooldowns* are separate keys on separate components and neither gates the other.
  simulation::GameWorld guarded = running_world();
  guarded.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  record_charge(guarded, entity(), east());
  check_refusal_changes_nothing(guarded, tick(kProtectedTick));

  // Protection over, shield cooldown still running: the charge is admitted, because the gate is
  // `!protection_active` and not "no Shield present" or "the shield is available".
  const testing::TickHarness cooling{tick(kAfterShieldTick)};
  ability()->apply(guarded, cooling.context());
  REQUIRE(charge_of(guarded) != nullptr);
  CHECK(velocity_of(guarded) == simulation::Vector2::create(kBurstSpeed, 0.0));
  // And the shield the charge stepped past is untouched: same activation, same cooldown.
  REQUIRE(shield_of(guarded) != nullptr);
  CHECK(*shield_of(guarded) == raised());
}

TEST_CASE("when a shield and a charge are both eligible the shield wins and charge pays nothing",
          "[unit][gameplay][ability][charge][shield][conflict]") {
  simulation::GameWorld world = running_world();
  record_pulses(world, entity(),
                {simulation::ShieldCommand{entity(), std::nullopt},
                 simulation::ChargeCommand{entity(), east(), std::nullopt}});
  const testing::TickHarness harness{tick()};
  ability()->apply(world, harness.context());
  // The shield committed.
  REQUIRE(shield_of(world) != nullptr);
  CHECK(shield_of(world)->activation_tick() == tick());
  // The charge did not, and losing the tie cost it nothing: no `Charge`, so no cooldown, and no
  // burst. The very next tick, with the guard up, is still a refusal -- but for the protection,
  // not for a cooldown the charge never consumed.
  CHECK(charge_of(world) == nullptr);
  CHECK(velocity_of(world) == zero());

  // The proof that no cooldown was consumed: once the guard ends, the same entity charges on the
  // first tick it is allowed to, with no wait carried over from the tie it lost.
  record_charge(world, entity(), east());
  const testing::TickHarness after{tick(kAfterShieldTick)};
  ability()->apply(world, after.context());
  REQUIRE(charge_of(world) != nullptr);
  CHECK(charge_of(world)->activation_tick() == tick(kAfterShieldTick));
  CHECK(velocity_of(world) == simulation::Vector2::create(kBurstSpeed, 0.0));
}

TEST_CASE("an ineligible shield pulse does not suppress an eligible charge",
          "[unit][gameplay][ability][charge][shield][conflict]") {
  // The conflict gate is spelled `!shield_eligible`, not "no Shield present" and not "no shield
  // pulse was sent". A shield pulse that could not have activated anyway must therefore cost the
  // charge nothing at all.
  //
  // First clause: the shield is on cooldown. Protection ended at 167 and the cooldown runs to 367.
  simulation::GameWorld cooling = running_world();
  cooling.mutable_store<simulation::Shield>().insert_or_assign(entity(), raised());
  record_pulses(cooling, entity(),
                {simulation::ShieldCommand{entity(), std::nullopt},
                 simulation::ChargeCommand{entity(), east(), std::nullopt}});
  const testing::TickHarness after_shield{tick(kAfterShieldTick)};
  ability()->apply(cooling, after_shield.context());
  REQUIRE(charge_of(cooling) != nullptr);
  CHECK(charge_of(cooling)->activation_tick() == tick(kAfterShieldTick));
  CHECK(velocity_of(cooling) == simulation::Vector2::create(kBurstSpeed, 0.0));
  // The ineligible shield pulse activated nothing, so the committed guard is still the seeded one.
  REQUIRE(shield_of(cooling) != nullptr);
  CHECK(*shield_of(cooling) == raised());

  // Second clause: the shield pulse is stale while the charge pulse is current. Phase 0 records at
  // most one command of each kind and each carries the generation its own submitter stamped, so
  // the two can disagree.
  simulation::GameWorld stale_shield = running_world();
  stale_shield.mutable_store<simulation::Controllable>().mutable_find(entity())->input_generation =
      tick();
  record_pulses(stale_shield, entity(),
                {simulation::ShieldCommand{entity(), std::nullopt},
                 simulation::ChargeCommand{entity(), east(), tick()}});
  const testing::TickHarness harness{tick()};
  ability()->apply(stale_shield, harness.context());
  CHECK(shield_of(stale_shield) == nullptr);
  REQUIRE(charge_of(stale_shield) != nullptr);
  CHECK(velocity_of(stale_shield) == simulation::Vector2::create(kBurstSpeed, 0.0));
}

TEST_CASE("the safety envelope refuses a burst that would leave it and admits one landing on it",
          "[unit][gameplay][ability][charge][validation]") {
  // 20,000 wu/s is the authored envelope. A body at 19,800 would reach 20,250, which is refused
  // outright rather than converted into a shorter burst: ADR 0008 contrasts refusal with
  // *conversion*, and a clamped charge would be a strength the server invented.
  simulation::GameWorld beyond = running_world();
  seed_velocity(beyond,
                simulation::Vector2::create(tuning().charge_safety_envelope_speed() - 200.0, 0.0));
  record_charge(beyond, entity(), east());
  check_refusal_changes_nothing(beyond, tick());

  // Landing exactly on the envelope is admitted: the bound is `<=`, so the refusal covers only a
  // burst that would carry the body past it.
  simulation::GameWorld exact = running_world();
  seed_velocity(exact, simulation::Vector2::create(
                           tuning().charge_safety_envelope_speed() - kBurstSpeed, 0.0));
  record_charge(exact, entity(), east());
  const testing::TickHarness harness{tick()};
  ability()->apply(exact, harness.context());
  REQUIRE(charge_of(exact) != nullptr);
  CHECK(velocity_of(exact) ==
        simulation::Vector2::create(tuning().charge_safety_envelope_speed(), 0.0));

  // The envelope is a *magnitude* bound, not a per-axis one: a body already at the envelope
  // sideways is refused a burst along a perpendicular axis, which no component test would catch.
  simulation::GameWorld sideways = running_world();
  seed_velocity(sideways,
                simulation::Vector2::create(0.0, tuning().charge_safety_envelope_speed()));
  record_charge(sideways, entity(), east());
  check_refusal_changes_nothing(sideways, tick());
}

TEST_CASE("the envelope is checked in raw doubles before any Vector2 is built",
          "[unit][gameplay][ability][charge][validation]") {
  // The refusal that would otherwise be a dead room. With the envelope authored at the component
  // domain itself, a body already at the domain edge would sum to 1e12 + 450 -- a value
  // `Vector2::create` refuses with SIMULATION.PHYSICAL_SCALAR_OUT_OF_RANGE. That throw would escape
  // `AbilitySystem::apply` and `GameSimulation::step`, the runtime worker would record a worker
  // failure and return, and the simulation thread would stop permanently. Checking the sum in raw
  // doubles first turns it into a silent refusal instead, which is why this case asserts NOTHROW
  // and not just "the world is unchanged".
  const gameplay::AbilityConfiguration domain_envelope =
      charge_tuning(gameplay::AbilityConfiguration::kDefaultChargeCooldownSeconds,
                    simulation::kMaximumPhysicalComponentMagnitude);
  simulation::GameWorld world = running_world();
  seed_velocity(world,
                simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude, 0.0));
  record_charge(world, entity(), east());
  check_refusal_changes_nothing(world, tick(), domain_envelope);
}

TEST_CASE("repeated charges at zero drag converge on the safety envelope rather than growing",
          "[unit][gameplay][ability][charge][validation]") {
  // **The envelope, not drag, is what bounds repeated charges.** `drag_per_second=0` is the
  // checked-in value, and at zero drag the burst never decays -- so without the envelope a body
  // could charge every cooldown forever and grow without bound. This case drives the system
  // directly, which is exactly the zero-drag case: nothing between activations reduces velocity.
  //
  // The cooldown is one tick and the envelope is the smallest the cross-key rule permits beside the
  // authored 0.75 gain, so the ceiling is reached inside the loop rather than after four hundred
  // activations.
  constexpr double kSmallestLegalEnvelope =
      gameplay::AbilityConfiguration::kDefaultChargeSpeedFraction *
      simulation::kMaximumNormalTopSpeed;
  const gameplay::AbilityConfiguration rapid =
      charge_tuning(kOneTickCooldownSeconds, kSmallestLegalEnvelope);
  REQUIRE(rapid.charge_cooldown_ticks() == 1);

  simulation::GameWorld world = running_world();
  const auto system = ability_with(rapid);
  double greatest_speed = 0.0;
  std::uint64_t admitted = 0;
  for (std::uint64_t at = 1; at <= 50; ++at) {
    record_charge(world, entity(), east());
    const testing::TickHarness harness{tick(at)};
    REQUIRE_NOTHROW(system->apply(world, harness.context()));
    const simulation::Vector2 velocity = velocity_of(world);
    const double speed = std::sqrt(velocity.dot(velocity));
    if (speed > greatest_speed) {
      ++admitted;
      greatest_speed = speed;
    }
    CAPTURE(at, speed);
    REQUIRE(speed <= kSmallestLegalEnvelope);
  }
  // Sixteen activations of 450 wu/s reach 7,200; a seventeenth would be 7,650 and is refused for
  // the rest of the run, so the sequence converges instead of diverging.
  CHECK(admitted == 16);
  CHECK(greatest_speed == 16.0 * kBurstSpeed);
  CHECK(velocity_of(world) == simulation::Vector2::create(16.0 * kBurstSpeed, 0.0));
  // The last thirty-four ticks were refusals, so no cooldown was consumed by them and no `Charge`
  // is left standing once the final one expires.
  CHECK(charge_of(world) == nullptr);
}

TEST_CASE("a charge cooldown survives a stun", "[unit][gameplay][ability][charge][stun]") {
  // Being stunned costs the effect, never the wait -- the same rule that keeps a shield's cooldown
  // running through a cancellation. `StatusSystem` needs no charge arm to make this true: it
  // zeroes intent and acceleration and never touches velocity or a cooldown.
  simulation::GameWorld world = running_world();
  record_charge(world, entity(), east());
  const testing::TickHarness activating{tick()};
  ability()->apply(world, activating.context());
  REQUIRE(charge_of(world) != nullptr);
  const simulation::Charge committed = *charge_of(world);
  const simulation::Vector2 launched = velocity_of(world);

  world.mutable_store<simulation::Stun>().insert_or_assign(
      entity(),
      simulation::Stun{simulation::TickWindow::create(tick(kProtectedTick), kStunDuration)});
  record_charge(world, entity(), east());
  // Stunned: refused by the input lock, and the burst already in flight keeps flying.
  check_refusal_changes_nothing(world, tick(kProtectedTick));
  // Past the stun and still inside the cooldown: refused by the cooldown the stun did not reset.
  check_refusal_changes_nothing(world, tick(kProtectedTick + kStunDuration));
  CHECK(*charge_of(world) == committed);
  CHECK(velocity_of(world) == launched);
  // The cooldown expires on the tick it always would have, unmoved by the stun in the middle.
  const testing::TickHarness ready{tick(kAfterChargeCooldownTick)};
  ability()->apply(world, ready.context());
  REQUIRE(charge_of(world) != nullptr);
  CHECK(charge_of(world)->activation_tick() == tick(kAfterChargeCooldownTick));
}
