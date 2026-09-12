#include "../simulation/fixtures/deterministic_random_frozen_reference.hpp"
#include "controllers_validation_error.hpp"
#include "fixtures/tactical_observation_fixture.hpp"
#include "fixtures/tactical_profile_fixture.hpp"
#include "tactical_controller.hpp"

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace frame_fixture = blob_royale::testing::tactical_observation_fixture;
namespace profile_fixture = blob_royale::testing::tactical_profile_fixture;
namespace frozen_random = blob_royale::testing::deterministic_random_reference;

namespace {
using Hold = controllers::TacticalTargetHold;
using Reason = controllers::TacticalDecisionReason;

[[nodiscard]] std::unique_ptr<controllers::TacticalController>
bot(const controllers::TacticalProfile::Section& section = profile_fixture::immediate_section(),
    const std::uint64_t controller = frame_fixture::kController) {
  return std::make_unique<controllers::TacticalController>(
      simulation::ControllerId::create(controller), controllers::TacticalProfile::create(section),
      profile_fixture::kIdentity);
}
// The one place this file names the profile's selection settings, so a case states the numbers its
// arithmetic depends on instead of inheriting whatever the shared fixture happens to author. The
// profile *name* is never varied: `tactical_seed_for` mixes the name's length and every one of its
// bytes, so a differently named profile already draws differently and would prove nothing.
[[nodiscard]] controllers::TacticalProfile::Section tuned(const double objective_weight,
                                                          const double risk_tolerance,
                                                          const std::uint64_t horizon_ticks) {
  auto section = profile_fixture::immediate_section();
  section.objective_weights = {objective_weight, objective_weight, objective_weight,
                               objective_weight};
  section.risk_tolerance = risk_tolerance;
  section.prediction_horizon_ticks = horizon_ticks;
  return section;
}
[[nodiscard]] std::vector<simulation::Command> decide(controllers::Controller& controller,
                                                      const frame_fixture::Frame& frame) {
  return controller.decide(frame_fixture::observation(frame));
}
[[nodiscard]] simulation::ThrustCommand thrust(const std::vector<simulation::Command>& commands) {
  REQUIRE(commands.size() == 1);
  REQUIRE(std::holds_alternative<simulation::ThrustCommand>(commands.front()));
  return std::get<simulation::ThrustCommand>(commands.front());
}
void require_zero(const std::vector<simulation::Command>& commands) {
  const auto command = thrust(commands);
  CHECK(command.direction == simulation::Vector2::create(0.0, 0.0));
}
void require_go(const std::vector<simulation::Command>& commands) {
  const auto command = thrust(commands);
  const auto direction = command.direction;
  const double magnitude = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
  CHECK(std::abs(magnitude - 1.0) <= 1e-15);
  CHECK(std::abs(direction.x()) <= 1.0);
  CHECK(std::abs(direction.y()) <= 1.0);
}
void require_same_bits(const simulation::Vector2& actual, const simulation::Vector2& expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual.x()) == std::bit_cast<std::uint64_t>(expected.x()));
  CHECK(std::bit_cast<std::uint64_t>(actual.y()) == std::bit_cast<std::uint64_t>(expected.y()));
}
} // namespace

TEST_CASE("Tactical due choices consume endpoint draws and preserve unit strength rather than "
          "probability scaled thrust",
          "[unit][controllers][tactical]") {
  frame_fixture::Frame frame;
  for (const double probability : {0.0, 1.0}) {
    auto section = profile_fixture::immediate_section();
    section.objective_seek_probability = probability;
    auto controller = bot(section);
    for (std::uint64_t tick = 1; tick <= 3; ++tick) {
      frame.tick = tick;
      const auto commands = decide(*controller, frame);
      if (probability == 0.0) {
        require_zero(commands);
        CHECK(controller->draw_count() == tick);
      } else {
        require_go(commands);
        CHECK(controller->draw_count() == tick * 2);
      }
    }
  }
}

