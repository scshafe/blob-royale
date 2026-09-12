#include "shared/ability_system.hpp"

#include "commands/shield_command.hpp"
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
#include "spawn_seating.hpp"
#include "tick_window.hpp"

#include <catch2/catch_test_macros.hpp>

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

[[nodiscard]] const simulation::Shield* shield_of(const simulation::GameWorld& world,
                                                  const simulation::EntityId target = entity()) {
  return world.store<simulation::Shield>().find(target);
}

// Applies the system to a copy of a world that must refuse, and asserts the refusal cost nothing at
// all: no cooldown consumed, no queued activation, no component written, no event emitted. The copy
// is what lets a caller go on to prove the same world admits the pulse once the gate is lifted.
void check_refusal_changes_nothing(const simulation::GameWorld& before,
                                   const simulation::TickSequence at) {
  simulation::GameWorld world = before;
  const testing::TickHarness harness{at};
  ability()->apply(world, harness.context());
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
