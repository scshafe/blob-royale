#include "../simulation/fixtures/deterministic_random_frozen_reference.hpp"
#include "commands/charge_command.hpp"
#include "commands/shield_command.hpp"
#include "components/charge_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "fixed_delta.hpp"
#include "fixtures/tactical_observation_fixture.hpp"
#include "fixtures/tactical_profile_fixture.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "tactical_controller.hpp"
#include "terrain_definition.hpp"
#include "tick_window.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
                               objective_weight, objective_weight};
  section.risk_tolerance = risk_tolerance;
  section.prediction_horizon_ticks = horizon_ticks;
  return section;
}
// The same idea for the three combat settings: one weight moves while the profile *name* holds
// still, which is what makes a divergence below attributable to the authored number rather than to
// `tactical_seed_for`.
[[nodiscard]] controllers::TacticalProfile::Section
combat(const double shove_weight, const double charge_screen, const std::uint64_t shield_ticks) {
  auto section = tuned(1.0, 0.0, 0);
  section.objective_weights.shove_setup = shove_weight;
  section.charge_screen_diagonal_fraction = charge_screen;
  section.shield_anticipation_ticks = shield_ticks;
  return section;
}
// The same idea once more for the arrival brake, which is the one personality setting no policy
// carries: it is decided in the controller, on the arrived branch, so a case authors it on the
// profile and changes nothing else.
[[nodiscard]] controllers::TacticalProfile::Section braking(const double fraction) {
  auto section = combat(1.0, 0.5, 0);
  section.arrival_brake_fraction = fraction;
  return section;
}