TEST_CASE("Tactical seek and aim draw order matches independent seed and written normalization",
          "[unit][controllers][tactical]") {
  for (const double probability : {0.5, 1.0}) {
    auto section = profile_fixture::immediate_section();
    section.objective_seek_probability = probability;
    section.aim_error = 0.25;
    auto controller = bot(section);
    frame_fixture::Frame frame;
    frame.circles.front().y = 500.0;
    auto random = frozen_random::DeterministicRandom::create(profile_fixture::frozen_seed(
        profile_fixture::kIdentity, section.profile_name, frame.running_tick));
    std::size_t go_count = 0;
    std::size_t coast_count = 0;
    for (std::uint64_t tick = 1; tick <= 32; ++tick) {
      frame.tick = tick;
      const auto commands = decide(*controller, frame);
      const double seek = random.next_unit_interval();
      if (seek >= section.objective_seek_probability) {
        ++coast_count;
        require_zero(commands);
        CHECK(controller->draw_count() == random.draw_count());
        continue;
      }
      ++go_count;
      const auto command = thrust(commands);
      const double dx = frame.circles.front().x - frame.x;
      const double dy = frame.circles.front().y - frame.y;
      const double magnitude = std::sqrt(dx * dx + dy * dy);
      const double ux = dx / magnitude;
      const double uy = dy / magnitude;
      const double error = ((random.next_unit_interval() * 2.0) - 1.0) * section.aim_error;
      const double rx = ux - error * uy;
      const double ry = uy + error * ux;
      const double rotated_magnitude = std::sqrt(rx * rx + ry * ry);
      const auto expected =
          simulation::Vector2::create(std::clamp(rx / rotated_magnitude, -1.0, 1.0),
                                      std::clamp(ry / rotated_magnitude, -1.0, 1.0));
      require_same_bits(command.direction, expected);
      CHECK(controller->draw_count() == random.draw_count());
    }
    CHECK(go_count > 0);
    if (probability < 1.0) {
      CHECK(coast_count > 0);
    }
  }
}

TEST_CASE("Tactical first eligible empty observations retain their deadline and missed intervals "
          "do not catch up",
          "[unit][controllers][tactical][timing]") {
  auto section = profile_fixture::immediate_section();
  section.reaction_delay_ticks = 3;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  frame.circles.clear();
  CHECK(decide(*controller, frame).empty());
  REQUIRE(controller->reaction_window());
  CHECK(controller->reaction_window()->expiry_tick().value() == 4);
  frame.tick = 2;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->reaction_window()->activation_tick().value() == 1);
  frame.tick = 3;
  frame.circles = frame_fixture::Frame{}.circles;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 4;
  require_go(decide(*controller, frame));
  CHECK(controller->draw_count() == 2);
  frame.tick = 6;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 10;
  require_go(decide(*controller, frame));
  CHECK(controller->draw_count() == 4);
  CHECK(controller->reaction_window()->expiry_tick().value() == 13);
}

TEST_CASE("Tactical target persistence refreshes position without renewal and expires between "
          "reaction deadlines",
          "[unit][controllers][tactical][timing]") {
  // The weights are stated here rather than inherited: at the tick 9 deadline the held first
  // objective carries the hysteresis bonus, and a full weight is what lets the much nearer second
  // objective outscore it. A profile that weighted this kind at a quarter would keep the first.
  auto section = tuned(1.0, 1.0, 0);
  section.reaction_delay_ticks = 4;
  section.target_persistence_ticks = 2;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 5;
  require_go(decide(*controller, frame));
  REQUIRE(controller->persistence_window());
  CHECK(controller->persistence_window()->activation_tick().value() == 5);
  CHECK(controller->persistence_window()->expiry_tick().value() == 7);
  frame.tick = 6;
  frame.circles.front().x = 550.0;
  frame.circles.push_back({frame_fixture::kSecondObjective, 250.0, 320.0, 10.0});
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->target_key()->subject == frame_fixture::kFirstObjective);
  CHECK(controller->target() == simulation::Vector2::create(550.0, 320.0));
  CHECK(controller->persistence_window()->activation_tick().value() == 5);
  CHECK(controller->target_hold() == Hold::kRetainedInLease);
  frame.tick = 7;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->target_key()->subject == frame_fixture::kFirstObjective);
  frame.tick = 9;
  require_go(decide(*controller, frame));
  CHECK(controller->target_key()->subject == frame_fixture::kSecondObjective);
  CHECK(controller->target_hold() == Hold::kSwitched);
  CHECK(controller->persistence_window()->activation_tick().value() == 9);
}

