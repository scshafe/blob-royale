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
[[nodiscard]] std::unique_ptr<controllers::TacticalController>
bot(const controllers::TacticalProfile::Section& section = profile_fixture::immediate_section(),
    const std::uint64_t controller = frame_fixture::kController) {
  return std::make_unique<controllers::TacticalController>(
      simulation::ControllerId::create(controller), controllers::TacticalProfile::create(section),
      profile_fixture::kIdentity);
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
  auto section = profile_fixture::immediate_section();
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
  frame.tick = 7;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->target_key()->subject == frame_fixture::kFirstObjective);
  frame.tick = 9;
  require_go(decide(*controller, frame));
  CHECK(controller->target_key()->subject == frame_fixture::kSecondObjective);
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
  CHECK(controller->reaction_window()->expiry_tick().value() == 6);
  frame.tick = 5;
  CHECK(decide(*controller, frame).empty());
  CHECK(controller->draw_count() == 2);
  frame.tick = 6;
  require_go(decide(*controller, frame));
  CHECK(controller->target_key()->subject == frame_fixture::kSecondObjective);
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
  frame.tick = 2;
  frame.circles.front().radius = -1.0;
  CHECK_THROWS_AS(decide(*controller, frame), controllers::ControllersValidationError);
  CHECK(controller->last_completed_tick() == simulation::TickSequence::create(1));
  CHECK(controller->persistence_window() == window);
  CHECK(controller->current_seed() == seed);
  CHECK(controller->draw_count() == 2);
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
  }
}