// **The four shipped personalities, authored exactly as `config/blob-royale.cfg` authors them and
// with one deliberate difference: they all carry the fixture's name.** `tactical_seed_for` mixes a
// profile name's length and every one of its bytes, so four differently named profiles already draw
// different streams and any divergence between them would be a proof about the seed rather than
// about the numbers. Holding the name still is what makes the comparison below a proof that a
// personality *is* its numbers -- which is the whole claim, because no decision in
// `blob_controllers` branches on a profile's identity.
[[nodiscard]] controllers::TacticalProfile::Section keeper_section() {
  return {std::string{profile_fixture::kName},
          1.0,
          40,
          0.02,
          400,
          {1.0, 0.5, 0.25, 0.5, 0.0},
          0.25,
          120,
          1.0,
          28,
          0.75,
          1.0,
          0.0,
          1.0};
}
[[nodiscard]] controllers::TacticalProfile::Section bully_section() {
  return {std::string{profile_fixture::kName},
          1.0,
          60,
          0.05,
          200,
          {0.5, 0.5, 0.375, 0.5, 1.0},
          0.5,
          80,
          0.25,
          21,
          0.9,
          0.0,
          0.0,
          0.0};
}
[[nodiscard]] controllers::TacticalProfile::Section opportunist_section() {
  return {std::string{profile_fixture::kName},
          1.0,
          20,
          0.03,
          100,
          {0.5, 0.625, 0.5, 0.625, 0.75},
          0.625,
          80,
          0.375,
          24,
          0.8,
          0.0,
          1.0,
          0.5};
}
[[nodiscard]] controllers::TacticalProfile::Section cautious_racer_section() {
  return {std::string{profile_fixture::kName},
          1.0,
          100,
          0.01,
          600,
          {0.25, 0.5, 1.0, 1.0, 0.125},
          0.125,
          200,
          0.5,
          32,
          0.6,
          0.0,
          0.5,
          0.75};
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

// **The third movement helper, and the reason there has to be a third.** `require_zero` demands
// exactly `(0, 0)` and `require_go` demands a unit magnitude within 1e-15, so between them they
// describe every thrust this file could emit before the arrival brake existed and *none* of the
// ones it emits now: a brake is a real subunit command, which `normalized_thrust_intent` admits
// because it is a magnitude clamp and not a normaliser, and which `ChaserController` already ships
// as `unit * aggression_weight`. Loosening either of the two above to accept one would have
// weakened every movement assertion in this file at once, so the subunit case comes through here
// instead -- and at the strictest tolerance of the three, because a deadbeat law's whole claim is
// that two toolchains compute the same binary64.
void require_thrust_bits(const std::vector<simulation::Command>& commands,
                         const simulation::Vector2& expected) {
  require_same_bits(thrust(commands).direction, expected);
}

// **The sibling of `thrust()`, not a loosening of it.** `thrust()` REQUIREs exactly one command and
// backs every `require_go` and `require_zero` above, so relaxing its count would quietly weaken
// every movement assertion in this file. A pass that also emits an ability comes through here
// instead, and the count is still exact: the thrust first, then the pass's one ability.
[[nodiscard]] simulation::Command ability(const std::vector<simulation::Command>& commands) {
  REQUIRE(commands.size() == 2);
  REQUIRE(std::holds_alternative<simulation::ThrustCommand>(commands.front()));
  return commands.back();
}

// The combat world. The shared observation fixture publishes no opponent, no hazard and no ability
// component, and this step does not own that file, so the combat cases build their own snapshot
// here -- the fixture's own construction narrowed to what a shove needs. Merge it into
// `fixtures/tactical_observation_fixture.hpp` when that file next opens.
//
// The fixed geometry, chosen once so every case below can be read against one picture:
//
//   bot        (200, 320), radius 30, at rest unless a case gives it velocity
//   hill       (600, 100), radius 20 -- the mode candidate, always present, never nearest
//   opponent   wherever a case puts it, radius 30
//   pit        (400, 400), radius 30 -- the badness a standing point is derived from
//
// With the opponent at (400, 320) the pit is directly below it, so `unit(O - Hazard)` is exactly
// (0,-1) and S lands one standoff below the opponent: about 212 units from the bot against the
// hill's 456, which is what makes the shove candidate the nearer of the two kinds.
inline constexpr double kSelfRadius = 30.0;
inline constexpr double kOpponentRadius = 30.0;
inline constexpr std::uint64_t kFirstOpponent = 100;
inline constexpr std::uint64_t kSecondOpponent = 101;
inline constexpr std::uint64_t kThirdOpponent = 102;
inline constexpr std::uint64_t kAbilityActivationTick = 1;
inline constexpr std::uint64_t kExposureStunDurationTicks = 200;
inline constexpr std::uint64_t kSpentCooldownTicks = 400;

// One opponent's published escapes, as the components the exposure quality reads. Only the three
// that mean something outside a mode are authored here: `ZoneExposure` exists under royale alone
// and `HillPresence` under king of the hill alone, and the arithmetic of all five is proven where
// the quality is built, in `tactical_objective_candidates_tests.cpp`. What this file needs is two
// opponents a profile can tell apart, which three terms already give.
struct OpponentExposure final {
  bool stunned{false};
  bool shield_spent{false};
  bool charge_spent{false};
};

struct CombatOpponent final {
  std::uint64_t entity;
  double x;
  double y;
  double velocity_x{0.0};
  double velocity_y{0.0};
  OpponentExposure exposure{};
};

struct CombatFrame final {
  // The bot's own published position, which every case written before the arrival brake leaves at
  // the fixed (200, 320) of the picture above. A brake case moves it onto the hill, because arrival
  // is where the brake lives and the hill is the only kind it reads.
  double self_x{200.0};
  double self_y{320.0};
  double self_velocity_x{0.0};
  double self_velocity_y{0.0};
  double hill_x{600.0};
  double hill_y{100.0};
  double hill_radius{20.0};
  // Published `HillMotion`, seeded only when it is nonzero so that every existing case keeps a hill
  // that publishes no motion at all and a candidate that carries a structural zero.
  double hill_velocity_x{0.0};
  double hill_velocity_y{0.0};
  std::vector<CombatOpponent> opponents{};
  std::vector<simulation::TerrainHole> holes{};
  std::optional<simulation::Shield> shield{};
  std::optional<simulation::Charge> charge{};
  // Committed ticks to advance before observing, which every cooldown case needs because
  // `Shield::activate` and `Charge::activate` refuse tick zero -- the loaded initial state, never a
  // tick a pulse was admitted on -- and which a case with a reaction delay needs to reach a tick
  // past that delay. Nothing is authored to depend on where a step leaves a body: every stepped
  // body here is at rest and does not move at all, except one that only closes further.
  std::uint64_t steps{0};
};

[[nodiscard]] simulation::PhysicsBody combat_body(const double x, const double y,
                                                  const double velocity_x, const double velocity_y,
                                                  const double radius) {
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::PhysicsBody::create(simulation::Vector2::create(x, y),
                                         simulation::Vector2::create(velocity_x, velocity_y), zero)
      .with_radius(radius);
}

[[nodiscard]] simulation::TerrainHole pit(const std::string& name, const double x, const double y,
                                          const double radius) {
  return simulation::TerrainHole::create(name, simulation::Vector2::create(x, y), radius);
}

[[nodiscard]] controllers::Observation combat_observation(const CombatFrame& frame) {
  const auto self = simulation::EntityId::create(frame_fixture::kEntity);
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      self,
      combat_body(frame.self_x, frame.self_y, frame.self_velocity_x, frame.self_velocity_y,
                  kSelfRadius),
      simulation::ControllerId::create(frame_fixture::kController)));
  for (const auto& opponent : frame.opponents) {
    // Two-argument seeding gives an entity its own controller id, which is its entity id, so every
    // body here is one some other controller drives -- the durable identity the shove provider
    // compares, and never this bot's own second body.
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        simulation::EntityId::create(opponent.entity),
        combat_body(opponent.x, opponent.y, opponent.velocity_x, opponent.velocity_y,
                    kOpponentRadius)));
  }
  auto world = simulation::GameWorld::create(std::move(seeds));
  auto& match = world.mutable_match();
  match.phase = simulation::MatchPhase::kRunning;
  match.previous_phase = simulation::MatchPhase::kRunning;
  match.mode_state = simulation::KingOfTheHillModeState{};
  const auto hill = simulation::EntityId::create(frame_fixture::kFirstObjective);
  world.mutable_store<simulation::Hill>().insert_or_assign(
      hill,
      simulation::Hill{simulation::Vector2::create(frame.hill_x, frame.hill_y), frame.hill_radius});
  if (frame.hill_velocity_x != 0.0 || frame.hill_velocity_y != 0.0) {
    world.mutable_store<simulation::HillMotion>().insert_or_assign(
        hill, simulation::HillMotion{
                  simulation::Vector2::create(frame.hill_velocity_x, frame.hill_velocity_y)});
  }
  // The opponents' published escapes. A stun window is plain published state with no activation
  // rule, so it is authored from tick zero and needs no stepped world; the two ability windows do,
  // because `Shield::activate` and `Charge::activate` refuse tick zero and a window opened at tick
  // one still contains tick one. "Spent" is a cooldown that has not ended beside a protection that
  // has, which is how `AbilitySystem` reads them and not bare presence.
  for (const auto& opponent : frame.opponents) {
    const auto entity = simulation::EntityId::create(opponent.entity);
    const auto activation = simulation::TickSequence::create(kAbilityActivationTick);
    if (opponent.exposure.stunned) {
      world.mutable_store<simulation::Stun>().insert_or_assign(
          entity, simulation::Stun{simulation::TickWindow::create(simulation::TickSequence::zero(),
                                                                  kExposureStunDurationTicks)});
    }
    if (opponent.exposure.shield_spent) {
      world.mutable_store<simulation::Shield>().insert_or_assign(
          entity, simulation::Shield::activate(activation, 2, 1, kSpentCooldownTicks, 40));
    }
    if (opponent.exposure.charge_spent) {
      world.mutable_store<simulation::Charge>().insert_or_assign(
          entity, simulation::Charge::activate(activation, kSpentCooldownTicks));
    }
  }
  if (frame.shield) {
    world.mutable_store<simulation::Shield>().insert_or_assign(self, *frame.shield);
  }
  if (frame.charge) {
    world.mutable_store<simulation::Charge>().insert_or_assign(self, *frame.charge);
  }
  auto map = simulation::MapDefinition::create(
      "tactical_combat_fixture",
      simulation::TerrainDefinition::create(simulation::ArenaBounds::create(960.0, 640.0),
                                            simulation::TerrainGround::kSolid, {}, frame.holes),
      {}, {}, simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  for (std::uint64_t tick = 0; tick < frame.steps; ++tick) {
    static_cast<void>(
        game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  }
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(frame_fixture::kController));
}