TEST_CASE("Tactical removed targets cancel held input immediately and restart delay once",
          "[unit][controllers][tactical][timing]") {
  auto section = profile_fixture::immediate_section();
  section.reaction_delay_ticks = 2;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 3;
  require_go(decide(*controller, frame));
  frame.tick = 4;
  frame.circles = {{frame_fixture::kSecondObjective, 400.0, 400.0, 20.0}};
  require_zero(decide(*controller, frame));
  CHECK_FALSE(controller->target_key());
  CHECK(controller->target_hold() == Hold::kLost);
  CHECK(controller->decision_reason() == Reason::kAwaitingReaction);
  CHECK(controller->reaction_window()->expiry_tick().value() == 6);
  frame.tick = 5;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->draw_count() == 2);
  frame.tick = 6;
  require_go(decide(*controller, frame));
  CHECK(controller->target_key()->subject == frame_fixture::kSecondObjective);
  CHECK(controller->target_hold() == Hold::kAcquired);
}

TEST_CASE(
    "Tactical race gate changes and missing progress cannot retain stale work while finish coasts",
    "[unit][controllers][tactical][race]") {
  auto section = profile_fixture::immediate_section();
  section.reaction_delay_ticks = 2;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  frame.mode = frame_fixture::Mode::kRace;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 3;
  require_go(decide(*controller, frame));
  frame.tick = 4;
  frame.checkpoint = 1;
  require_zero(decide(*controller, frame));
  CHECK(controller->reaction_window()->expiry_tick().value() == 6);
  frame.tick = 6;
  require_go(decide(*controller, frame));
  CHECK(controller->target_key()->subject == 1);
  frame.tick = 7;
  frame.race_progress = false;
  require_zero(decide(*controller, frame));
  CHECK_FALSE(controller->target_key());
  CHECK(controller->reaction_window()->expiry_tick().value() == 9);
  frame.tick = 8;
  require_zero(decide(*controller, frame));
  CHECK(controller->reaction_window()->expiry_tick().value() == 9);
  frame.tick = 9;
  frame.race_progress = true;
  require_go(decide(*controller, frame));
  frame.tick = 10;
  frame.checkpoint = 2;
  require_zero(decide(*controller, frame));
  CHECK(controller->draw_count() == 6);
}

TEST_CASE("Tactical observed stun and missed stun generation changes restart reaction without "
          "decision draws",
          "[unit][controllers][tactical][stun]") {
  auto section = profile_fixture::immediate_section();
  section.reaction_delay_ticks = 2;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 3;
  require_go(decide(*controller, frame));
  const auto seed = controller->current_seed();
  frame.tick = 4;
  frame.generation = 4;
  frame.stun = true;
  frame.stun_activation = 4;
  frame.stun_duration = 3;
  CHECK(decide(*controller, frame).empty());
  CHECK_FALSE(controller->target_key());
  frame.tick = 6;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->draw_count() == 2);
  frame.tick = 7;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->reaction_window()->expiry_tick().value() == 9);
  frame.tick = 9;
  const auto recovered = thrust(decide(*controller, frame));
  CHECK(recovered.input_generation == simulation::TickSequence::create(4));
  CHECK(controller->draw_count() == 4);
  frame.tick = 10;
  frame.stun = false;
  frame.generation = 10;
  const auto cancellation = decide(*controller, frame);
  require_zero(cancellation);
  CHECK(thrust(cancellation).input_generation == simulation::TickSequence::create(10));
  CHECK(controller->draw_count() == 4);
  frame.tick = 12;
  require_go(decide(*controller, frame));
  CHECK(controller->draw_count() == 6);
  CHECK(controller->current_seed() == seed);
}

