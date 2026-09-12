#include "sandbox/sandbox_mode.hpp"

#include "gameplay_test_fixture.hpp"

#include "command_registry.hpp"
#include "components/controllable_component.hpp"
#include "contact_rule_table.hpp"
#include "gameplay_validation_error.hpp"
#include "input_batch.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "shared/guarded_pair_contact_rule.hpp"
#include "simulation_validation_error.hpp"
#include "system_pipeline.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

// The sandbox simulation every test below drives: a four-point map and explicit legacy tuning.
[[nodiscard]] simulation::GameSimulation
sandbox_simulation(const std::size_t spawn_point_count = 4) {
  return testing::gameplay_simulation(gameplay::SandboxMode::create(),
                                      testing::gameplay_map(spawn_point_count));
}

} // namespace

TEST_CASE("SandboxMode declares free play with shared falling and configured return",
          "[unit][gameplay][sandbox]") {
  const gameplay::SandboxMode mode{};

  CHECK(mode.name() == std::string_view{"sandbox"});
  // **This used to assert exact equality with `ContactRuleTable::built_in()`, and the change is
  // deliberate.** That assertion was the honest statement of free play while free play had no
  // ability to defend with. ADR 0008's mode/state matrix answers "Sandbox, running with a body"
  // with *Enabled*, so a blob in free play may raise a shield, and a shield that worked in the
  // three competitive modes and not in the one mode people experiment in would be a mechanic that
  // lies. Sandbox therefore declares the same `guarded_pair` row the other three do, which makes
  // the engine's three rows unreachable here; the suffix check states that they are nonetheless
  // still declared, unmodified, and taken from `built_in()` rather than transcribed.
  const simulation::ContactRuleTable rules = mode.contact_rules();
  const simulation::ContactRuleTable built_in = simulation::ContactRuleTable::built_in();
  REQUIRE(rules.size() == built_in.size() + 1);
  CHECK(rules.rows()[0].name() == gameplay::kGuardedPairContactRuleName);
  for (std::size_t index = 0; index < built_in.size(); ++index) {
    CHECK(rules.rows()[index + 1] == built_in.rows()[index]);
  }
  // Six: the four free play always had, plus `shield` and `charge`, advertised only because
  // sandbox declares the one `ability` system that admits both of them. Free play still declares
  // no lobby or tuning kind, so charge widened the ability half of this list and nothing else.
  CHECK(mode.accepted_command_kinds() ==
        simulation::CommandKindMask::create(
            {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
             simulation::CommandKind::kLeave, simulation::CommandKind::kThrust,
             simulation::CommandKind::kShield, simulation::CommandKind::kCharge}));

  const simulation::SystemPipeline systems = mode.systems();
  REQUIRE(systems.size() == 4);
  CHECK(mode.motion_triggers().size() == 1);
  // `ability` is last at this stage in every mode that declares it, mirroring `status` at
  // kPostKernel: pulse admission reads the canonical input lock, so every kPreKernel system that
  // can change what that lock answers has already run.
  REQUIRE(systems.systems_at(simulation::SystemStage::kPreKernel).size() == 2);
  CHECK(systems.systems_at(simulation::SystemStage::kPreKernel)[0].system->name() ==
        std::string_view{"thrust_steering"});
  CHECK(systems.systems_at(simulation::SystemStage::kPreKernel)[1].system->name() ==
        std::string_view{"ability"});
  REQUIRE(systems.systems_at(simulation::SystemStage::kPostKernel).size() == 1);
  CHECK(systems.systems_at(simulation::SystemStage::kPostKernel)[0].system->name() == "status");
  REQUIRE(systems.systems_at(simulation::SystemStage::kLifecycle).size() == 1);
  CHECK(systems.systems_at(simulation::SystemStage::kLifecycle)[0].system->name() == "respawn");
}

TEST_CASE("SandboxMode accepts thrust, so a thrust command reaches the world",
          "[unit][gameplay][sandbox]") {
  simulation::GameSimulation game = sandbox_simulation();

  CHECK(game.accepted_command_kinds().contains(simulation::CommandKind::kThrust));
  CHECK(game.accepted_command_kinds().contains(simulation::CommandKind::kSpawn));
  CHECK(game.accepted_command_kinds().contains(simulation::CommandKind::kDespawn));

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7)}));
  REQUIRE(testing::published_body(game.snapshot(), 1).has_value());

  // A thrust for the seated entity is accepted by InputBatch::create rather than rejected, which
  // is the mode's accepted set being honoured at the one place it is enforced inside the engine.
  CHECK_NOTHROW(testing::gameplay_batch(game, {testing::thrust_command(1, 1.0, 0.0)}));
}

TEST_CASE("SandboxMode never leaves free play", "[unit][gameplay][sandbox]") {
  simulation::GameSimulation game = sandbox_simulation();

  // Zero durations, and the machine commits at most one transition per tick, so lobby yields to
  // countdown on tick 1 and countdown to running on tick 2.
  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  CHECK(testing::committed_phase(game) == simulation::MatchPhase::kCountdown);
  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  REQUIRE(testing::committed_phase(game) == simulation::MatchPhase::kRunning);

  const simulation::TickSequence running_started = testing::committed_running_started_tick(game);

  // An empty field, a joining entity, and a leaving entity are the three roster states an
  // attrition mode would end on. Free play ends on none of them.
  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7)}));
  for (int tick = 0; tick < 64; ++tick) {
    game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  }

  CHECK(testing::committed_phase(game) == simulation::MatchPhase::kRunning);
  CHECK(testing::committed_outcome(game) == simulation::MatchOutcome::undecided());
  // `running` was entered once and never re-entered, so no restart happened behind the assertion.
  CHECK(testing::committed_running_started_tick(game) == running_started);
}

