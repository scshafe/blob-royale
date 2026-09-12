#include "shared/ability_system.hpp"

#include "arena_bounds.hpp"
#include "command_registry.hpp"
#include "commands/shield_command.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_mode_registry.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "gameplay_test_fixture.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/ability_configuration.hpp"
#include "simulation_limits.hpp"
#include "terrain_definition.hpp"
#include "tick_sequence.hpp"
#include "tick_window.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

// canonical: shield_symmetry_tests -- one shield admission, whoever pressed and whichever game is
// being played.
//
// This file mirrors no single source, because the invariant is not a property of one class. Two
// separate claims meet here and nowhere else:
//
//  1. **Nothing about the source reaches the admission.** A pulse arrives at
//     `Controllable::commands_this_tick` as a `simulation::Command` value with the same shape
//     whether a network session, an in-process bot, or a scripted replay produced it, and the only
//     trace of provenance left in the world is `Controllable::controller_id`, which ADR 0004
//     § "Controllers" says the tick stores and never branches on. `AbilitySystem` is the newest
//     reader of that component, so the invariant needs re-proving against it rather than assuming.
//  2. **Every mode that fields the ability admits the same pulse the same way.** Shield is declared
//     by all four registered modes, which means four different pipelines, four different objectives
//     and four different contact tables all reach one admission
//     (`docs/reviews/2026-09-12-shield-composition-contract.md` § "Command, admission, and
//     lifecycle": "All four gameplay modes declare the system and advertise shield only with its
//     implementation").
//
// **Why this argument lives here and not with the controllers.**
// `tests/unit/controllers/human_bot_symmetry_tests.cpp` owns the end-to-end half -- a hosted
// `ScriptedReplayController` and a direct `CommandSink` call producing indistinguishable commands
// through one real sandbox -- and that is where the human/bot/replay argument belongs. It cannot
// own this half: its fixture is a sandbox game by construction, a stun cannot be seeded into a
// world it exposes as `const`, and a cross-mode claim asserted in `unit.controllers` would be
// absent from the core `unit.gameplay` lane the step's primary gate runs
// (`.claude/plans/2026-09-10-dynamic-arenas-and-combat.md` Step 18 "Verify"). One argument, two
// homes, split where the evidence is reachable rather than where the words match.
namespace {

// Two seated bodies whose only difference is the durable identity behind them: the lowest
// controller id a sink ever issues, and one far above every id these fixtures otherwise use. A
// large gap rather than adjacent values, so an admission that accidentally ordered or compared
// identities would have somewhere to go wrong.
inline constexpr std::uint64_t kFirstEntity = 1;
inline constexpr std::uint64_t kSecondEntity = 2;
inline constexpr std::uint64_t kLowController = simulation::kMinimumControllerId;
inline constexpr std::uint64_t kHighController = simulation::kMinimumControllerId + 4'096;

// The tick every direct-system case admits on. Positive, because tick zero is the loaded initial
// state and no activation may claim it.
inline constexpr std::uint64_t kAdmissionTick = 5;

// A seeded shield's activation, one tick before the pulse under test, so both its protection and
// its cooldown are dated before the tick that must be refused.
inline constexpr std::uint64_t kSeededActivationTick = kAdmissionTick - 1;
inline constexpr std::uint64_t kOneTick = 1;
inline constexpr std::uint64_t kLongCooldownTicks = 100;
inline constexpr std::uint64_t kLongProtectionTicks = 100;

// The shared road every seeded body rests on, and the widths that let one map satisfy four modes.
inline constexpr double kRoadY = 320.0;
inline constexpr double kRoadHalfWidth = 70.0;

// The ids a mode's own first-tick systems may take. Above the two seeded bodies with room to
// spare: royale creates its zone entity and the hill mode its hill entity on the tick they first
// observe none, and a tick that was handed no reservation may create nothing.
inline constexpr std::uint64_t kFirstReservedEntityId = 1'000;
inline constexpr std::uint64_t kReservedEntityIdCount = 32;

[[nodiscard]] simulation::Vector2 on_road(const double x) {
  return simulation::Vector2::create(x, kRoadY);
}

[[nodiscard]] simulation::EntityId entity(const std::uint64_t value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] simulation::TickSequence tick(const std::uint64_t value) {
  return simulation::TickSequence::create(value);
}

// One entity's pulse. The default token is the absent one, which is the initial generation of an
// entity whose input has never been invalidated.
[[nodiscard]] simulation::Command
shield_for(const std::uint64_t id, const std::optional<simulation::TickSequence> generation = {}) {
  return simulation::Command{
      simulation::ShieldCommand{.entity = entity(id), .input_generation = generation}};
}

// Two live dynamic bodies at rest on the road, seated to two different controllers. Everything
// else about them is identical -- same shape, same velocity, same acceleration, same ground
// attachment, same absent generation -- so any difference in what the admission does to them could
// only have come from the identity.
[[nodiscard]] simulation::GameWorld
seated_pair(const std::uint64_t first_controller, const std::uint64_t second_controller,
            const simulation::MatchPhase phase = simulation::MatchPhase::kRunning) {
  const auto body = [](const double x) {
    return simulation::PhysicsBody::create(on_road(x), simulation::Vector2::create(0.0, 0.0),
                                           simulation::Vector2::create(0.0, 0.0))
        .with_ground_attachment(simulation::GroundAttachment::kGroundBound);
  };
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      entity(kFirstEntity), body(100.0), simulation::ControllerId::create(first_controller)));
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      entity(kSecondEntity), body(200.0), simulation::ControllerId::create(second_controller)));
  simulation::GameWorld world = simulation::GameWorld::create(std::move(seeds));
  world.mutable_match().phase = phase;
  world.mutable_match().previous_phase = phase;
  return world;
}