TEST_CASE("Tactical bodyless waiting replacement and absent-body retries retain no stale intent or "
          "allocation seed",
          "[unit][controllers][tactical][lifecycle]") {
  auto section = profile_fixture::immediate_section();
  section.reaction_delay_ticks = 2;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 3;
  require_go(decide(*controller, frame));
  const auto seed = controller->current_seed();
  frame.tick = 4;
  frame.body = false;
  CHECK(decide(*controller, frame).empty());
  CHECK_FALSE(controller->target_key());
  CHECK(controller->draw_count() == 2);
  frame.tick = 5;
  frame.body = true;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->reaction_window()->expiry_tick().value() == 7);
  frame.tick = 7;
  require_go(decide(*controller, frame));
  frame.tick = 8;
  frame.owned = false;
  const auto spawn = decide(*controller, frame);
  REQUIRE(spawn.size() == 1);
  CHECK(std::holds_alternative<simulation::SpawnCommand>(spawn.front()));
  CHECK(decide(*controller, frame).empty());
  frame.tick = 47;
  CHECK(decide(*controller, frame).empty());
  frame.tick = 48;
  REQUIRE(decide(*controller, frame).size() == 1);
  frame.tick = 49;
  frame.owned = true;
  frame.entity = 9;
  CHECK(decide(*controller, frame).empty());
  CHECK_FALSE(controller->last_spawn_request_tick());
  frame.tick = 51;
  CHECK(thrust(decide(*controller, frame)).entity == simulation::EntityId::create(9));
  CHECK(controller->current_seed() == seed);
  CHECK(controller->draw_count() == 6);
}

TEST_CASE("Tactical duplicate and stale observations are rejected before base identity mutation "
          "and each new round reseeds once",
          "[unit][controllers][tactical][idempotence]") {
  auto controller = bot();
  frame_fixture::Frame frame;
  frame.phase = simulation::MatchPhase::kLobby;
  require_zero(decide(*controller, frame));
  CHECK_FALSE(controller->current_seed());
  frame.tick = 2;
  frame.phase = simulation::MatchPhase::kLobby;
  require_zero(decide(*controller, frame));
  frame.tick = 3;
  frame.phase = simulation::MatchPhase::kRunning;
  require_go(decide(*controller, frame));
  const auto old_seed = controller->current_seed();
  frame.tick = 4;
  frame.phase = simulation::MatchPhase::kLobby;
  require_zero(decide(*controller, frame));
  CHECK(controller->draw_count() == 2);
  frame.tick = 5;
  frame.running_tick = 5;
  frame.phase = simulation::MatchPhase::kRunning;
  require_go(decide(*controller, frame));
  CHECK(controller->current_seed() != old_seed);
  CHECK(controller->draw_count() == 2);
  const auto key = controller->target_key();
  frame.entity = 9;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->entity() == simulation::EntityId::create(frame_fixture::kEntity));
  frame.tick = 4;
  frame.owned = false;
  frame.running_tick = 0;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->target_key() == key);
  CHECK(controller->last_completed_tick() == simulation::TickSequence::create(5));
  CHECK_FALSE(controller->last_spawn_request_tick());
  frame.controller = 8;
  CHECK_THROWS_AS(decide(*controller, frame), controllers::ControllersValidationError);
}

TEST_CASE("Tactical failed observation does not consume its tick lease or draws and corrected same "
          "tick can retry",
          "[unit][controllers][tactical][failure]") {
  auto controller = bot();
  auto reference = bot();
  frame_fixture::Frame frame;
  require_go(decide(*controller, frame));
  static_cast<void>(decide(*reference, frame));
  const auto window = controller->persistence_window();
  const auto seed = controller->current_seed();
  const auto reason = controller->decision_reason();
  frame.tick = 2;
  frame.circles.front().radius = -1.0;
  CHECK_THROWS_AS(decide(*controller, frame), controllers::ControllersValidationError);
  CHECK(controller->last_completed_tick() == simulation::TickSequence::create(1));
  CHECK(controller->persistence_window() == window);
  CHECK(controller->current_seed() == seed);
  CHECK(controller->draw_count() == 2);
  // A reason is decision state, so a failed observation leaves it exactly where the last completed
  // decision left it rather than recording the branch that threw.
  CHECK(controller->decision_reason() == reason);
  frame.circles.front().radius = 20.0;
  const auto retried = thrust(decide(*controller, frame));
  const auto expected = thrust(decide(*reference, frame));
  require_same_bits(retried.direction, expected.direction);
  CHECK(controller->draw_count() == 4);
  frame.tick = 3;
  frame.running_tick = 4;
  CHECK_THROWS_AS(decide(*controller, frame), controllers::ControllersValidationError);
  CHECK(controller->last_completed_tick() == simulation::TickSequence::create(2));
  frame.running_tick = 0;
  require_go(decide(*controller, frame));
}