// The two holes every charge case uses. The standoff pit is the badness; the screen pit is the one
// thing that moves between the refusal and the commitment, and it moves from inside the corridor
// the burst crosses to beyond the opponent's near surface.
[[nodiscard]] std::vector<simulation::TerrainHole> charge_terrain(const double screen_x) {
  return {pit("standoff_pit", 400.0, 400.0, 30.0), pit("screen_pit", screen_x, 320.0, 12.0)};
}
inline constexpr double kScreenInsideTheCorridor = 300.0;
inline constexpr double kScreenBeyondTheOpponent = 560.0;

// The published arena and the standoff a shove candidate's standing point sits at, in the same
// subtraction/product/sqrt order `controller_steering.cpp` and the collector use, so a case that
// stands the bot exactly on S stands it on the binary64 the provider computed.
const double kDiagonal = std::sqrt((960.0 * 960.0) + (640.0 * 640.0));
const double kShoveMargin = kDiagonal * controllers::kTacticalShoveStandoffDiagonalFraction;
const double kShoveStandoff = kSelfRadius + kOpponentRadius + kShoveMargin;

// The arrival brake law, re-derived here in the order `tactical_controller.cpp` writes it --
// subtract, divide, scale, clamp -- and never collapsed into one premultiplied factor, because the
// claim under test is that two toolchains reach the same binary64 and a reassociation is exactly
// what would break it. The clamp is `std::clamp` rather than the controllers helper for the reason
// the aim-error case re-derives its own rotation: an expectation that called production's own
// function at the one line that matters would be a tautology.
//
// `held_ticks` is the committed time one thrust stays in force: the profile's reaction delay, or
// the spacing between this controller's observations, whichever is longer. Every case below is a
// controller's first decision, where that spacing is one tick.
[[nodiscard]] simulation::Vector2
expected_brake(const controllers::Observation& observation, const double objective_velocity_x,
               const double objective_velocity_y, const double body_velocity_x,
               const double body_velocity_y, const double fraction,
               const std::uint64_t held_ticks) {
  const double acceleration = observation.snapshot().match().movement().current.acceleration();
  const double hold_seconds = static_cast<double>(held_ticks) * simulation::kFixedDeltaSeconds;
  const double divisor = acceleration * hold_seconds;
  const double closing_x = objective_velocity_x - body_velocity_x;
  const double closing_y = objective_velocity_y - body_velocity_y;
  return simulation::Vector2::create(std::clamp((closing_x / divisor) * fraction, -1.0, 1.0),
                                     std::clamp((closing_y / divisor) * fraction, -1.0, 1.0));
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
  // No published hill here carries a velocity and this fixture publishes no opponent, so the pass
  // casts escape rays and predicts nothing else: one per screened candidate, a third of the ceiling
  // the two providers together derive, and never a search or a replanning loop.
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
  // The combat group. Each supersedes the movement branch of the pass that reached it, so each is
  // asserted on a pass that would otherwise have read `kPursuing` -- which is what makes the
  // supersession itself visible rather than assumed.
  {
    CombatFrame world;
    world.holes = charge_terrain(kScreenBeyondTheOpponent);
    world.opponents = {{kFirstOpponent, 400.0, 320.0}};
    auto controller = bot(combat(1.0, 0.5, 0));
    CHECK(std::holds_alternative<simulation::ChargeCommand>(
        ability(controller->decide(combat_observation(world)))));
    CHECK(controller->decision_reason() == Reason::kChargeCommitted);
  }
  {
    CombatFrame world;
    world.holes = charge_terrain(kScreenInsideTheCorridor);
    world.opponents = {{kFirstOpponent, 400.0, 320.0}};
    auto controller = bot(combat(1.0, 0.5, 0));
    require_go(controller->decide(combat_observation(world)));
    CHECK(controller->decision_reason() == Reason::kChargeGroundEndsFirst);
  }
  {
    CombatFrame world;
    world.holes = charge_terrain(kScreenBeyondTheOpponent);
    world.opponents = {{kFirstOpponent, 400.0, 320.0}};
    world.self_velocity_y = 600.0;
    auto controller = bot(combat(1.0, 0.5, 0));
    require_go(controller->decide(combat_observation(world)));
    CHECK(controller->decision_reason() == Reason::kChargeMisaligned);
  }
  {
    CombatFrame world;
    world.holes = charge_terrain(kScreenBeyondTheOpponent);
    world.opponents = {{kFirstOpponent, 400.0, 320.0}};
    world.steps = 1;
    world.charge = simulation::Charge::activate(simulation::TickSequence::create(1), 400);
    auto controller = bot(combat(1.0, 0.5, 0));
    require_go(controller->decide(combat_observation(world)));
    CHECK(controller->decision_reason() == Reason::kChargeUnavailable);
  }
  {
    CombatFrame world;
    world.opponents = {{kFirstOpponent, 300.0, 320.0, -500.0, 0.0}};
    auto controller = bot(combat(1.0, 0.5, 40));
    CHECK(std::holds_alternative<simulation::ShieldCommand>(
        ability(controller->decide(combat_observation(world)))));
    CHECK(controller->decision_reason() == Reason::kShieldAnticipated);
  }
  {
    CombatFrame world;
    world.opponents = {{kFirstOpponent, 300.0, 320.0, -500.0, 0.0}};
    world.steps = 1;
    world.shield =
        simulation::Shield::activate(simulation::TickSequence::create(1), 160, 32, 360, 40);
    auto controller = bot(combat(1.0, 0.5, 40));
    require_go(controller->decide(combat_observation(world)));
    CHECK(controller->decision_reason() == Reason::kShieldUnavailable);
  }
}