// Records one pulse for each entity, exactly as kernel phase 0 records an accepted command onto the
// entity it addresses. A direct-system case writes here rather than handing over an `InputBatch`
// because `TickContext` carries none: a system reads its tick's input only through
// `Controllable::commands_this_tick` (`src/simulation/tick_context.hpp`).
void record_pulses(simulation::GameWorld& world,
                   const std::optional<simulation::TickSequence> generation = {}) {
  for (const std::uint64_t id : {kFirstEntity, kSecondEntity}) {
    world.mutable_store<simulation::Controllable>().mutable_find(entity(id))->commands_this_tick = {
        shield_for(id, generation)};
  }
}

// The stored activation for one entity, or nullptr when the world holds none.
[[nodiscard]] const simulation::Shield* stored_shield(const simulation::GameWorld& world,
                                                      const std::uint64_t id) {
  return world.store<simulation::Shield>().find(entity(id));
}

// The published activation for one entity, which is the only proof a pulse became a shield: queue
// acceptance and a local send are not confirmation, and this step adds no per-request negative
// receipt (`src/gameplay/shared/ability_system.hpp`).
[[nodiscard]] std::optional<simulation::Shield>
published_shield(const simulation::WorldSnapshot& snapshot, const std::uint64_t id) {
  for (const simulation::ComponentStore<simulation::Shield>::Entry& entry :
       snapshot.components<simulation::Shield>()) {
    if (entry.entity == entity(id)) {
      return entry.value;
    }
  }
  return std::nullopt;
}

// One map every registered mode validates, so a cross-mode proof varies the mode and nothing else.
// Race needs a corridor under its configured road name and a checkpoint whose centre is inside it,
// the hill needs a `hill` marker, and free play, royale and the hill all need spawn markers; the
// corridor's half-width clears race's default 40 wu checkpoint radius, and every marker sits on the
// centreline so a body resting on it is supported in all four games. Four per-mode maps would have
// let a mode fail for a reason belonging to its map instead of to its declarations.
[[nodiscard]] simulation::MapDefinition every_mode_map() {
  std::vector<simulation::MapDefinition::Marker> markers;
  for (const double x : {300.0, 600.0}) {
    markers.push_back(simulation::MapDefinition::Marker::create(
        "checkpoint", on_road(x), std::nullopt, simulation::MapMetadata::none()));
  }
  markers.push_back(simulation::MapDefinition::Marker::create("hill", on_road(500.0), std::nullopt,
                                                              simulation::MapMetadata::none()));
  for (const double x : {100.0, 200.0, 300.0, 400.0}) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(on_road(x)));
  }
  simulation::TerrainDefinition terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(960.0, 640.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create("road", kRoadHalfWidth,
                                           {on_road(100.0), on_road(800.0)})},
      {});
  return simulation::MapDefinition::create("shield_symmetry_map", std::move(terrain), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

// One registered mode over the shared map and the shared seeded pair, built through the registry
// rather than through a mode's own constructor: the registry is the one place a name becomes a
// mode, and its no-argument overload supplies each mode's declared defaults, which for the ability
// is the same authored `AbilityConfiguration::defaults()` in all four.
[[nodiscard]] simulation::GameSimulation mode_game(const std::string_view mode_name,
                                                   const simulation::MatchPhase phase) {
  return simulation::GameSimulation::create(
      testing::gameplay_configuration(), seated_pair(kLowController, kHighController, phase),
      simulation::GameSimulationSetup::of_mode(every_mode_map(),
                                               gameplay::GameModeRegistry::create(mode_name)));
}

// One engine tick carrying one pulse per entity through the batch a runtime hands the tick, so the
// mode's whole declared pipeline runs rather than the ability system alone.
[[nodiscard]] simulation::WorldSnapshot step_with_pulses(simulation::GameSimulation& game) {
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {shield_for(kFirstEntity), shield_for(kSecondEntity)},
                                    kFirstReservedEntityId, kReservedEntityIdCount));
  return game.snapshot();
}

} // namespace