TEST_CASE("Tactical arrived zero-radius goals and lost terrain support coast without draws",
          "[unit][controllers][tactical][terrain]") {
  auto controller = bot();
  frame_fixture::Frame frame;
  frame.circles.front().x = frame.x;
  frame.circles.front().radius = 0.0;
  require_zero(decide(*controller, frame));
  CHECK(controller->draw_count() == 0);
  frame.tick = 2;
  frame.circles.front().x = 600.0;
  require_go(decide(*controller, frame));
  CHECK(controller->draw_count() == 2);
  frame.tick = 3;
  frame.terrain = frame_fixture::terrain_with_hole(400.0, 30.0);
  require_zero(decide(*controller, frame));
  CHECK_FALSE(controller->target_key());
  CHECK(controller->draw_count() == 2);
  CHECK(controller->decision_reason() == Reason::kNoScreenedCandidate);
  frame.tick = 4;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->draw_count() == 2);
}

TEST_CASE("Tactical stable authored identity ignores controller allocation while copying profile "
          "configuration",
          "[unit][controllers][tactical][identity]") {
  auto profile = profile_fixture::profile();
  controllers::TacticalController first(
      simulation::ControllerId::create(frame_fixture::kController), profile,
      profile_fixture::kIdentity);
  controllers::TacticalController second(simulation::ControllerId::create(12), profile,
                                         profile_fixture::kIdentity);
  auto changed = profile_fixture::immediate_section();
  changed.objective_seek_probability = 0.0;
  profile = controllers::TacticalProfile::create(changed);
  frame_fixture::Frame frame;
  const auto a = thrust(decide(first, frame));
  frame.controller = 12;
  frame.entity = 12;
  const auto b = thrust(decide(second, frame));
  require_same_bits(a.direction, b.direction);
  CHECK(first.current_seed() == second.current_seed());
  CHECK(first.profile().objective_seek_probability() == 1.0);
  CHECK(second.profile().objective_seek_probability() == 1.0);
}

TEST_CASE("Tactical every published nonrunning phase coasts without seeding or drawing",
          "[unit][controllers][tactical][lifecycle]") {
  for (const auto phase : {simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                           simulation::MatchPhase::kEnded}) {
    auto controller = bot();
    frame_fixture::Frame frame;
    // Loaded tick0 preserves the authored phase. Stepping the fixture's Idle objective would
    // transition countdown/ended to lobby and would not exercise these distinct publications.
    frame.tick = 0;
    frame.phase = phase;
    const auto observation = frame_fixture::observation(frame);
    REQUIRE(observation.snapshot().match().phase() == phase);
    require_zero(controller->decide(observation));
    CHECK_FALSE(controller->current_seed());
    CHECK(controller->draw_count() == 0);
    CHECK_FALSE(controller->target_key());
    CHECK(controller->decision_reason() == Reason::kMatchNotRunning);
  }
}

TEST_CASE("Tactical objective weight and not the seed chooses between a near risk and a far clear "
          "objective",
          "[unit][controllers][tactical][utility]") {
  frame_fixture::Frame frame;
  frame.terrain = frame_fixture::terrain_with_hole(400.0, 30.0);
  frame.circles = {{frame_fixture::kFirstObjective, 350.0, 320.0, 0.0},
                   {frame_fixture::kSecondObjective, 350.0, 100.0, 0.0}};
  // Both approaches are supported, so both survive terrain screening. Only the first overshoots
  // into the hole's void within the horizon: 600 world units a second for 160 ticks is 240 units,
  // which runs past a target 150 away and into a void that starts at 170.
  auto bold = bot(tuned(1.0, 0.95, 160));
  auto careful = bot(tuned(0.25, 0.95, 160));
  const auto bold_commands = decide(*bold, frame);
  const auto careful_commands = decide(*careful, frame);
  // Same authored name, so both mix the same seed and draw the same stream. The only difference
  // between these two bots is one authored weight, which is what makes the divergence below a
  // proof about the weight instead of a proof about `tactical_seed_for`.
  CHECK(bold->profile().name() == careful->profile().name());
  CHECK(bold->current_seed() == careful->current_seed());
  CHECK(bold->draw_count() == careful->draw_count());
  require_go(bold_commands);
  require_go(careful_commands);
  CHECK(bold->target_key()->subject == frame_fixture::kFirstObjective);
  CHECK(careful->target_key()->subject == frame_fixture::kSecondObjective);
  CHECK(bold->decision_reason() == Reason::kPursuingUnderRisk);
  CHECK(careful->decision_reason() == Reason::kPursuing);
  CHECK(bold->objective_work().screened_candidate_count == 2);
  CHECK(bold->objective_work().prediction_step_count == 2);
}