TEST_CASE("SandboxMode seats every spawn at the next free spawn point",
          "[unit][gameplay][sandbox]") {
  simulation::GameSimulation game = sandbox_simulation();

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7), testing::spawn_command(8),
                                           testing::spawn_command(9)}));

  const simulation::WorldSnapshot snapshot = game.snapshot();
  REQUIRE(snapshot.players().size() == 3);
  // The map's points are at x = 100, 200, 300, 400; seating walks the rotation counter forward, so
  // three joiners in one tick take three consecutive points rather than contending for one.
  CHECK(testing::published_body(snapshot, 1)->position() ==
        simulation::Vector2::create(100.0, 320.0));
  CHECK(testing::published_body(snapshot, 2)->position() ==
        simulation::Vector2::create(200.0, 320.0));
  CHECK(testing::published_body(snapshot, 3)->position() ==
        simulation::Vector2::create(300.0, 320.0));
}

TEST_CASE("SandboxMode seats a joiner that arrives mid-match", "[unit][gameplay][sandbox]") {
  simulation::GameSimulation game = sandbox_simulation();

  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  REQUIRE(testing::committed_phase(game) == simulation::MatchPhase::kRunning);

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7)}));

  CHECK(testing::published_player_count(game) == 1);
  CHECK(testing::published_body(game.snapshot(), 1).has_value());
}

TEST_CASE("SandboxMode defers a joiner when every spawn point is taken",
          "[unit][gameplay][sandbox]") {
  simulation::GameSimulation game = sandbox_simulation(1);

  game.step(testing::kGameplayFixedDelta,
            testing::gameplay_batch(game, {testing::spawn_command(7), testing::spawn_command(8)}));

  // Deferral is the only way a policy declines, and it creates the entity without a body: one
  // player is published and the second id exists carrying only its controller link.
  const simulation::WorldSnapshot snapshot = game.snapshot();
  CHECK(snapshot.players().size() == 1);
  CHECK(snapshot.components<simulation::Controllable>().size() == 2);
  CHECK_FALSE(testing::published_body(snapshot, 2).has_value());
}

TEST_CASE("SandboxMode rejects a map with no spawn marker at construction",
          "[unit][gameplay][sandbox][validation]") {
  try {
    static_cast<void>(testing::gameplay_simulation(gameplay::SandboxMode::create(),
                                                   testing::gameplay_map_without_spawn_points()));
    FAIL("a map with no spawn marker was accepted");
  } catch (const gameplay::GameplayValidationError& error) {
    CHECK(error.validation_code() ==
          gameplay::GameplayValidationCode::kSandboxMapWithoutSpawnPoint);
    CHECK(error.code() == std::string_view{"GAMEPLAY.SANDBOX_MAP_WITHOUT_SPAWN_POINT"});
    CHECK(error.context() == "sandbox_mode.validate_map.spawn_points");
    CHECK(error.detail().find("gameplay_map_without_spawn_points") != std::string::npos);
  }
}

TEST_CASE("SandboxMode uses shared authored movement without advertising unseated tuning authority",
          "[unit][gameplay][sandbox][movement]") {
  for (const double acceleration : {0.0, 800.0}) {
    CAPTURE(acceleration);
    auto configuration = gameplay::GameModeConfiguration::defaults();
    configuration.movement = simulation::MovementTuning::create(acceleration, 10'000.0);
    auto game =
        testing::gameplay_simulation(gameplay::SandboxMode::create(configuration),
                                     testing::gameplay_map(4), 0, {}, configuration.movement);
    CHECK_FALSE(
        game.accepted_command_kinds().contains(simulation::CommandKind::kSetMovementTuning));
    game.step(testing::kGameplayFixedDelta,
              testing::gameplay_batch(
                  game, {testing::spawn_command(7), testing::thrust_command(1, 1.0, 0.0)}));
    const auto snapshot = game.snapshot();
    CHECK(testing::published_body(snapshot, 1)->acceleration() ==
          simulation::Vector2::create(acceleration, 0.0));
    CHECK(snapshot.match().movement().current == configuration.movement);
    CHECK(snapshot.match().movement().defaults == configuration.movement);
  }
}

TEST_CASE("A sandbox simulation with no command reproduces the empty-batch tick",
          "[unit][gameplay][sandbox]") {
  // Sandbox declares two kPreKernel systems and nothing else at that stage, and neither steering
  // nor ability admission has an entity to act on, so with no entity to steer the tick is still the
  // accepted seven-phase baseline: an empty world stays empty and nothing is published.
  simulation::GameSimulation game = sandbox_simulation();
  for (int tick = 0; tick < 8; ++tick) {
    game.step(testing::kGameplayFixedDelta, simulation::InputBatch::empty());
  }

  CHECK(testing::published_entity_count(game) == 0);
  CHECK(testing::published_player_count(game) == 0);
  CHECK(game.tick_sequence().value() == 8);
}