TEST_CASE("Tactical charge is refused when the ground ends first and taken when it does not",
          "[unit][controllers][tactical][charge]") {
  // **Both halves, because a bot that never charges would pass the refusal alone.** Only the screen
  // pit moves between the two passes: inside the corridor the burst crosses, then beyond the
  // opponent's near surface. Everything else -- profile, geometry, seed -- is the same.
  CombatFrame world;
  world.opponents = {{kFirstOpponent, 400.0, 320.0}};
  world.holes = charge_terrain(kScreenInsideTheCorridor);
  auto refusing = bot(combat(1.0, 0.5, 0));
  const auto refused = refusing->decide(combat_observation(world));
  require_go(refused);
  CHECK(refusing->decision_reason() == Reason::kChargeGroundEndsFirst);
  CHECK(refusing->target_key() ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kShoveSetup,
                                          kFirstOpponent});

  world.holes = charge_terrain(kScreenBeyondTheOpponent);
  auto charging = bot(combat(1.0, 0.5, 0));
  const auto committed = charging->decide(combat_observation(world));
  const auto burst = ability(committed);
  REQUIRE(std::holds_alternative<simulation::ChargeCommand>(burst));
  CHECK(charging->decision_reason() == Reason::kChargeCommitted);
  // **The burst is aimed at the opponent and not at the standing point.** S is one standoff behind
  // the opponent, so a charge along `B -> S` would push nothing anywhere; the commanded ray is the
  // exact unit offset to the published body.
  require_same_bits(std::get<simulation::ChargeCommand>(burst).direction,
                    simulation::Vector2::create(1.0, 0.0));
  CHECK(charging->target_key() ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kShoveSetup,
                                          kFirstOpponent});
  // The thrust still rides beside it: a bot that is steering somewhere is still the bot that has to
  // reach the standing point it chose.
  CHECK(std::holds_alternative<simulation::ThrustCommand>(committed.front()));
  // **No new draw anywhere.** The refusing pass and the charging pass each spend exactly the seek
  // draw and the aim draw Step 15 put there, in that order, which is the whole of what keeps every
  // authored profile's stream where it was.
  CHECK(refusing->draw_count() == 2);
  CHECK(charging->draw_count() == 2);
}

TEST_CASE("Tactical charge alignment refuses a burst the screened corridor would not describe",
          "[unit][controllers][tactical][charge]") {
  // The burst is additive, so committed velocity across the commanded ray decides whether the
  // corridor the screen certified is the corridor the body will cross. Only that velocity moves.
  CombatFrame world;
  world.opponents = {{kFirstOpponent, 400.0, 320.0}};
  world.holes = charge_terrain(kScreenBeyondTheOpponent);
  world.self_velocity_y = 600.0;
  auto crossing = bot(combat(1.0, 0.5, 0));
  require_go(crossing->decide(combat_observation(world)));
  CHECK(crossing->decision_reason() == Reason::kChargeMisaligned);
  // Speed *along* the ray is not misalignment at any magnitude, which is the distinction the gate
  // exists to draw: the same body moving just as fast straight at the opponent is admitted.
  world.self_velocity_y = 0.0;
  world.self_velocity_x = 600.0;
  auto aligned = bot(combat(1.0, 0.5, 0));
  CHECK(std::holds_alternative<simulation::ChargeCommand>(
      ability(aligned->decide(combat_observation(world)))));
  CHECK(aligned->decision_reason() == Reason::kChargeCommitted);
  CHECK(crossing->draw_count() == aligned->draw_count());
}

TEST_CASE("Tactical shield is raised on a predicted close and suppressed by a visible cooldown",
          "[unit][controllers][tactical][shield]") {
  // No hazard on this map, so no shove candidate exists and the hill is what the bot pursues: the
  // shield answers whoever is arriving and is deliberately not scoped to the selected candidate.
  CombatFrame world;
  world.opponents = {{kFirstOpponent, 300.0, 320.0, -500.0, 0.0}};
  auto anticipating = bot(combat(1.0, 0.5, 40));
  const auto raised = anticipating->decide(combat_observation(world));
  CHECK(std::holds_alternative<simulation::ShieldCommand>(ability(raised)));
  CHECK(anticipating->decision_reason() == Reason::kShieldAnticipated);
  CHECK(anticipating->target_key()->kind == controllers::TacticalObjectiveKind::kHill);
  // **Not otherwise**, and by the authored window rather than by the geometry: the same closing
  // opponent inside a window too short to reach it raises nothing, and a zero window answers before
  // extrapolating at all. Both passes still steer, so this is a declined ability and not a declined
  // decision.
  auto brief = bot(combat(1.0, 0.5, 8));
  require_go(brief->decide(combat_observation(world)));
  CHECK(brief->decision_reason() == Reason::kPursuing);
  auto blind = bot(combat(1.0, 0.5, 0));
  require_go(blind->decide(combat_observation(world)));
  CHECK(blind->decision_reason() == Reason::kPursuing);
  // A pulse this bot can already see will be refused is declined locally. It costs nothing to send
  // one -- the tick refuses it, consuming no cooldown and queueing nothing -- so this is the honest
  // answer to the rate asymmetry a bot enjoys rather than a second rate authority.
  world.steps = 1;
  world.shield =
      simulation::Shield::activate(simulation::TickSequence::create(1), 160, 32, 360, 40);
  auto guarded = bot(combat(1.0, 0.5, 40));
  require_go(guarded->decide(combat_observation(world)));
  CHECK(guarded->decision_reason() == Reason::kShieldUnavailable);
  // **The ability costs no randomness.** Two bots on the same world, one that pulses and one whose
  // window is zero, have drawn the same number of times when the pass ends.
  CHECK(anticipating->draw_count() == 2);
  CHECK(blind->draw_count() == anticipating->draw_count());
}