TEST_CASE("Tactical hysteresis holds a target through its lease and then by the bonus until a "
          "challenger beats it",
          "[unit][controllers][tactical][utility]") {
  auto section = tuned(1.0, 1.0, 0);
  section.reaction_delay_ticks = 0;
  section.target_persistence_ticks = 2;
  auto controller = bot(section);
  frame_fixture::Frame frame;
  frame.circles = {{frame_fixture::kFirstObjective, 500.0, 320.0, 0.0}};
  require_go(decide(*controller, frame));
  CHECK(controller->target_hold() == Hold::kAcquired);
  CHECK(controller->decision_reason() == Reason::kPursuing);
  frame.tick = 2;
  frame.circles.push_back({frame_fixture::kSecondObjective, 450.0, 320.0, 0.0});
  require_go(decide(*controller, frame));
  CHECK(controller->target_hold() == Hold::kRetainedInLease);
  CHECK(controller->target_key()->subject == frame_fixture::kFirstObjective);
  // The lease has ended, so selection runs; the challenger is nearer but by less than the bonus.
  frame.tick = 3;
  require_go(decide(*controller, frame));
  CHECK(controller->target_hold() == Hold::kRetainedByBonus);
  CHECK(controller->target_key()->subject == frame_fixture::kFirstObjective);
  // Nearer by more than the bonus, and the held target is abandoned rather than oscillated over.
  frame.tick = 5;
  frame.circles.back().x = 250.0;
  require_go(decide(*controller, frame));
  CHECK(controller->target_hold() == Hold::kSwitched);
  CHECK(controller->target_key()->subject == frame_fixture::kSecondObjective);
}

TEST_CASE("Tactical candidates that all screen badly still produce a decision rather than a throw "
          "or an empty pass",
          "[unit][controllers][tactical][utility]") {
  frame_fixture::Frame frame;
  frame.terrain = frame_fixture::terrain_with_hole(400.0, 30.0);
  frame.circles = {{frame_fixture::kFirstObjective, 350.0, 320.0, 0.0},
                   {frame_fixture::kSecondObjective, 330.0, 320.0, 0.0}};
  auto controller = bot(tuned(1.0, 0.0, 160));
  const auto commands = decide(*controller, frame);
  require_go(commands);
  CHECK(controller->decision_reason() == Reason::kPursuingUnderRisk);
  CHECK(controller->target_key()->subject == frame_fixture::kSecondObjective);
  CHECK(controller->objective_work().screened_candidate_count == 2);
}

TEST_CASE("Tactical bounded work is published per decision and sits under its derived ceiling",
          "[unit][controllers][tactical][limits]") {
  frame_fixture::Frame frame;
  frame.circles.clear();
  for (std::size_t index = 0; index < controllers::kMaximumTacticalObjectiveCandidateCount;
       ++index) {
    frame.circles.push_back({frame_fixture::kFirstObjective + index, 600.0, 320.0, 0.0});
  }
  auto controller = bot(tuned(1.0, 1.0, 160));
  require_go(decide(*controller, frame));
  const auto work = controller->objective_work();
  CHECK(work.raw_candidate_count == controllers::kMaximumTacticalObjectiveCandidateCount);
  CHECK(work.screened_candidate_count == controllers::kMaximumTacticalObjectiveCandidateCount);
  // No published hill here carries a velocity, so the pass casts escape rays and predicts nothing
  // else: one per screened candidate, half the ceiling, and never a search or a replanning loop.
  CHECK(work.prediction_step_count == controllers::kMaximumTacticalObjectiveCandidateCount);
  CHECK(work.prediction_step_count <= controllers::kMaximumTacticalPredictionStepCount);
}