TEST_CASE("Shield admission answers the world and never the identity behind the pulse",
          "[unit][gameplay][ability][shield][symmetry]") {
  const auto ability = gameplay::AbilitySystem::create(gameplay::AbilityConfiguration::defaults());
  const testing::TickHarness harness{tick(kAdmissionTick)};

  auto ordered = seated_pair(kLowController, kHighController);
  record_pulses(ordered);
  ability->apply(ordered, harness.context());

  const simulation::Shield* first = stored_shield(ordered, kFirstEntity);
  const simulation::Shield* second = stored_shield(ordered, kSecondEntity);
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  // One activation each, and the two are the same value: three windows dated this tick and the
  // same captured parry-stun duration. A `Shield` records when protection went up and what it will
  // inflict, and nothing at all about who raised it.
  CHECK(*first == *second);
  CHECK(first->activation_tick() == tick(kAdmissionTick));

  // Exchanging the two identities exchanges nothing. `Controllable::controller_id` is the one place
  // the entity and controller identity spaces meet, and this is the strongest available form of
  // "the tick never branches on it": the whole store is unchanged, entity for entity, when the two
  // bodies swap the identities driving them.
  auto swapped = seated_pair(kHighController, kLowController);
  record_pulses(swapped);
  ability->apply(swapped, harness.context());
  const simulation::Shield* swapped_first = stored_shield(swapped, kFirstEntity);
  const simulation::Shield* swapped_second = stored_shield(swapped, kSecondEntity);
  REQUIRE(swapped_first != nullptr);
  REQUIRE(swapped_second != nullptr);
  CHECK(*swapped_first == *first);
  CHECK(*swapped_second == *second);
}

TEST_CASE("Every gate that refuses one identity's pulse refuses the other's on the same tick",
          "[unit][gameplay][ability][shield][symmetry]") {
  const auto ability = gameplay::AbilitySystem::create(gameplay::AbilityConfiguration::defaults());
  const testing::TickHarness harness{tick(kAdmissionTick)};

  // The three phases that are not `running`. A pulse in any of them is refused rather than held:
  // the match machine is engine-owned, so a queued activation would commit at a tick no source
  // chose. Neither identity is held over to the next tick either -- nothing is stored at all.
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kEnded}) {
    CAPTURE(phase);
    auto world = seated_pair(kLowController, kHighController, phase);
    record_pulses(world);
    ability->apply(world, harness.context());
    CHECK(stored_shield(world, kFirstEntity) == nullptr);
    CHECK(stored_shield(world, kSecondEntity) == nullptr);
  }

  // Both entities had their input invalidated on an earlier tick and both pulses carry the absent
  // initial token. Exact optional equality refuses an absent generation against a present one --
  // the stale-input rule steering already obeys -- and it refuses it for both identities.
  {
    auto world = seated_pair(kLowController, kHighController);
    for (const std::uint64_t id : {kFirstEntity, kSecondEntity}) {
      world.mutable_store<simulation::Controllable>().mutable_find(entity(id))->input_generation =
          tick(kSeededActivationTick);
    }
    record_pulses(world);
    ability->apply(world, harness.context());
    CHECK(stored_shield(world, kFirstEntity) == nullptr);
    CHECK(stored_shield(world, kSecondEntity) == nullptr);
  }

  // The canonical input lock, shared with steering: a stunned entity cannot start a guard. The lock
  // reads the world's `Stun` window and takes no argument that could name a source.
  {
    auto world = seated_pair(kLowController, kHighController);
    for (const std::uint64_t id : {kFirstEntity, kSecondEntity}) {
      world.mutable_store<simulation::Stun>().insert_or_assign(
          entity(id),
          simulation::Stun{simulation::TickWindow::create(tick(kAdmissionTick), kOneTick)});
    }
    record_pulses(world);
    ability->apply(world, harness.context());
    CHECK(stored_shield(world, kFirstEntity) == nullptr);
    CHECK(stored_shield(world, kSecondEntity) == nullptr);
  }

  // Both halves of availability, each seeded on both entities: protection that is still running,
  // and protection that is over while the cooldown runs on. The second is the one state in which
  // the component survives with nothing left to protect, because the cooldown is then the only
  // thing that can refuse the next tap. Either way the refusal changes nothing -- the stored value
  // is exactly the one the world came in with, for both identities.
  for (const simulation::Shield existing :
       {simulation::Shield::activate(tick(kSeededActivationTick), kLongProtectionTicks, kOneTick,
                                     kLongCooldownTicks, kOneTick),
        simulation::Shield::activate(tick(kSeededActivationTick), kOneTick, kOneTick,
                                     kLongCooldownTicks, kOneTick)}) {
    auto world = seated_pair(kLowController, kHighController);
    for (const std::uint64_t id : {kFirstEntity, kSecondEntity}) {
      world.mutable_store<simulation::Shield>().insert_or_assign(entity(id), existing);
    }
    record_pulses(world);
    ability->apply(world, harness.context());
    const simulation::Shield* first = stored_shield(world, kFirstEntity);
    const simulation::Shield* second = stored_shield(world, kSecondEntity);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(*first == existing);
    CHECK(*second == existing);
  }
}