TEST_CASE("Tactical passes emit at most one ability and prefer the shield the tick prefers",
          "[unit][controllers][tactical][shield][charge]") {
  // A world that wants both: a screened, aligned, admissible charge at one opponent and a second
  // opponent predicted into contact. `AbilitySystem` spells its own priority
  // `charge_admissible && !shield_eligible`, so a local order that preferred offence would be one
  // the tick contradicts on exactly this pass.
  CombatFrame world;
  world.holes = charge_terrain(kScreenBeyondTheOpponent);
  world.opponents = {{kFirstOpponent, 400.0, 320.0}, {kSecondOpponent, 300.0, 320.0, -500.0, 0.0}};
  auto controller = bot(combat(1.0, 0.5, 40));
  const auto commands = controller->decide(combat_observation(world));
  CHECK(std::holds_alternative<simulation::ShieldCommand>(ability(commands)));
  CHECK(controller->decision_reason() == Reason::kShieldAnticipated);
  // The nearer opponent's standing point is what the bot is steering at, so the pass had a charge
  // available and still sent one command, not two.
  CHECK(controller->target_key()->kind == controllers::TacticalObjectiveKind::kShoveSetup);
  CHECK(controller->draw_count() == 2);
}

TEST_CASE("Tactical shove weight and not the seed chooses between an opponent and the hill",
          "[unit][controllers][tactical][utility][shove]") {
  // **The first non-vacuous weight proof in this tree.** Every shipped mode yields at most one
  // candidate, so until the opponent-derived provider existed a weight was a common factor over the
  // whole set and could not reorder anything. Here the set holds two kinds: the standing point
  // behind the opponent, 212 units away, and the hill at 456. At equal weights the nearer one wins;
  // at a zero shove weight there is no shove candidate at all and the hill is what is left.
  //
  // **The zero is a provider-level skip and not a selection-level veto**, which is what the
  // screened counts below now say: the racer's set is one candidate short rather than holding one
  // it can never prefer. The outcome is the same hill, and deliberately so -- a skip changes what
  // the set contains and never what a zero weight means for the four mode kinds.
  CombatFrame world;
  world.opponents = {{kFirstOpponent, 400.0, 320.0}};
  world.holes = charge_terrain(kScreenInsideTheCorridor);
  const auto observation = combat_observation(world);
  auto bully = bot(combat(1.0, 0.5, 0));
  auto racer = bot(combat(0.0, 0.5, 0));
  const auto bully_commands = bully->decide(observation);
  const auto racer_commands = racer->decide(observation);
  // Same authored name, so both mix the same seed and draw the same stream. The only difference
  // between these two bots is one authored weight, which is what makes the divergence below a proof
  // about the weight instead of a proof about `tactical_seed_for`.
  CHECK(bully->profile().name() == racer->profile().name());
  CHECK(bully->current_seed() == racer->current_seed());
  CHECK(bully->draw_count() == racer->draw_count());
  require_go(bully_commands);
  require_go(racer_commands);
  CHECK(bully->target_key() ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kShoveSetup,
                                          kFirstOpponent});
  CHECK(racer->target_key() ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kHill,
                                          frame_fixture::kFirstObjective});
  // Two kinds in one screened set for the profile that weights both, which is the condition that
  // makes a weight mean anything -- and one kind for the profile that skipped the provider, which
  // is the condition that makes the skip observable at all.
  CHECK(bully->objective_work().screened_candidate_count == 2);
  CHECK(bully->objective_work().raw_candidate_count == 2);
  CHECK(racer->objective_work().screened_candidate_count == 1);
  CHECK(racer->objective_work().raw_candidate_count == 1);
  // A skipped provider is not a fallback: the racer still decides, still steers, and still reports
  // a pursuing branch. It also never reaches a charge, because there is no shove candidate to aim
  // one at, which is the second thing the skip saves.
  CHECK(racer->decision_reason() == Reason::kPursuing);
  CHECK(bully->decision_reason() == Reason::kChargeGroundEndsFirst);
}