TEST_CASE("Tactical every decision reason and every hold outcome is reachable",
          "[unit][controllers][tactical][reason]") {
  const auto section = tuned(1.0, 1.0, 0);
  {
    const auto controller = bot(section);
    CHECK(controller->decision_reason() == Reason::kNotDecided);
    CHECK(controller->target_hold() == Hold::kNone);
  }
  {
    auto controller = bot(section);
    frame_fixture::Frame frame;
    frame.owned = false;
    REQUIRE(decide(*controller, frame).size() == 1);
    CHECK(controller->decision_reason() == Reason::kAwaitingBody);
    frame.tick = 2;
    frame.owned = true;
    frame.body = false;
    CHECK(decide(*controller, frame).empty());
    CHECK(controller->decision_reason() == Reason::kNoControllableBody);
    frame.tick = 3;
    frame.body = true;
    frame.phase = simulation::MatchPhase::kLobby;
    require_zero(decide(*controller, frame));
    CHECK(controller->decision_reason() == Reason::kMatchNotRunning);
    frame.tick = 4;
    frame.phase = simulation::MatchPhase::kRunning;
    frame.stun = true;
    frame.stun_activation = 4;
    frame.stun_duration = 2;
    CHECK(decide(*controller, frame).empty());
    CHECK(controller->decision_reason() == Reason::kStunned);
  }
  {
    auto controller = bot(section);
    frame_fixture::Frame frame;
    frame.terrain = frame_fixture::terrain_with_hole(600.0, 30.0);
    CHECK(decide(*controller, frame).empty());
    CHECK(controller->decision_reason() == Reason::kNoScreenedCandidate);
    CHECK(controller->target_hold() == Hold::kNone);
  }
  {
    auto delayed = tuned(1.0, 1.0, 0);
    delayed.reaction_delay_ticks = 5;
    auto controller = bot(delayed);
    frame_fixture::Frame frame;
    CHECK(decide(*controller, frame).empty());
    CHECK(controller->decision_reason() == Reason::kAwaitingReaction);
  }
  {
    auto controller = bot(section);
    frame_fixture::Frame frame;
    frame.circles.front().x = frame.x;
    require_zero(decide(*controller, frame));
    CHECK(controller->decision_reason() == Reason::kArrived);
  }
  {
    auto declining = tuned(1.0, 1.0, 0);
    declining.objective_seek_probability = 0.0;
    auto controller = bot(declining);
    frame_fixture::Frame frame;
    require_zero(decide(*controller, frame));
    CHECK(controller->decision_reason() == Reason::kSeekDeclined);
  }
  {
    auto controller = bot(section);
    frame_fixture::Frame frame;
    require_go(decide(*controller, frame));
    CHECK(controller->decision_reason() == Reason::kPursuing);
    CHECK(controller->target_hold() == Hold::kAcquired);
  }
  {
    // A released lease is not a lost one: the objective did not disappear, the provider stopped
    // publishing any objective at all, and later says this bot is finished.
    auto controller = bot(section);
    frame_fixture::Frame frame;
    frame.mode = frame_fixture::Mode::kRace;
    require_go(decide(*controller, frame));
    CHECK(controller->target_hold() == Hold::kAcquired);
    frame.tick = 2;
    frame.race_progress = false;
    require_zero(decide(*controller, frame));
    CHECK(controller->decision_reason() == Reason::kObjectivesWaiting);
    CHECK(controller->target_hold() == Hold::kReleased);
    frame.tick = 3;
    frame.race_progress = true;
    require_go(decide(*controller, frame));
    frame.tick = 4;
    frame.checkpoint = 2;
    require_zero(decide(*controller, frame));
    CHECK(controller->decision_reason() == Reason::kObjectivesFinished);
    CHECK(controller->target_hold() == Hold::kReleased);
  }
}