TEST_CASE("Every registered mode admits the same running pulse from a live dynamic body",
          "[unit][gameplay][ability][shield][mode]") {
  // Derived from the registry rather than typed beside it, so a fifth game cannot be added without
  // either answering this proof or failing the name list below. The four registered today are
  // sandbox, royale, king_of_the_hill and race, and ADR 0008's mode/state matrix enables the shield
  // in every one of them.
  std::vector<std::string_view> proven;
  std::optional<simulation::Shield> shared;
  for (const gameplay::GameModeRegistry::Registration& registration :
       gameplay::GameModeRegistry::registrations()) {
    CAPTURE(registration.name);
    simulation::GameSimulation game =
        mode_game(registration.name, simulation::MatchPhase::kRunning);
    const simulation::WorldSnapshot snapshot = step_with_pulses(game);

    const std::optional<simulation::Shield> first = published_shield(snapshot, kFirstEntity);
    const std::optional<simulation::Shield> second = published_shield(snapshot, kSecondEntity);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    CHECK(first->activation_tick() == snapshot.tick_sequence());
    // The perfect opening is a prefix of the protection and the cooldown is dated from the same
    // activation, which is what makes these three windows one shield rather than three timers.
    CHECK(first->perfect_window().activation_tick() == first->activation_tick());
    CHECK(first->cooldown_window().activation_tick() == first->activation_tick());
    CHECK(first->perfect_window().expiry_tick() <= first->shield_window().expiry_tick());

    // Every mode reads the same authored `[abilities]` section through its own owned copy, so the
    // shield a race produces is the shield a royale produces, down to the captured stun duration.
    if (shared.has_value()) {
      CHECK(*first == *shared);
    } else {
      shared = first;
    }
    proven.push_back(registration.name);
  }
  // Named as well as counted: a fifth game may not quietly appear beneath this proof, and a
  // renamed one may not quietly leave it. The literal lives outside the assertion because a
  // braced list inside a Catch2 macro would be read as separate macro arguments.
  const std::vector<std::string_view> expected{"sandbox", "royale", "king_of_the_hill", "race"};
  CHECK(proven == expected);
}

TEST_CASE("No registered mode admits a pulse outside the running phase",
          "[unit][gameplay][ability][shield][mode]") {
  for (const gameplay::GameModeRegistry::Registration& registration :
       gameplay::GameModeRegistry::registrations()) {
    for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                             simulation::MatchPhase::kEnded}) {
      CAPTURE(registration.name, phase);
      simulation::GameSimulation game = mode_game(registration.name, phase);
      const simulation::WorldSnapshot snapshot = step_with_pulses(game);
      CHECK_FALSE(published_shield(snapshot, kFirstEntity).has_value());
      CHECK_FALSE(published_shield(snapshot, kSecondEntity).has_value());
      // Not a vacuous refusal: the same two bodies are still live and dynamic in the same world the
      // running case above activates in, so the phase is the only thing that changed.
      REQUIRE(testing::published_body(snapshot, kFirstEntity).has_value());
      REQUIRE(testing::published_body(snapshot, kSecondEntity).has_value());
      CHECK_FALSE(testing::published_body(snapshot, kFirstEntity)->is_static());
      CHECK_FALSE(testing::published_body(snapshot, kSecondEntity)->is_static());
    }
  }
}