TEST_CASE("Tactical arrival brake nulls a relative velocity without overshoot and a zero fraction "
          "coasts bit for bit",
          "[unit][controllers][tactical][arrival]") {
  // The bot stands exactly on the published hill centre, so it is arrived by any radius and the
  // approach branch is unreachable: every thrust below is the whole of what `kArrived` emits.
  const auto standing = [](const double self_velocity_x, const double self_velocity_y,
                           const double hill_velocity_x) {
    CombatFrame world;
    world.self_x = 480.0;
    world.self_y = 320.0;
    world.hill_x = 480.0;
    world.hill_y = 320.0;
    world.hill_radius = 60.0;
    world.self_velocity_x = self_velocity_x;
    world.self_velocity_y = self_velocity_y;
    world.hill_velocity_x = hill_velocity_x;
    return world;
  };
  const auto coast = simulation::Vector2::create(0.0, 0.0);

  // **At rest it emits exactly (0, 0), and there is no deadband anywhere.** The law divides the
  // velocity *vector* componentwise by a positive scalar and never by its own magnitude, so no 0/0
  // can arise, no component can be NaN, and `Vector2::create` has nothing to refuse on the terminal
  // state of every successful capture -- a throw there would be caught by `ControllerHost` and
  // would leave this controller repeating the same pass forever.
  {
    auto controller = bot(braking(1.0));
    const auto commands = controller->decide(combat_observation(standing(0.0, 0.0, 0.0)));
    CHECK(controller->decision_reason() == Reason::kArrived);
    require_zero(commands);
    require_thrust_bits(commands, coast);
    CHECK(controller->draw_count() == 0);
  }
  // **Relative, and not absolute.** A bot already matching the hill's own published motion has
  // nothing to null and emits that same exact coast at a full brake fraction.
  {
    auto controller = bot(braking(1.0));
    const auto commands = controller->decide(combat_observation(standing(100.0, 0.0, 100.0)));
    CHECK(controller->decision_reason() == Reason::kArrived);
    require_thrust_bits(commands, coast);
  }
  // A real subunit command, which is what the third helper exists for and what
  // `normalized_thrust_intent` already admits: it is a magnitude clamp and not a normaliser, and
  // `ChaserController` has shipped `unit * aggression_weight` since long before this.
  {
    const auto observation = combat_observation(standing(0.75, -0.25, 0.25));
    auto controller = bot(braking(0.5));
    const auto commands = controller->decide(observation);
    CHECK(controller->decision_reason() == Reason::kArrived);
    const auto expected = expected_brake(observation, 0.25, 0.0, 0.75, -0.25, 0.5, 1);
    require_thrust_bits(commands, expected);
    const double magnitude =
        std::sqrt((expected.x() * expected.x()) + (expected.y() * expected.y()));
    CHECK(magnitude > 0.0);
    CHECK(magnitude < 1.0);
    // It brakes *against* the closing velocity in both components, which is the one direction that
    // removes speed and the only sign the law can get wrong.
    CHECK(expected.x() < 0.0);
    CHECK(expected.y() > 0.0);
  }
  // **It cannot overshoot.** The unscaled quotient is exactly the thrust that nulls the relative
  // velocity over one command hold in the drag-free case, so where more than full thrust would be
  // needed the componentwise clamp caps it at full thrust rather than asking for more. Drag only
  // removes further speed, so a nonzero drag makes this undershoot, and an undershoot corrects
  // itself on the next pass.
  {
    const auto observation =
        combat_observation(standing(simulation::kDefaultNormalTopSpeed, 0.0, 0.0));
    auto controller = bot(braking(1.0));
    require_thrust_bits(controller->decide(observation), simulation::Vector2::create(-1.0, 0.0));
  }
  // **A zero fraction is the Step 22b coast, bit for bit**, and it is asserted on the world that
  // would otherwise produce the largest brake this law can ask for -- a still world cannot tell a
  // coast from a brake with nothing left to null. This is what keeps every shipped `kArrived`
  // assertion and both browser fixtures' motionless pins unchanged.
  {
    const auto world =
        standing(simulation::kDefaultNormalTopSpeed, -simulation::kDefaultNormalTopSpeed, 0.0);
    auto coasting = bot(braking(0.0));
    const auto commands = coasting->decide(combat_observation(world));
    CHECK(coasting->decision_reason() == Reason::kArrived);
    require_zero(commands);
    require_thrust_bits(commands, coast);
    // The shared fixture authors the same zero, which is why every arrived case in this file that
    // was written before the brake existed still coasts by rule rather than by luck.
    auto shared = bot();
    require_thrust_bits(shared->decide(combat_observation(world)), coast);
  }
  // **`kHill` only, and the kind is what gates it rather than the objective's velocity.** A bot
  // standing exactly on a shove candidate's standing point is arrived on a `kShoveSetup` and coasts
  // at a full brake fraction however fast it is moving. The zone is why the brake is not general: a
  // `kZone` candidate's arrival radius is the zone's own, `zone_full_radius` is the arena
  // half-diagonal, and the shipped configuration authors `mode=royale` -- so a zone brake would be
  // a permanent parking brake on every bot in it. `kShoveSetup` is deferred, not impossible.
  {
    CombatFrame world;
    world.self_x = 400.0;
    world.self_y = 320.0 - kShoveStandoff;
    world.self_velocity_x = simulation::kDefaultNormalTopSpeed;
    world.holes = {pit("standoff_pit", 400.0, 400.0, 30.0)};
    world.opponents = {{kFirstOpponent, 400.0, 320.0}};
    auto controller = bot(braking(1.0));
    const auto commands = controller->decide(combat_observation(world));
    CHECK(controller->target_key() ==
          controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kShoveSetup,
                                            kFirstOpponent});
    // Standing on it, which is what makes this the arrived branch and not the approach.
    CHECK(controller->target() == simulation::Vector2::create(400.0, 320.0 - kShoveStandoff));
    require_thrust_bits(commands, coast);
    // The movement branch was the arrived one; the reason names the ability this pass also refused,
    // because a velocity across the commanded ray is exactly what the alignment gate exists for.
    CHECK(controller->decision_reason() == Reason::kChargeMisaligned);
  }
}

TEST_CASE("Tactical four authored personalities decide four different objectives on one identical "
          "observation",
          "[unit][controllers][tactical][utility][personality]") {
  // **The step's headline claim, as one comparison rather than four assertions.** Four profiles,
  // one world, one observation, and one shared profile *name* -- so `tactical_seed_for` mixes the
  // same bytes for all four, every one of them draws the identical stream, and the only thing left
  // that can separate their decisions is the authored numbers. That is what "a personality is
  // numbers and never a code path" has to mean to be provable at all.
  //
  // The picture, all of it inside a 960x640 arena with the bot near the south wall:
  //
  //   bot        (480, 100)                the shared self, at rest
  //   hill       (480,  40), radius 20     60 away, and escape-blocked: the arena edge is 100 out
  //                                        along the approach and the shortest authored horizon
  //                                        reaches 120
  //   east       (652, 100)                nothing published against it: no exposure at all
  //   west       (258, 100)                stunned
  //   north      (480, 564)                stunned, shield spent, charge spent
  //
  // Each opponent has its own hazard directly beyond it, so each standing point sits between the
  // bot and that opponent and the three are at three clearly different distances -- east nearest,
  // then west, then north.
  const auto world_at = [](const std::uint64_t steps) {
    CombatFrame world;
    world.self_x = 480.0;
    world.self_y = 100.0;
    world.hill_x = 480.0;
    world.hill_y = 40.0;
    world.hill_radius = 20.0;
    world.steps = steps;
    // Every bot here carries a charge on cooldown, so no pass can emit an ability beside its thrust
    // and the movement decision is what is being compared rather than the ability path beside it.
    world.charge = simulation::Charge::activate(
        simulation::TickSequence::create(kAbilityActivationTick), kSpentCooldownTicks);
    world.holes = {pit("hazard_east", 760.0, 100.0, 8.0), pit("hazard_west", 150.0, 100.0, 8.0),
                   pit("hazard_north", 480.0, 604.0, 8.0)};
    world.opponents = {{kFirstOpponent, 652.0, 100.0},
                       {kSecondOpponent, 258.0, 100.0, 0.0, 0.0, {.stunned = true}},
                       {kThirdOpponent,
                        480.0,
                        564.0,
                        0.0,
                        0.0,
                        {.stunned = true, .shield_spent = true, .charge_spent = true}}};
    return world;
  };
  // Each personality authors its own reaction delay, from the opportunist's twenty ticks to the
  // cautious racer's hundred, and a controller's first eligible pass *opens* that window rather
  // than deciding through it. So the four are brought to their first decision on one earlier
  // observation of the same world and then handed the one identical observation this case is
  // about, at a tick past the longest authored delay.
  const auto warm_up = combat_observation(world_at(1));
  const auto decisive = combat_observation(world_at(105));

  auto keeper = bot(keeper_section());
  auto bully = bot(bully_section());
  auto opportunist = bot(opportunist_section());
  auto cautious = bot(cautious_racer_section());
  for (auto* const controller : {keeper.get(), bully.get(), opportunist.get(), cautious.get()}) {
    CHECK(controller->decide(warm_up).empty());
    CHECK(controller->decision_reason() == Reason::kAwaitingReaction);
  }
  require_go(keeper->decide(decisive));
  require_go(bully->decide(decisive));
  require_go(opportunist->decide(decisive));
  require_go(cautious->decide(decisive));

  using Key = controllers::TacticalObjectiveKey;
  using Kind = controllers::TacticalObjectiveKind;
  REQUIRE(keeper->target_key().has_value());
  REQUIRE(bully->target_key().has_value());
  REQUIRE(opportunist->target_key().has_value());
  REQUIRE(cautious->target_key().has_value());
  const std::array<Key, 4> chosen{*keeper->target_key(), *bully->target_key(),
                                  *opportunist->target_key(), *cautious->target_key()};
  for (std::size_t left = 0; left < chosen.size(); ++left) {
    for (std::size_t right = left + 1; right < chosen.size(); ++right) {
      INFO(left << " against " << right);
      CHECK_FALSE(chosen[left] == chosen[right]);
    }
  }
  // Keeper weights the shove kind at zero, so the opponent provider never ran and the hill is the
  // only thing it could have chosen -- "defend a stable interior", literally.
  CHECK(chosen[0] == Key{Kind::kHill, frame_fixture::kFirstObjective});
  // Bully scores no exposure and floors nothing, so all three fights are on offer and it takes the
  // nearest. This is precisely what separates it from the opportunist below.
  CHECK(chosen[1] == Key{Kind::kShoveSetup, kFirstOpponent});
  // Opportunist scores exposure in full and floors at a half, so the bare east opponent is not a
  // fight it will take at all, and between the two that are left the most exposed wins over the
  // nearer one.
  CHECK(chosen[2] == Key{Kind::kShoveSetup, kThirdOpponent});
  // The cautious racer reads the same two openings through a half preference and a higher floor,
  // which compresses them -- so the nearer fight wins where the opportunist took the richer one.
  // Its shove weight is a deliberate eighth rather than a zero: "avoid expensive fights" is not
  // "never fight", and the floor is what decides which ones are cheap.
  CHECK(chosen[3] == Key{Kind::kShoveSetup, kSecondOpponent});

  // The set each was choosing from, which is where the two provider-level rules become visible:
  // keeper's zero weight skipped the opponent scan outright, and the two profiles that score
  // exposure dropped the one opponent with nothing published against it.
  CHECK(keeper->objective_work().screened_candidate_count == 1);
  CHECK(bully->objective_work().screened_candidate_count == 4);
  CHECK(opportunist->objective_work().screened_candidate_count == 3);
  CHECK(cautious->objective_work().screened_candidate_count == 3);
  // Keeper never reaches a charge: a hill candidate is no body to aim a burst at, so the movement
  // branch's reason stands, and it is the risk-taking one because the hill's approach runs at the
  // arena edge. The other three are refused by the cooldown this world publishes.
  CHECK(keeper->decision_reason() == Reason::kPursuingUnderRisk);
  CHECK(bully->decision_reason() == Reason::kChargeUnavailable);
  CHECK(opportunist->decision_reason() == Reason::kChargeUnavailable);
  CHECK(cautious->decision_reason() == Reason::kChargeUnavailable);

  // **Same name, same identity, same seed, same stream.** Every divergence above is an authored
  // number, and none of it is `tactical_seed_for`.
  CHECK(keeper->profile().name() == bully->profile().name());
  CHECK(keeper->profile().name() == opportunist->profile().name());
  CHECK(keeper->profile().name() == cautious->profile().name());
  CHECK(keeper->current_seed() == bully->current_seed());
  CHECK(keeper->current_seed() == opportunist->current_seed());
  CHECK(keeper->current_seed() == cautious->current_seed());
  // **The draw count is unchanged by everything this step added.** Two per deciding pass -- the
  // seek and the aim Step 15 put there, in that order -- and nothing at all on the warm-up, on the
  // brake, on the exposure quality, on the opening floor or on the skipped provider.
  CHECK(keeper->draw_count() == 2);
  CHECK(bully->draw_count() == 2);
  CHECK(opportunist->draw_count() == 2);
  CHECK(cautious->draw_count() == 2);
}

TEST_CASE("Tactical minimum opening abandons a fight whose opening closed and re-arms the reaction "
          "window",
          "[unit][controllers][tactical][shove][exposure]") {
  // One opponent, its hazard behind it, and the screen pit inside the corridor so the charge is
  // refused and every deciding pass emits exactly one command. The only thing that moves between
  // the two decisions is the opponent's published stun, which is the whole of its exposure here.
  const auto world_at = [](const std::uint64_t steps, const bool stunned) {
    CombatFrame world;
    world.steps = steps;
    world.holes = charge_terrain(kScreenInsideTheCorridor);
    world.opponents = {{kFirstOpponent, 400.0, 320.0, 0.0, 0.0, {.stunned = stunned}}};
    return world;
  };
  auto section = combat(1.0, 0.5, 0);
  section.reaction_delay_ticks = 2;
  section.objective_weights.hill = 0.25;
  section.exposure_preference = 1.0;
  section.minimum_opening = 0.25;
  auto controller = bot(section);
  CHECK(controller->decide(combat_observation(world_at(1, true))).empty());
  CHECK(controller->decision_reason() == Reason::kAwaitingReaction);
  // A stunned opponent's opening is the stun weight alone, which clears the floor, so the fight is
  // taken and leased over the hill this profile weights at a quarter.
  require_go(controller->decide(combat_observation(world_at(3, true))));
  CHECK(controller->target_key() ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kShoveSetup,
                                          kFirstOpponent});
  CHECK(controller->target_hold() == Hold::kAcquired);
  CHECK(controller->decision_reason() == Reason::kChargeGroundEndsFirst);
  CHECK(controller->objective_work().screened_candidate_count == 2);
  CHECK(controller->draw_count() == 2);
  // **The opening closed, and the candidate is absent rather than unselectable.** The leased key is
  // no longer published, so this takes the same `kLost` path a disappearing objective takes: the
  // lease is released, the held input is cancelled with an explicit zero, and the reaction window
  // is re-armed from this tick rather than from the acquisition. It costs no draw, because the pass
  // returns before the seek.
  const auto abandoned = controller->decide(combat_observation(world_at(5, false)));
  require_zero(abandoned);
  CHECK_FALSE(controller->target_key());
  CHECK(controller->target_hold() == Hold::kLost);
  CHECK(controller->decision_reason() == Reason::kAwaitingReaction);
  REQUIRE(controller->reaction_window().has_value());
  CHECK(controller->reaction_window()->activation_tick().value() == 5);
  CHECK(controller->reaction_window()->expiry_tick().value() == 7);
  CHECK(controller->objective_work().screened_candidate_count == 1);
  CHECK(controller->draw_count() == 2);
  // The hill is still there, so the next decision acquires it: abandoning a fight leaves a bot
  // choosing again, never inert.
  require_go(controller->decide(combat_observation(world_at(7, false))));
  CHECK(controller->target_key()->kind == controllers::TacticalObjectiveKind::kHill);
  CHECK(controller->target_hold() == Hold::kAcquired);
  CHECK(controller->decision_reason() == Reason::kPursuing);
  CHECK(controller->draw_count() == 4);
}

TEST_CASE("Tactical a zero exposure preference decides identically whatever an opponent has spent",
          "[unit][controllers][tactical][shove][exposure]") {
  // **This is what protects both browser fixtures.** They author `exposure_preference=0`, so their
  // bots have to decide exactly as they did before the opening existed -- not nearly, and not to a
  // tolerance. Two worlds differing in nothing but one opponent's published escapes, a profile that
  // scores none of them, and the same command down to the last bit.
  const auto world_with = [](const bool exposed) {
    CombatFrame world;
    world.steps = 5;
    world.holes = charge_terrain(kScreenInsideTheCorridor);
    world.opponents = {{kFirstOpponent,
                        400.0,
                        320.0,
                        0.0,
                        0.0,
                        {.stunned = exposed, .shield_spent = exposed, .charge_spent = exposed}}};
    return world;
  };
  auto against_exposed = bot(combat(1.0, 0.5, 0));
  auto against_bare = bot(combat(1.0, 0.5, 0));
  const auto exposed_commands = against_exposed->decide(combat_observation(world_with(true)));
  const auto bare_commands = against_bare->decide(combat_observation(world_with(false)));
  require_same_bits(thrust(exposed_commands).direction, thrust(bare_commands).direction);
  CHECK(against_exposed->target_key() == against_bare->target_key());
  CHECK(against_exposed->decision_reason() == against_bare->decision_reason());
  CHECK(against_exposed->draw_count() == against_bare->draw_count());
  // **And the same two worlds do separate a profile that scores exposure**, which is what makes the
  // paragraph above a property of the authored zero rather than of a world where nothing was at
  // stake. Its hill weight is a quarter, so the fight it will take is worth more than the hill and
  // the fight it will not take is worth nothing at all.
  auto scoring = combat(1.0, 0.5, 0);
  scoring.objective_weights.hill = 0.25;
  scoring.exposure_preference = 1.0;
  auto takes_it = bot(scoring);
  auto leaves_it = bot(scoring);
  require_go(takes_it->decide(combat_observation(world_with(true))));
  require_go(leaves_it->decide(combat_observation(world_with(false))));
  CHECK(takes_it->target_key()->kind == controllers::TacticalObjectiveKind::kShoveSetup);
  CHECK(leaves_it->target_key()->kind == controllers::TacticalObjectiveKind::kHill);
  CHECK(takes_it->draw_count() == leaves_it->draw_count());
}
