#include "components/charge_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "controller_observation_queries.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "fixed_delta.hpp"
#include "fixtures/tactical_observation_fixture.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "tactical_objective_candidates.hpp"
#include "terrain_definition.hpp"
#include "tick_window.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tactical_observation_fixture;

namespace {
// Every existing case keeps the policy Step 15 effectively had: no weights, no tolerance and a zero
// horizon, which casts no escape ray and predicts no hill, so those assertions still mean exactly
// what they meant.
[[nodiscard]] controllers::TacticalObjectiveCandidates
candidates(const fixture::Frame& frame, const controllers::TacticalObjectivePolicy& policy = {}) {
  const auto observation = fixture::observation(frame);
  const auto* body = controllers::find_observed_component<simulation::PhysicsBody>(
      observation.snapshot(), *observation.entity());
  REQUIRE(body != nullptr);
  return controllers::collect_tactical_objective_candidates(observation, *body, policy);
}
void require_rejection(const fixture::Frame& frame,
                       const controllers::ControllersValidationCode code) {
  try {
    static_cast<void>(candidates(frame));
    FAIL("invalid published objective must fail visibly");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() == code);
  }
}

[[nodiscard]] controllers::TacticalObjectivePolicy
policy(const double weight, const double risk_tolerance, const std::uint64_t horizon_ticks) {
  controllers::TacticalObjectivePolicy value;
  value.objective_weights.fill(weight);
  value.risk_tolerance = risk_tolerance;
  value.prediction_horizon_ticks = horizon_ticks;
  return value;
}

[[nodiscard]] controllers::TacticalObjectiveCandidate
scored(const controllers::TacticalObjectiveKind kind, const std::uint64_t subject,
       const double normalized_distance, const bool escape_blocked) {
  return {{kind, subject}, simulation::Vector2::create(0.0, 0.0), 0.0, 0.0, normalized_distance,
          escape_blocked};
}

// The shared observation fixture publishes no `HillMotion`, and this step does not own that file,
// so the cases that need a committed hill velocity build their own world here. It is the fixture's
// own construction, narrowed to what an intercept needs; merge it into
// `fixtures/tactical_observation_fixture.hpp` as a per-circle velocity when that file next opens.
[[nodiscard]] controllers::Observation moving_hills(const std::size_t count,
                                                    const simulation::Vector2& velocity) {
  const auto self = simulation::EntityId::create(fixture::kEntity);
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      self, simulation::PhysicsBody::create(simulation::Vector2::create(200.0, 320.0), zero, zero),
      simulation::ControllerId::create(fixture::kController)));
  auto world = simulation::GameWorld::create(std::move(seeds));
  auto& match = world.mutable_match();
  match.phase = simulation::MatchPhase::kRunning;
  match.previous_phase = simulation::MatchPhase::kRunning;
  match.mode_state = simulation::KingOfTheHillModeState{};
  for (std::size_t index = 0; index < count; ++index) {
    const auto hill = simulation::EntityId::create(fixture::kFirstObjective + index);
    world.mutable_store<simulation::Hill>().insert_or_assign(
        hill, simulation::Hill{simulation::Vector2::create(600.0, 320.0), 20.0});
    world.mutable_store<simulation::HillMotion>().insert_or_assign(
        hill, simulation::HillMotion{velocity});
  }
  auto map = simulation::MapDefinition::create(
      "tactical_hill_motion_fixture",
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(960.0, 640.0)), {}, {},
      simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(fixture::kController));
}

[[nodiscard]] controllers::TacticalObjectiveCandidates
moving_hill_candidates(const controllers::Observation& observation,
                       const controllers::TacticalObjectivePolicy& value) {
  const auto* body = controllers::find_observed_component<simulation::PhysicsBody>(
      observation.snapshot(), *observation.entity());
  REQUIRE(body != nullptr);
  return controllers::collect_tactical_objective_candidates(observation, *body, value);
}

// The shared observation fixture publishes no opponent and no lethal entity, and this step does not
// own that file either, so the shove cases build their own world here on the same terms
// `moving_hills` above already does. Merge both into `fixtures/tactical_observation_fixture.hpp`
// when that file next opens.
inline constexpr double kSelfRadius = 30.0;
inline constexpr double kOpponentRadius = 30.0;
inline constexpr std::uint64_t kFirstOpponent = 100;
inline constexpr std::uint64_t kSecondOpponent = 101;
inline constexpr std::uint64_t kNearOpponent = 200;
inline constexpr std::uint64_t kHazardEntity = 300;

// The published arena and the two scales a standing point is built from, written in the same
// subtraction/product/sqrt order `controller_steering.cpp` uses so that an expected S below is the
// binary64 the provider computed and not a value that merely rounds to it.
const double kDiagonal = std::sqrt((960.0 * 960.0) + (640.0 * 640.0));
const double kShoveMargin = kDiagonal * controllers::kTacticalShoveStandoffDiagonalFraction;
const double kShoveStandoff = kSelfRadius + kOpponentRadius + kShoveMargin;

// **A spent ability cannot be expressed at tick zero**: `Shield::activate` and `Charge::activate`
// both refuse a zero activation as the loaded initial state, and a window opened at tick one still
// contains tick one, so "the protection has ended and the cooldown has not" is only reachable from
// a stepped world. A stun window carries no such rule -- it is plain published state -- so it is
// authored from tick zero and is the one term a case can read without stepping at all. Only the
// single-opponent case steps, which is deliberate: the two cases below seat two opponents at one
// published position to make their candidates exactly equidistant, and stepping bodies that share
// a point would ask the solver to separate them.
inline constexpr std::uint64_t kExposureActivationTick = 1;
inline constexpr std::uint64_t kExposureObservedTick = 5;
inline constexpr std::uint64_t kExposureStunDurationTicks = 40;
inline constexpr std::uint64_t kSpentShieldDurationTicks = 2;
inline constexpr std::uint64_t kProtectingShieldDurationTicks = 200;
inline constexpr std::uint64_t kSpentCooldownTicks = 400;
inline constexpr std::uint64_t kParryStunDurationTicks = 40;

// One opponent's five published escapes, authored as the components the quality actually looks for
// rather than as a number it is told. `shield_protecting` is the sixth flag and the one that is not
// a term: a guarded body is the *least* exposed thing on the map, so it exists to prove that the
// shield term reads spentness the way `AbilitySystem` does and not bare presence.
struct OpponentExposure final {
  bool stunned{false};
  bool shield_spent{false};
  bool shield_protecting{false};
  bool charge_spent{false};
  bool outside_zone{false};
  bool holding_hill{false};
};

struct CombatOpponent final {
  std::uint64_t entity;
  double x;
  double y;
  double velocity_x{0.0};
  double velocity_y{0.0};
  double radius{kOpponentRadius};
  OpponentExposure exposure{};
};

struct CombatFrame final {
  bool race{false};
  double self_x{200.0};
  double self_y{320.0};
  double self_velocity_x{0.0};
  double self_velocity_y{0.0};
  double self_radius{kSelfRadius};
  std::vector<CombatOpponent> opponents{};
  std::vector<simulation::TerrainHole> holes{};
  // A published `LethalOnContact` body, seeded straight into the stores so it carries no
  // `Controllable` and therefore cannot also be read as an opponent.
  std::optional<simulation::Vector2> lethal{};
  std::size_t hills{1};
  double hill_x{600.0};
  double hill_y{100.0};
  double hill_velocity_x{0.0};
  bool race_progress{true};
  std::uint64_t checkpoint{1};
  // Committed ticks to advance before observing. Zero -- the loaded initial state -- is what every
  // case written before the exposure quality uses, and stepping is needed only where an ability
  // window has to have ended.
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

[[nodiscard]] controllers::Observation combat_observation(const CombatFrame& frame) {
  const auto self = simulation::EntityId::create(fixture::kEntity);
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      self,
      combat_body(frame.self_x, frame.self_y, frame.self_velocity_x, frame.self_velocity_y,
                  frame.self_radius),
      simulation::ControllerId::create(fixture::kController)));
  for (const auto& opponent : frame.opponents) {
    // Two-argument seeding gives an entity its own controller id, which is its entity id, so every
    // opponent here is a body some *other* controller drives -- which is exactly the test the shove
    // provider applies, and it is applied to the durable identity rather than to a resolved entity.
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        simulation::EntityId::create(opponent.entity),
        combat_body(opponent.x, opponent.y, opponent.velocity_x, opponent.velocity_y,
                    opponent.radius)));
  }
  auto world = simulation::GameWorld::create(std::move(seeds));
  auto& match = world.mutable_match();
  match.phase = simulation::MatchPhase::kRunning;
  match.previous_phase = simulation::MatchPhase::kRunning;
  if (frame.race) {
    match.mode_state = fixture::race_course();
    if (frame.race_progress) {
      world.mutable_store<simulation::RaceProgress>().insert_or_assign(
          self, simulation::RaceProgress{frame.checkpoint});
    }
  } else {
    match.mode_state = simulation::KingOfTheHillModeState{};
    for (std::size_t index = 0; index < frame.hills; ++index) {
      const auto hill = simulation::EntityId::create(fixture::kFirstObjective + index);
      world.mutable_store<simulation::Hill>().insert_or_assign(
          hill, simulation::Hill{simulation::Vector2::create(frame.hill_x, frame.hill_y), 20.0});
      if (frame.hill_velocity_x != 0.0) {
        world.mutable_store<simulation::HillMotion>().insert_or_assign(
            hill, simulation::HillMotion{simulation::Vector2::create(frame.hill_velocity_x, 0.0)});
      }
    }
  }
  // The exposure components, written straight into the stores the quality reads. Nothing derives
  // them: `ZoneExposure` and `HillPresence` are erasure-based presence flags whose owning systems
  // do not run behind `engine_defaults`, and the two ability windows are the published values
  // `AbilitySystem` would have left, so authoring them here is authoring the same public state a
  // running mode would have published.
  for (const auto& opponent : frame.opponents) {
    const auto entity = simulation::EntityId::create(opponent.entity);
    const auto activation = simulation::TickSequence::create(kExposureActivationTick);
    if (opponent.exposure.stunned) {
      world.mutable_store<simulation::Stun>().insert_or_assign(
          entity, simulation::Stun{simulation::TickWindow::create(simulation::TickSequence::zero(),
                                                                  kExposureStunDurationTicks)});
    }
    if (opponent.exposure.shield_spent) {
      world.mutable_store<simulation::Shield>().insert_or_assign(
          entity, simulation::Shield::activate(activation, kSpentShieldDurationTicks, 1,
                                               kSpentCooldownTicks, kParryStunDurationTicks));
    }
    if (opponent.exposure.shield_protecting) {
      world.mutable_store<simulation::Shield>().insert_or_assign(
          entity, simulation::Shield::activate(activation, kProtectingShieldDurationTicks,
                                               kParryStunDurationTicks, kSpentCooldownTicks,
                                               kParryStunDurationTicks));
    }
    if (opponent.exposure.charge_spent) {
      world.mutable_store<simulation::Charge>().insert_or_assign(
          entity, simulation::Charge::activate(activation, kSpentCooldownTicks));
    }
    if (opponent.exposure.outside_zone) {
      world.mutable_store<simulation::ZoneExposure>().insert_or_assign(entity,
                                                                       simulation::ZoneExposure{3});
    }
    if (opponent.exposure.holding_hill) {
      world.mutable_store<simulation::HillPresence>().insert_or_assign(entity,
                                                                       simulation::HillPresence{4});
    }
  }
  if (frame.lethal) {
    const auto hazard = simulation::EntityId::create(kHazardEntity);
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        hazard, combat_body(frame.lethal->x(), frame.lethal->y(), 0.0, 0.0, 10.0));
    world.mutable_store<simulation::LethalOnContact>().insert_or_assign(
        hazard, simulation::LethalOnContact{});
  }
  auto terrain = frame.race ? fixture::race_terrain()
                            : simulation::TerrainDefinition::create(
                                  simulation::ArenaBounds::create(960.0, 640.0),
                                  simulation::TerrainGround::kSolid, {}, frame.holes);
  auto map = simulation::MapDefinition::create("tactical_combat_fixture", std::move(terrain), {},
                                               {}, simulation::MapMetadata::none());
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(std::move(map)));
  for (std::uint64_t tick = 0; tick < frame.steps; ++tick) {
    static_cast<void>(
        game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()));
  }
  return controllers::Observation::create(
      std::make_shared<const simulation::WorldSnapshot>(game.snapshot()),
      simulation::ControllerId::create(fixture::kController));
}

[[nodiscard]] const simulation::PhysicsBody&
self_body(const controllers::Observation& observation) {
  const auto* body = controllers::find_observed_component<simulation::PhysicsBody>(
      observation.snapshot(), *observation.entity());
  REQUIRE(body != nullptr);
  return *body;
}

[[nodiscard]] controllers::TacticalObjectiveCandidates
combat_candidates(const controllers::Observation& observation,
                  const controllers::TacticalObjectivePolicy& value) {
  return controllers::collect_tactical_objective_candidates(observation, self_body(observation),
                                                            value);
}

[[nodiscard]] simulation::TerrainHole hole(const std::string& name, const double x, const double y,
                                           const double radius) {
  return simulation::TerrainHole::create(name, simulation::Vector2::create(x, y), radius);
}

// Every shove candidate's subject, in the order the provider emitted them: the nearest-N filter's
// total order, which is what two toolchains have to agree on and what one lane can still observe.
[[nodiscard]] std::vector<std::uint64_t>
shove_subjects(const controllers::TacticalObjectiveCandidates& result) {
  std::vector<std::uint64_t> subjects;
  for (const auto& value : result.candidates) {
    if (value.key.kind == controllers::TacticalObjectiveKind::kShoveSetup) {
      subjects.push_back(value.key.subject);
    }
  }
  return subjects;
}

// A candidate carrying nothing but the key the three exported combat predicates read, so a case can
// ask them about an opponent without first arranging for the provider to publish one.
[[nodiscard]] controllers::TacticalObjectiveCandidate shove_key(const std::uint64_t subject) {
  return scored(controllers::TacticalObjectiveKind::kShoveSetup, subject, 0.0, false);
}

[[nodiscard]] controllers::TacticalObjectivePolicy combat_policy(const std::uint64_t horizon_ticks,
                                                                 const double charge_screen,
                                                                 const std::uint64_t shield_ticks) {
  auto value = policy(1.0, 0.0, horizon_ticks);
  value.charge_screen_diagonal_fraction = charge_screen;
  value.shield_anticipation_ticks = shield_ticks;
  return value;
}

// The two knobs that read a candidate's opening, moved together and with nothing else moved. Every
// other field keeps the value a default-constructed policy has, which is the value that reproduces
// the behaviour before this step -- including `road_caution_fraction`, the one member of the policy
// that does not default to zero.
[[nodiscard]] controllers::TacticalObjectivePolicy opening_policy(const double preference,
                                                                  const double opening_floor) {
  auto value = policy(1.0, 0.0, 0);
  value.exposure_preference = preference;
  value.minimum_opening = opening_floor;
  return value;
}

// The world every exposure case shares: no hill, one hazard the standing point is derived from, and
// the loaded initial tick. The opponents are the caller's.
[[nodiscard]] CombatFrame exposed_frame(std::vector<CombatOpponent> opponents) {
  CombatFrame frame;
  frame.hills = 0;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  frame.opponents = std::move(opponents);
  return frame;
}

// The opening one lone shove candidate carries under one authored preference, with no floor so that
// nothing is dropped and the quality itself is what is read. This is the one exposure world that is
// stepped, because it is the one with a single body and therefore nothing for the solver to do.
[[nodiscard]] double lone_opening(const OpponentExposure& exposure, const double preference) {
  auto frame = exposed_frame({{kFirstOpponent, 400.0, 320.0, 0.0, 0.0, kOpponentRadius, exposure}});
  frame.steps = kExposureObservedTick;
  const auto result = combat_candidates(combat_observation(frame), opening_policy(preference, 0.0));
  REQUIRE(result.candidates.size() == 1);
  REQUIRE(result.candidates.front().key.kind == controllers::TacticalObjectiveKind::kShoveSetup);
  return result.candidates.front().opening;
}
} // namespace

TEST_CASE("Tactical circle providers preserve public centers radii subjects and ordered ties",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.x = 400.0;
  frame.circles = {{fixture::kSecondObjective, 500.0, 320.0, 0.0},
                   {fixture::kFirstObjective, 300.0, 320.0, 20.0}};
  const auto hill = candidates(frame);
  REQUIRE(hill.candidates.size() == 2);
  const auto best =
      std::ranges::min_element(hill.candidates, controllers::tactical_candidate_precedes);
  CHECK(best->key == controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kHill,
                                                       fixture::kFirstObjective});
  CHECK(best->squared_distance == 10'000.0);
  CHECK(best->target == simulation::Vector2::create(300.0, 320.0));
  CHECK(hill.candidates[1].arrival_radius == 0.0);
  frame.mode = fixture::Mode::kZone;
  const auto zone = candidates(frame);
  REQUIRE(zone.candidates.size() == 2);
  CHECK(zone.candidates.front().key.kind == controllers::TacticalObjectiveKind::kZone);
  CHECK(controllers::tactical_candidate_precedes(hill.candidates.front(), zone.candidates.front()));
  CHECK_FALSE(
      controllers::tactical_candidate_precedes(zone.candidates.front(), hill.candidates.front()));
}

TEST_CASE(
    "Tactical candidate screening rejects unsupported targets and supported targets across void",
    "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.terrain = fixture::terrain_with_hole(600.0, 30.0);
  CHECK(candidates(frame).candidates.empty());
  frame.terrain = fixture::terrain_with_hole(400.0, 30.0);
  CHECK(candidates(frame).candidates.empty());
  frame.circles.front().x = 350.0;
  CHECK(candidates(frame).candidates.size() == 1);
  frame.circles.front().x = 370.0; // Canonical exact supported rim, no void before the endpoint.
  CHECK(candidates(frame).candidates.size() == 1);
}

TEST_CASE("Tactical providers refuse thirty three candidates before terrain filtering and reject "
          "malformed radii",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.circles.clear();
  for (std::size_t index = 0; index < controllers::kMaximumTacticalObjectiveCandidateCount;
       ++index) {
    frame.circles.push_back({fixture::kFirstObjective + index, 600.0, 320.0, 0.0});
  }
  CHECK(candidates(frame).candidates.size() ==
        controllers::kMaximumTacticalObjectiveCandidateCount);
  frame.circles.push_back(
      {fixture::kFirstObjective + controllers::kMaximumTacticalObjectiveCandidateCount, 600.0,
       320.0, 0.0});
  frame.terrain = fixture::terrain_with_hole(600.0, 30.0);
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalCandidateLimitExceeded);
  frame = fixture::Frame{};
  frame.circles.front().radius = -1.0;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.mode = fixture::Mode::kUnsupported;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalModeUnsupported);
}

TEST_CASE("Tactical race provider uses exact road binding progress and strict recovery threshold",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.mode = fixture::Mode::kRace;
  frame.checkpoint = 1;
  auto result = candidates(frame);
  REQUIRE(result.candidates.size() == 1);
  CHECK(result.candidates.front().key ==
        controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kRaceGate, 1});
  CHECK(result.candidates.front().target == simulation::Vector2::create(600.0, 320.0));
  frame.y = 380.0;
  CHECK(candidates(frame).candidates.front().key.kind ==
        controllers::TacticalObjectiveKind::kRaceGate);
  frame.y = std::nextafter(380.0, std::numeric_limits<double>::infinity());
  result = candidates(frame);
  REQUIRE(result.candidates.size() == 1);
  CHECK(result.candidates.front().key.kind == controllers::TacticalObjectiveKind::kRaceRecovery);
  CHECK(result.candidates.front().target == simulation::Vector2::create(frame.x, 320.0));
  CHECK(result.candidates.front().arrival_radius == 0.0);
  frame.checkpoint = 2;
  CHECK(candidates(frame).disposition == controllers::TacticalObjectiveDisposition::kFinished);
  frame.race_progress = false;
  CHECK(candidates(frame).disposition == controllers::TacticalObjectiveDisposition::kWaiting);
}

TEST_CASE("Tactical race provider fails missing bindings invalid progress and invalid gate radius",
          "[unit][controllers][tactical_objectives]") {
  fixture::Frame frame;
  frame.mode = fixture::Mode::kRace;
  frame.course = fixture::race_course();
  frame.course->road = simulation::RaceRoadName::create("missing_lane");
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.course = fixture::race_course();
  frame.checkpoint = 3;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.checkpoint = 0;
  frame.course->checkpoint_radius = 0.0;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.course->checkpoint_radius = 81.0;
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
  frame.course->checkpoints.clear();
  require_rejection(frame, controllers::ControllersValidationCode::kTacticalObjectiveInvalid);
}

TEST_CASE("Tactical utility keeps its written multiply subtract add order and its unit terms",
          "[unit][controllers][tactical_objectives][utility]") {
  const auto tolerant = policy(0.8, 0.25, 0);
  const auto clear = scored(controllers::TacticalObjectiveKind::kHill, 1, 0.5, false);
  const auto blocked = scored(controllers::TacticalObjectiveKind::kHill, 2, 0.5, true);
  CHECK(controllers::tactical_candidate_score(clear, tolerant, false) ==
        ((0.8 * (1.0 - 0.5)) - 0.0) + 0.0);
  CHECK(controllers::tactical_candidate_score(blocked, tolerant, false) ==
        ((0.8 * (1.0 - 0.5)) - (1.0 - 0.25)) + 0.0);
  CHECK(controllers::tactical_candidate_score(clear, tolerant, true) ==
        ((0.8 * (1.0 - 0.5)) - 0.0) + controllers::kTacticalHeldTargetBonus);
  // A weight scales proximity and nothing else, so it is the term that decides a blocked near
  // candidate against a clear far one. This is the differentiation the step exists to make
  // possible, at the level where it is arithmetic rather than behaviour.
  const auto strict = policy(0.8, 0.0, 0);
  CHECK(controllers::tactical_candidate_score(blocked, strict, false) ==
        ((0.8 * (1.0 - 0.5)) - 1.0) + 0.0);
  const auto full_risk = policy(0.8, 1.0, 0);
  CHECK(controllers::tactical_candidate_score(blocked, full_risk, false) ==
        controllers::tactical_candidate_score(clear, full_risk, false));
  // **The opening is the third factor and its default is one, so every score above is the score
  // this rule computed before the opening existed.** That default is not a style choice: the other
  // members of `TacticalObjectiveCandidate` default to zero and false, `scored()` initialises only
  // six of them, and the four mode kinds have no opponent to be exposed -- so an `opening{}` would
  // have multiplied every hill, zone, gate and recovery preference by zero, silently, because the
  // aggregate is well formed with either initializer and no compiler can tell them apart.
  CHECK(clear.opening == 1.0);
  CHECK(blocked.opening == 1.0);
  // The four-member aggregate initialization every provider uses, written out here so the default
  // is proven at the shape that actually builds candidates in production and not only through
  // `scored()`.
  const controllers::TacticalObjectiveCandidate bare{{controllers::TacticalObjectiveKind::kHill, 9},
                                                     simulation::Vector2::create(0.0, 0.0),
                                                     0.0,
                                                     0.0};
  CHECK(bare.opening == 1.0);
  CHECK(bare.objective_velocity_x == 0.0);
  CHECK(bare.objective_velocity_y == 0.0);
  // A half opening halves the preference term and leaves the penalty and the bonus where they are,
  // which is what keeps the product inside the unit interval the penalty already lives in.
  auto narrow = clear;
  narrow.opening = 0.5;
  CHECK(controllers::tactical_candidate_score(narrow, tolerant, false) ==
        ((0.8 * (1.0 - 0.5) * 0.5) - 0.0) + 0.0);
  auto narrow_blocked = blocked;
  narrow_blocked.opening = 0.5;
  CHECK(controllers::tactical_candidate_score(narrow_blocked, tolerant, false) ==
        ((0.8 * (1.0 - 0.5) * 0.5) - (1.0 - 0.25)) + 0.0);
}

TEST_CASE("Tactical selection takes the maximum and breaks exact ties on the stable kind ordinal",
          "[unit][controllers][tactical_objectives][utility]") {
  const auto even = policy(1.0, 1.0, 0);
  std::vector<controllers::TacticalObjectiveCandidate> set{
      scored(controllers::TacticalObjectiveKind::kHill, 7, 0.9, false),
      scored(controllers::TacticalObjectiveKind::kHill, 8, 0.1, false)};
  CHECK(controllers::tactical_select_candidate(set, even, std::nullopt) == 1);
  // **The empty optional is reachable for an empty set and for nothing else**, which is the whole
  // of what the return type now promises. Before it, this line read `== 0` against a
  // `candidates.size()` sentinel that was also a legal index, so it asserted the same thing an
  // out-of-range answer would have; a caller could index with it and read inside the reserved but
  // unfilled tail of the vector without a sanitizer noticing.
  CHECK_FALSE(controllers::tactical_select_candidate({}, even, std::nullopt).has_value());
  CHECK(controllers::tactical_select_candidate(set, even, std::nullopt).has_value());
  // Held targets win a comparison they would otherwise lose, by exactly the bonus and no more.
  const auto held = controllers::TacticalObjectiveKey{controllers::TacticalObjectiveKind::kHill, 7};
  set[0].normalized_distance = 0.2;
  CHECK(controllers::tactical_select_candidate(set, even, held) == 0);
  set[0].normalized_distance = 0.3;
  CHECK(controllers::tactical_select_candidate(set, even, held) == 1);
  // A per-kind weight orders two kinds directly. This is the arithmetic underneath the behavioural
  // proof in `tactical_controller_tests.cpp`, where the opponent-derived provider puts a shove
  // candidate and a mode candidate in one screened set and the shove weight alone decides.
  auto weighted = policy(0.0, 1.0, 0);
  weighted.objective_weights[controllers::tactical_objective_kind_ordinal(
      controllers::TacticalObjectiveKind::kZone)] = 1.0;
  std::vector<controllers::TacticalObjectiveCandidate> kinds{
      scored(controllers::TacticalObjectiveKind::kHill, 1, 0.1, false),
      scored(controllers::TacticalObjectiveKind::kZone, 2, 0.8, false)};
  CHECK(controllers::tactical_select_candidate(kinds, weighted, std::nullopt) == 1);
  // Exactly equal scores fall to the tie-break chain, whose tail is the key, and the answer cannot
  // depend on the order the provider happened to push them in.
  std::vector<controllers::TacticalObjectiveCandidate> tied{
      scored(controllers::TacticalObjectiveKind::kZone, 4, 0.5, false),
      scored(controllers::TacticalObjectiveKind::kHill, 3, 0.5, false)};
  const auto first = tied[*controllers::tactical_select_candidate(tied, even, std::nullopt)].key;
  std::ranges::reverse(tied);
  const auto second = tied[*controllers::tactical_select_candidate(tied, even, std::nullopt)].key;
  CHECK(first == second);
  CHECK(first.kind == controllers::TacticalObjectiveKind::kHill);
}

TEST_CASE("Tactical escape screening ranks down an overshoot into void without deleting it",
          "[unit][controllers][tactical_objectives][terrain]") {
  fixture::Frame frame;
  frame.terrain = fixture::terrain_with_hole(400.0, 30.0);
  frame.circles.front().x = 350.0;
  // The approach itself is supported; the published top speed of 600 world units a second carries
  // this bot 240 units in 160 ticks, which is past the target and into the hole's void.
  const auto blocked = candidates(frame, policy(1.0, 0.0, 160));
  REQUIRE(blocked.candidates.size() == 1);
  CHECK(blocked.candidates.front().escape_blocked);
  CHECK(blocked.work.raw_candidate_count == 1);
  CHECK(blocked.work.screened_candidate_count == 1);
  CHECK(blocked.work.prediction_step_count == 1);
  // The same geometry with a horizon that stops short of the void is not ranked down at all.
  const auto clear = candidates(frame, policy(1.0, 0.0, 100));
  REQUIRE(clear.candidates.size() == 1);
  CHECK_FALSE(clear.candidates.front().escape_blocked);
  CHECK(clear.work.prediction_step_count == 1);
  // A zero horizon casts no ray, and therefore counts no prediction.
  const auto none = candidates(frame, policy(1.0, 0.0, 0));
  REQUIRE(none.candidates.size() == 1);
  CHECK_FALSE(none.candidates.front().escape_blocked);
  CHECK(none.work.prediction_step_count == 0);
  // Screening writes the arena-relative distance the utility rule reads, clamped to the diagonal.
  const double diagonal = std::sqrt((960.0 * 960.0) + (640.0 * 640.0));
  CHECK(none.candidates.front().normalized_distance == 150.0 / diagonal);
}

TEST_CASE("Tactical hill intercept extrapolates the published committed velocity only",
          "[unit][controllers][tactical_objectives][hill]") {
  const auto center = simulation::Vector2::create(600.0, 320.0);
  CHECK(controllers::tactical_predicted_center(center, simulation::Vector2::create(40.0, -80.0),
                                               100) ==
        simulation::Vector2::create(600.0 + (40.0 * 0.25), 320.0 + (-80.0 * 0.25)));
  CHECK(controllers::tactical_predicted_center(center, simulation::Vector2::create(40.0, -80.0),
                                               0) == center);
  // A published velocity that cannot be extrapolated into a representable point is refused rather
  // than thrown on: the host isolates a throw and the bot would stop acting for the pass.
  CHECK(controllers::tactical_predicted_center(
            center,
            simulation::Vector2::create(simulation::kMaximumPhysicalComponentMagnitude, 0.0),
            4'000) == center);
  const auto observation = moving_hills(1, simulation::Vector2::create(40.0, 0.0));
  const auto predicted = moving_hill_candidates(observation, policy(1.0, 1.0, 100));
  REQUIRE(predicted.candidates.size() == 1);
  CHECK(predicted.candidates.front().target == simulation::Vector2::create(610.0, 320.0));
  CHECK(predicted.candidates.front().squared_distance == 410.0 * 410.0);
  CHECK(predicted.work.prediction_step_count == 2);
  CHECK(predicted.candidates.front().objective_velocity_x == 40.0);
  CHECK(predicted.candidates.front().objective_velocity_y == 0.0);
  // A zero horizon holds the published centre, so every stationary-hill behaviour is unchanged.
  const auto held = moving_hill_candidates(observation, policy(1.0, 1.0, 0));
  REQUIRE(held.candidates.size() == 1);
  CHECK(held.candidates.front().target == simulation::Vector2::create(600.0, 320.0));
  CHECK(held.work.prediction_step_count == 0);
  // **The motion is carried whether or not it is predicted over**, because the arrival brake reads
  // it at a zero horizon too and this is the one place the store is already consulted. Carrying it
  // is what keeps every later reader from rebuilding an `EntityId` out of `key.subject`, which is a
  // throw for any subject outside the published range and therefore a permanently inert bot.
  CHECK(held.candidates.front().objective_velocity_x == 40.0);
  CHECK(held.candidates.front().objective_velocity_y == 0.0);
  // Structural zeros everywhere else: a hill that publishes no `HillMotion`, and the two kinds
  // whose objectives are authored terrain that publishes motion of no kind at all.
  fixture::Frame still;
  const auto stationary = candidates(still, policy(1.0, 1.0, 100));
  REQUIRE(stationary.candidates.size() == 1);
  CHECK(stationary.candidates.front().objective_velocity_x == 0.0);
  CHECK(stationary.candidates.front().objective_velocity_y == 0.0);
  still.mode = fixture::Mode::kRace;
  const auto gate = candidates(still, policy(1.0, 1.0, 100));
  REQUIRE(gate.candidates.size() == 1);
  CHECK(gate.candidates.front().key.kind == controllers::TacticalObjectiveKind::kRaceGate);
  CHECK(gate.candidates.front().objective_velocity_x == 0.0);
  CHECK(gate.candidates.front().objective_velocity_y == 0.0);
}

TEST_CASE("Tactical bounded work reaches its derived ceiling and never passes it",
          "[unit][controllers][tactical_objectives][limits]") {
  // The ceiling is derived, not written: one intercept per raw *mode* candidate plus one escape ray
  // per screened candidate over the merged set of two providers. Spelled out, that is 32 + 64 = 96.
  CHECK(controllers::kMaximumTacticalPredictionStepCount ==
        controllers::kMaximumTacticalObjectiveCandidateCount *
            (controllers::kTacticalObjectiveProviderCount + 1));
  const auto observation = moving_hills(controllers::kMaximumTacticalObjectiveCandidateCount,
                                        simulation::Vector2::create(40.0, 0.0));
  const auto mode_only = moving_hill_candidates(observation, policy(1.0, 1.0, 100));
  CHECK(mode_only.work.raw_candidate_count == controllers::kMaximumTacticalObjectiveCandidateCount);
  CHECK(mode_only.work.screened_candidate_count ==
        controllers::kMaximumTacticalObjectiveCandidateCount);
  // A world with no opponent reaches two thirds of the ceiling and no more, which is the whole of
  // what Step 22a could reach: the third of it that is missing is the shove provider's screening.
  CHECK(mode_only.work.prediction_step_count ==
        2 * controllers::kMaximumTacticalObjectiveCandidateCount);
  CHECK(mode_only.work.prediction_step_count <= controllers::kMaximumTacticalPredictionStepCount);

  CombatFrame frame;
  frame.hills = controllers::kMaximumTacticalObjectiveCandidateCount;
  frame.hill_velocity_x = 40.0;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  for (std::size_t index = 0; index < controllers::kMaximumTacticalShoveCandidateCount; ++index) {
    frame.opponents.push_back({kFirstOpponent + index, 400.0, 320.0});
  }
  const auto full = combat_candidates(combat_observation(frame), policy(1.0, 1.0, 100));
  CHECK(full.work.raw_candidate_count == 2 * controllers::kMaximumTacticalObjectiveCandidateCount);
  CHECK(full.work.screened_candidate_count ==
        2 * controllers::kMaximumTacticalObjectiveCandidateCount);
  // Both providers at their own budget, and the shove provider extrapolating nothing: 32 hill
  // intercepts plus one escape ray for each of the 64 screened candidates sits exactly on 96.
  CHECK(full.work.prediction_step_count == controllers::kMaximumTacticalPredictionStepCount);
  CHECK(full.work.prediction_step_count <= controllers::kMaximumTacticalPredictionStepCount);
}

TEST_CASE("Tactical candidate budgets are per provider and never measured on the merged set",
          "[unit][controllers][tactical_objectives][limits]") {
  // Sixty four raw candidates in one pass, which the shared budget Step 15 wrote would have refused
  // at thirty three. Each provider is inside its own thirty two, so a legal world stays legal.
  CombatFrame frame;
  frame.hills = controllers::kMaximumTacticalObjectiveCandidateCount;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  for (std::size_t index = 0; index < controllers::kMaximumTacticalShoveCandidateCount; ++index) {
    frame.opponents.push_back({kFirstOpponent + index, 400.0, 320.0});
  }
  CHECK(combat_candidates(combat_observation(frame), policy(1.0, 1.0, 0)).candidates.size() ==
        2 * controllers::kMaximumTacticalObjectiveCandidateCount);
  // The shove provider cannot exceed its budget even when the world does: the nearest-N filter is
  // set to the same thirty two, so a thirty third opponent is dropped rather than thrown on.
  frame.opponents.push_back(
      {kFirstOpponent + controllers::kMaximumTacticalShoveCandidateCount, 400.0, 320.0});
  CHECK(combat_candidates(combat_observation(frame), policy(1.0, 1.0, 0)).candidates.size() ==
        2 * controllers::kMaximumTacticalObjectiveCandidateCount);
  // The mode provider has no such filter, so its own thirty third still throws, and the merged
  // count it throws at is irrelevant.
  frame.hills = controllers::kMaximumTacticalObjectiveCandidateCount + 1;
  try {
    static_cast<void>(combat_candidates(combat_observation(frame), policy(1.0, 1.0, 0)));
    FAIL("a mode provider past its own budget must fail visibly");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kTacticalCandidateLimitExceeded);
  }
}

TEST_CASE("Tactical shove provider stands the bot on the safe side and never on the opponent",
          "[unit][controllers][tactical_objectives][shove]") {
  // One hill and one opponent, which is the first candidate set in this tree that genuinely holds
  // two kinds: every shipped mode yields at most one candidate on its own.
  CombatFrame frame;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  frame.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const auto result = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0));
  REQUIRE(result.candidates.size() == 2);
  CHECK(result.candidates.front().key.kind == controllers::TacticalObjectiveKind::kHill);
  const auto& shove = result.candidates.back();
  CHECK(shove.key == controllers::TacticalObjectiveKey{
                         controllers::TacticalObjectiveKind::kShoveSetup, kFirstOpponent});
  // S = O + unit(O - Hazard) * (r_self + r_opponent + margin). The hazard is directly below the
  // opponent, so the unit direction is exactly (0,-1) and the whole standoff lands on y.
  const auto standing = simulation::Vector2::create(400.0, 320.0 - kShoveStandoff);
  CHECK(shove.target == standing);
  // Stated as the two things it is not, because a later reader will want to "simplify" it to one of
  // them and both would make `escape_blocked` a constant over every shove candidate.
  CHECK_FALSE(shove.target == simulation::Vector2::create(400.0, 320.0));
  CHECK_FALSE(shove.target == simulation::Vector2::create(400.0, 400.0));
  // Arrival is the margin and not the standoff: a bot one margin from S on the opponent's side is
  // exactly two radii from the opponent, which is contact.
  CHECK(shove.arrival_radius == kShoveMargin);
  CHECK(shove.squared_distance ==
        ((400.0 - 200.0) * (400.0 - 200.0)) +
            ((320.0 - kShoveStandoff - 320.0) * (320.0 - kShoveStandoff - 320.0)));
  CHECK_FALSE(shove.escape_blocked);
  CHECK(result.work.raw_candidate_count == 2);
  CHECK(result.work.screened_candidate_count == 2);
  // No hazard on this map is an absent candidate, not a failure: the bot has nowhere to shove them.
  frame.holes.clear();
  const auto nowhere = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0));
  CHECK(nowhere.candidates.size() == 1);
  CHECK(shove_subjects(nowhere).empty());
  // A published hill the opponent is standing in is the one badness that runs outward, so the safe
  // side is the centre's and no terrain hole is needed to produce a candidate at all.
  frame.opponents = {{kFirstOpponent, 610.0, 100.0}};
  const auto inside = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0));
  REQUIRE(inside.candidates.size() == 2);
  CHECK(inside.candidates.back().target ==
        simulation::Vector2::create(610.0 - kShoveStandoff, 100.0));
}

TEST_CASE("Tactical shove candidates are screened and ranked down on the same two terrain calls",
          "[unit][controllers][tactical_objectives][shove][terrain]") {
  // The hazard here is a published `LethalOnContact` body rather than a hole, which keeps the two
  // holes below free to be pure terrain: both sit farther from the opponent than it does, so
  // neither can take over as the badness the standing point is derived from.
  CombatFrame frame;
  frame.lethal = simulation::Vector2::create(400.0, 380.0);
  frame.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const double leg = std::sqrt((200.0 * 200.0) + (kShoveStandoff * kShoveStandoff));
  const auto ray_hole = [&leg](const std::string& name, const double along, const double radius) {
    return hole(name, 200.0 + ((200.0 / leg) * along), 320.0 - ((kShoveStandoff / leg) * along),
                radius);
  };
  // Void that begins 260 units out, on the `B -> S` line and 48 units past S itself.
  frame.holes = {ray_hole("overshoot_pit", 280.0, 20.0)};
  const auto observation = combat_observation(frame);
  // **`escape_blocked` keeps its shipped meaning on a shove candidate**, which is the whole reason
  // the target is S: it is computed from where the approach *overshoots to* and is not true by
  // construction. A 300-unit horizon reaches the void past S and ranks the candidate down without
  // deleting it; a 150-unit one stops short of S and finds nothing at all.
  const auto reaching = combat_candidates(observation, policy(1.0, 0.0, 200));
  REQUIRE(reaching.candidates.size() == 2);
  CHECK(reaching.candidates.back().key.kind == controllers::TacticalObjectiveKind::kShoveSetup);
  CHECK(reaching.candidates.back().escape_blocked);
  const auto short_of_it = combat_candidates(observation, policy(1.0, 0.0, 100));
  REQUIRE(short_of_it.candidates.size() == 2);
  CHECK_FALSE(short_of_it.candidates.back().escape_blocked);
  // Void *between* the bot and S is a different answer: the approach crosses it, so the candidate
  // is deleted by the shared screen rather than ranked down -- a shove across a hole, refused with
  // no pathfinder. The hill candidate beside it is untouched, so this is the screen and not a
  // throw.
  frame.holes = {ray_hole("crossing_pit", 100.0, 10.0)};
  const auto crossed = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 200));
  REQUIRE(crossed.candidates.size() == 1);
  CHECK(crossed.candidates.front().key.kind == controllers::TacticalObjectiveKind::kHill);
  CHECK(crossed.work.raw_candidate_count == 2);
  CHECK(crossed.work.screened_candidate_count == 1);
}

TEST_CASE("Tactical nearest opponent filter keeps thirty two under one stated total order",
          "[unit][controllers][tactical_objectives][shove][limits]") {
  // Thirty four opponents for thirty two slots. One is strictly nearer; the other thirty three
  // share one position and therefore one exact squared distance, so the entity id is the only thing
  // left that can order them -- which is precisely what `std::nth_element` and `std::partial_sort`
  // do not promise and what two toolchains have to agree on bit for bit.
  CombatFrame frame;
  frame.hills = 0;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  frame.opponents = {{kNearOpponent, 300.0, 320.0}};
  for (std::size_t index = 0; index <= controllers::kMaximumTacticalShoveCandidateCount; ++index) {
    frame.opponents.push_back({kFirstOpponent + index, 400.0, 320.0});
  }
  REQUIRE(frame.opponents.size() == controllers::kMaximumTacticalShoveCandidateCount + 2);
  const auto subjects =
      shove_subjects(combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0)));
  std::vector<std::uint64_t> expected{kNearOpponent};
  for (std::size_t index = 0; index + 1 < controllers::kMaximumTacticalShoveCandidateCount;
       ++index) {
    expected.push_back(kFirstOpponent + index);
  }
  REQUIRE(expected.size() == controllers::kMaximumTacticalShoveCandidateCount);
  // The whole order, not just the membership: distance first, then the stable id, and the two
  // opponents that lost their slot are the two highest ids at the far distance.
  CHECK(subjects == expected);
  CHECK(std::ranges::find(subjects,
                          kFirstOpponent + controllers::kMaximumTacticalShoveCandidateCount - 1) ==
        subjects.end());
  CHECK(std::ranges::find(subjects,
                          kFirstOpponent + controllers::kMaximumTacticalShoveCandidateCount) ==
        subjects.end());
}

TEST_CASE("Tactical shove candidates merge only when the mode provider is ready",
          "[unit][controllers][tactical_objectives][shove][race]") {
  // The mode provider owns the disposition, and this is what that decides: a bot with no
  // `RaceProgress` yet -- the state immediately after spawn -- and a racer that has finished both
  // publish an opponent they cannot shove. The hazard here is the road edge, which is the one
  // badness source that exists without a hole or a lethal body.
  CombatFrame frame;
  frame.race = true;
  frame.opponents = {{kFirstOpponent, 400.0, 360.0}};
  const auto ready = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0));
  CHECK(ready.disposition == controllers::TacticalObjectiveDisposition::kReady);
  REQUIRE(ready.candidates.size() == 2);
  CHECK(ready.candidates.front().key.kind == controllers::TacticalObjectiveKind::kRaceGate);
  CHECK(ready.candidates.back().target ==
        simulation::Vector2::create(400.0, 360.0 - kShoveStandoff));
  frame.race_progress = false;
  const auto waiting = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0));
  CHECK(waiting.disposition == controllers::TacticalObjectiveDisposition::kWaiting);
  CHECK(waiting.candidates.empty());
  frame.race_progress = true;
  frame.checkpoint = 2;
  const auto finished = combat_candidates(combat_observation(frame), policy(1.0, 0.0, 0));
  CHECK(finished.disposition == controllers::TacticalObjectiveDisposition::kFinished);
  CHECK(finished.candidates.empty());
}

TEST_CASE("Tactical charge screen compares the exit time to the contact time and never presence",
          "[unit][controllers][tactical_objectives][charge]") {
  // A shove charge points at the hazard by construction, so `first_support_exit` has a value on
  // every interesting pass and a `.has_value()` screen would veto every one of them. The two halves
  // below differ in nothing but where one hole sits.
  CombatFrame frame;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0), hole("screen_pit", 560.0, 320.0, 12.0)};
  frame.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const auto beyond = combat_observation(frame);
  const auto candidate = shove_key(kFirstOpponent);
  const auto screen = combat_policy(0, 0.5, 0);
  // Ground ends 348 units out along a 576-unit screen; the opponent's near surface is 140 out. The
  // burst arrives before the ground runs out, so the charge is admitted.
  CHECK(controllers::tactical_charge_screen_admits(beyond, self_body(beyond), candidate, screen));
  // The same hole, moved strictly inside the corridor the burst crosses. Nothing else changes.
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0), hole("screen_pit", 300.0, 320.0, 12.0)};
  const auto inside = combat_observation(frame);
  CHECK_FALSE(
      controllers::tactical_charge_screen_admits(inside, self_body(inside), candidate, screen));
  // A screen of no length examined nothing, and a refusal on no evidence is not a screen.
  CHECK(controllers::tactical_charge_screen_admits(inside, self_body(inside), candidate,
                                                   combat_policy(0, 0.0, 0)));
  // Nothing to admit rather than something that failed: another kind, and a subject that has left
  // the snapshot after an elimination.
  CHECK_FALSE(controllers::tactical_charge_screen_admits(
      beyond, self_body(beyond),
      scored(controllers::TacticalObjectiveKind::kHill, kFirstOpponent, 0.0, false), screen));
  CHECK_FALSE(controllers::tactical_charge_screen_admits(beyond, self_body(beyond),
                                                         shove_key(kNearOpponent), screen));
  // A bot standing in void falls out of the same comparison rather than needing its own branch: an
  // initially unsupported start exits at zero, which is not greater than any non-negative gap.
  frame.self_x = 300.0;
  const auto in_void = combat_observation(frame);
  CHECK_FALSE(
      controllers::tactical_charge_screen_admits(in_void, self_body(in_void), candidate, screen));
}

TEST_CASE("Tactical charge alignment measures committed velocity across the commanded ray",
          "[unit][controllers][tactical_objectives][charge]") {
  // The burst is additive, so what has to be small is the component of the *committed* velocity
  // perpendicular to the ray; speed along the ray is not a misalignment at any magnitude.
  const double ceiling = simulation::kDefaultNormalTopSpeed;
  const double allowed = ceiling * controllers::kTacticalChargeAlignmentPerpendicularFraction;
  CombatFrame frame;
  frame.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const auto candidate = shove_key(kFirstOpponent);
  const auto admits = [&frame, &candidate](const double velocity_x, const double velocity_y) {
    auto moving = frame;
    moving.self_velocity_x = velocity_x;
    moving.self_velocity_y = velocity_y;
    const auto observation = combat_observation(moving);
    return controllers::tactical_charge_alignment_admits(observation, self_body(observation),
                                                         candidate);
  };
  CHECK(admits(0.0, 0.0));
  CHECK(admits(ceiling, 0.0));
  CHECK(admits(0.0, allowed));
  CHECK_FALSE(admits(0.0, std::nextafter(allowed, std::numeric_limits<double>::infinity())));
  // The sign of the crossing does not change the answer: it is a magnitude against a fraction.
  CHECK_FALSE(admits(0.0, -ceiling));
  const auto observation = combat_observation(frame);
  CHECK_FALSE(controllers::tactical_charge_alignment_admits(
      observation, self_body(observation),
      scored(controllers::TacticalObjectiveKind::kHill, kFirstOpponent, 0.0, false)));
}

TEST_CASE("Tactical shield closing test extrapolates published motion inside the authored window",
          "[unit][controllers][tactical_objectives][shield]") {
  // One evaluation at the end of the window, never a swept root, and no drag term -- so what this
  // case pins is the arithmetic the header says it is: position plus velocity times the window,
  // against the two published radii.
  CombatFrame frame;
  frame.hills = 0;
  frame.opponents = {{kFirstOpponent, 300.0, 320.0, -500.0, 0.0}};
  const auto closing = combat_observation(frame);
  // A hundred units apart now and sixty units of contact, so the present separation is not contact.
  // Forty ticks carries the opponent fifty units in and the window closes; eight ticks carries it
  // ten and the window does not.
  CHECK(controllers::tactical_opponent_closes_to_contact(closing, self_body(closing),
                                                         combat_policy(0, 0.5, 40)));
  CHECK_FALSE(controllers::tactical_opponent_closes_to_contact(closing, self_body(closing),
                                                               combat_policy(0, 0.5, 8)));
  // A zero window anticipates nothing and answers before extrapolating anything at all.
  CHECK_FALSE(controllers::tactical_opponent_closes_to_contact(closing, self_body(closing),
                                                               combat_policy(0, 0.5, 0)));
  // The same geometry with the velocity reversed, and the same geometry at rest: neither closes.
  frame.opponents = {{kFirstOpponent, 300.0, 320.0, 500.0, 0.0}};
  const auto receding = combat_observation(frame);
  CHECK_FALSE(controllers::tactical_opponent_closes_to_contact(receding, self_body(receding),
                                                               combat_policy(0, 0.5, 40)));
  frame.opponents = {{kFirstOpponent, 300.0, 320.0}};
  const auto still = combat_observation(frame);
  CHECK_FALSE(controllers::tactical_opponent_closes_to_contact(still, self_body(still),
                                                               combat_policy(0, 0.5, 40)));
  // Contact is the two published radii, so two bodies that declare none never close by this test,
  // however far into the window they are carried.
  frame.self_radius = simulation::PhysicsBody::kUndeclaredRadius;
  frame.opponents = {
      {kFirstOpponent, 300.0, 320.0, -500.0, 0.0, simulation::PhysicsBody::kUndeclaredRadius}};
  const auto sizeless = combat_observation(frame);
  CHECK_FALSE(controllers::tactical_opponent_closes_to_contact(sizeless, self_body(sizeless),
                                                               combat_policy(0, 0.5, 40)));
}

TEST_CASE("Tactical shove opponent lookup is the one owner of which body a candidate is about",
          "[unit][controllers][tactical_objectives][shove]") {
  CombatFrame frame;
  frame.hills = 0;
  frame.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const auto observation = combat_observation(frame);
  const auto* found =
      controllers::tactical_shove_opponent_body(observation.snapshot(), shove_key(kFirstOpponent));
  REQUIRE(found != nullptr);
  CHECK(found->position() == simulation::Vector2::create(400.0, 320.0));
  // A candidate of any other kind, and a subject that is no longer published, are both nullptr --
  // an elimination and a wrong question answered the same way, because both mean "no body to aim
  // at" rather than "something failed".
  CHECK(controllers::tactical_shove_opponent_body(observation.snapshot(),
                                                  scored(controllers::TacticalObjectiveKind::kHill,
                                                         kFirstOpponent, 0.0, false)) == nullptr);
  CHECK(controllers::tactical_shove_opponent_body(observation.snapshot(),
                                                  shove_key(kNearOpponent)) == nullptr);
}

TEST_CASE("Tactical exposure is a fixed-order weighted sum of five published booleans",
          "[unit][controllers][tactical_objectives][shove][exposure]") {
  // **Every term is a boolean and nothing here divides by anything.** The natural ratio spellings
  // -- exposure ticks over the elimination grace, presence ticks over the point interval, cooldown
  // remaining over an authored cooldown -- each divide by a denominator this codebase documents as
  // legally zero, and two of those denominators live in `mode_state`, which this provider is
  // documented never to read. Reading the wrong variant arm throws `std::bad_variant_access`,
  // `ControllerHost` catches it, and `TacticalController` assigns its state only after
  // `decide_next` returns -- so the bot would repeat the throwing pass forever rather than make one
  // bad decision. The five components below need no denominator at all.
  CHECK(lone_opening({.stunned = true}, 1.0) == controllers::kTacticalExposureStunWeight);
  CHECK(lone_opening({.shield_spent = true}, 1.0) ==
        controllers::kTacticalExposureShieldSpentWeight);
  CHECK(lone_opening({.charge_spent = true}, 1.0) ==
        controllers::kTacticalExposureChargeSpentWeight);
  CHECK(lone_opening({.outside_zone = true}, 1.0) ==
        controllers::kTacticalExposureOutsideZoneWeight);
  CHECK(lone_opening({.holding_hill = true}, 1.0) == controllers::kTacticalExposureHillHoldWeight);
  // **Spent, not merely present.** A shield's cooldown starts at its activation, so bare presence
  // would score a body that is protected *right now* as the most exposed thing on the map. The same
  // component, inside its protection window rather than past it, contributes nothing.
  CHECK(lone_opening({.shield_protecting = true}, 1.0) == 0.0);
  // Nothing published is no exposure, and everything published is exactly the top of the interval:
  // the five weights are negative powers of two, so the sum is exact under any association.
  CHECK(lone_opening({}, 1.0) == 0.0);
  CHECK(lone_opening({.stunned = true,
                      .shield_spent = true,
                      .charge_spent = true,
                      .outside_zone = true,
                      .holding_hill = true},
                     1.0) == 1.0);
  // **A zero preference leaves every opening at exactly one**, which is the value every candidate
  // of every other kind carries and the value that reproduces the score before exposure existed.
  // It is asserted against both ends of the quality, because an implementation that multiplied
  // rather than interpolated would agree with this on the exposed opponent and disagree on the
  // bare one.
  CHECK(lone_opening({}, 0.0) == 1.0);
  CHECK(lone_opening({.stunned = true,
                      .shield_spent = true,
                      .charge_spent = true,
                      .outside_zone = true,
                      .holding_hill = true},
                     0.0) == 1.0);
  // Between the ends it interpolates, in the written order: `exposure * preference + (1 -
  // preference)`, which is one at a zero preference and the quality itself at a full one.
  CHECK(lone_opening({.stunned = true}, 0.5) ==
        (controllers::kTacticalExposureStunWeight * 0.5) + 0.5);
  CHECK(lone_opening({}, 0.5) == 0.5);
  // **A mode candidate is untouched by any of it**, because the four mode kinds have no opponent to
  // be exposed and the collector could not have answered the question for them. This is the
  // assertion that would fail if `opening` ever defaulted to zero.
  CombatFrame moded;
  moded.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  moded.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const auto mixed = combat_candidates(combat_observation(moded), opening_policy(1.0, 0.0));
  REQUIRE(mixed.candidates.size() == 2);
  CHECK(mixed.candidates.front().key.kind == controllers::TacticalObjectiveKind::kHill);
  CHECK(mixed.candidates.front().opening == 1.0);
}

TEST_CASE("Tactical exposure reorders two equidistant shove candidates and only exposure does",
          "[unit][controllers][tactical_objectives][shove][exposure]") {
  // Two opponents at one published position, which is the only way to make two shove candidates
  // *exactly* equidistant: both standing points come out of the same hazard through the same
  // arithmetic, so every screened field but the subject and the opening is bit for bit identical
  // and the opening is the only thing left that can decide.
  const auto frame = exposed_frame(
      {{kFirstOpponent, 400.0, 320.0},
       {kSecondOpponent, 400.0, 320.0, 0.0, 0.0, kOpponentRadius, {.stunned = true}}});
  const auto observation = combat_observation(frame);
  const auto indifferent = combat_candidates(observation, opening_policy(0.0, 0.0));
  REQUIRE(indifferent.candidates.size() == 2);
  CHECK(indifferent.candidates[0].squared_distance == indifferent.candidates[1].squared_distance);
  CHECK(indifferent.candidates[0].target == indifferent.candidates[1].target);
  CHECK(indifferent.candidates[0].opening == 1.0);
  CHECK(indifferent.candidates[1].opening == 1.0);
  const auto tied = controllers::tactical_select_candidate(indifferent.candidates,
                                                           opening_policy(0.0, 0.0), std::nullopt);
  REQUIRE(tied.has_value());
  CHECK(indifferent.candidates[*tied].key.subject == kFirstOpponent);

  // The same two candidates under a profile that scores exposure. Nothing about the world moved.
  const auto preferring = combat_candidates(observation, opening_policy(1.0, 0.0));
  REQUIRE(preferring.candidates.size() == 2);
  CHECK(preferring.candidates[0].opening == 0.0);
  CHECK(preferring.candidates[1].opening == controllers::kTacticalExposureStunWeight);
  const auto exposed = controllers::tactical_select_candidate(
      preferring.candidates, opening_policy(1.0, 0.0), std::nullopt);
  REQUIRE(exposed.has_value());
  CHECK(preferring.candidates[*exposed].key.subject == kSecondOpponent);
  // **This is the reordering the multiplication exists for**, and it is only visible with the kind
  // held constant: with one kind in the set the weight is a common factor and reorders nothing, so
  // exposure had to enter a factor the kind cannot supply.
  CHECK(preferring.candidates[0].key.kind == preferring.candidates[1].key.kind);
}

TEST_CASE("Tactical minimum opening drops a low-value fight rather than yielding an unselectable "
          "candidate",
          "[unit][controllers][tactical_objectives][shove][exposure]") {
  const auto frame = exposed_frame(
      {{kFirstOpponent, 400.0, 320.0},
       {kSecondOpponent, 400.0, 320.0, 0.0, 0.0, kOpponentRadius, {.stunned = true}}});
  const auto observation = combat_observation(frame);
  // A zero floor admits every fight, which is what a profile that fights whoever is nearest
  // authors and what every case written before this step effectively had.
  CHECK(combat_candidates(observation, opening_policy(1.0, 0.0)).candidates.size() == 2);
  // **The drop happens in the provider and not in selection**, so the bare opponent produces no
  // candidate at all rather than one nothing can prefer: the raw count falls with the screened one.
  const auto floored = combat_candidates(observation, opening_policy(1.0, 0.25));
  REQUIRE(floored.candidates.size() == 1);
  CHECK(floored.candidates.front().key.subject == kSecondOpponent);
  CHECK(floored.work.raw_candidate_count == 1);
  CHECK(floored.work.screened_candidate_count == 1);
  // The floor is a floor and not a strict threshold: an opening exactly equal to it is taken.
  const double stun = controllers::kTacticalExposureStunWeight;
  CHECK(combat_candidates(observation, opening_policy(1.0, stun)).candidates.size() == 1);
  CHECK(combat_candidates(
            observation,
            opening_policy(1.0, std::nextafter(stun, std::numeric_limits<double>::infinity())))
            .candidates.empty());
  // Both fights refused is an empty set and not a failure, and the selector answers it with the
  // empty optional rather than with an index the caller could use.
  const auto refused = combat_candidates(observation, opening_policy(1.0, 1.0));
  CHECK(refused.candidates.empty());
  CHECK(refused.work.raw_candidate_count == 0);
  CHECK(refused.disposition == controllers::TacticalObjectiveDisposition::kReady);
  CHECK_FALSE(controllers::tactical_select_candidate(refused.candidates, opening_policy(1.0, 1.0),
                                                     std::nullopt)
                  .has_value());
  // A profile that scores no exposure carries an opening of one on every candidate, so the same
  // full floor refuses nothing at all -- the floor reads the profile's own opening and not the
  // quality behind it.
  CHECK(combat_candidates(observation, opening_policy(0.0, 1.0)).candidates.size() == 2);
}

TEST_CASE("Tactical a zero shove weight skips the opponent provider outright",
          "[unit][controllers][tactical_objectives][shove][utility]") {
  // **A provider-level skip and not a selection-level veto**, which is the difference that makes it
  // safe: the profile produces no shove candidate rather than an unselectable one, so there is no
  // all-vetoed case to answer, no new reason code, and no change to what a zero weight means for
  // the four mode kinds.
  CombatFrame frame;
  frame.holes = {hole("standoff_pit", 400.0, 400.0, 30.0)};
  frame.opponents = {{kFirstOpponent, 400.0, 320.0}};
  const auto observation = combat_observation(frame);
  const auto shove_ordinal =
      controllers::tactical_objective_kind_ordinal(controllers::TacticalObjectiveKind::kShoveSetup);
  auto faint = policy(1.0, 0.0, 0);
  faint.objective_weights[shove_ordinal] = std::numeric_limits<double>::denorm_min();
  const auto kept = combat_candidates(observation, faint);
  CHECK(kept.candidates.size() == 2);
  CHECK(kept.work.raw_candidate_count == 2);
  // The smallest positive weight still runs the provider: the skip is authored zero and nothing
  // else, exactly as the degenerate-weight rejection one file over rejects an omission rather than
  // a faint preference.
  auto skipping = policy(1.0, 0.0, 0);
  skipping.objective_weights[shove_ordinal] = 0.0;
  const auto skipped = combat_candidates(observation, skipping);
  REQUIRE(skipped.candidates.size() == 1);
  CHECK(skipped.candidates.front().key.kind == controllers::TacticalObjectiveKind::kHill);
  CHECK(skipped.work.raw_candidate_count == 1);
  CHECK(skipped.work.screened_candidate_count == 1);
  const auto chosen =
      controllers::tactical_select_candidate(skipped.candidates, skipping, std::nullopt);
  REQUIRE(chosen.has_value());
  CHECK(skipped.candidates[*chosen].key.kind == controllers::TacticalObjectiveKind::kHill);
  // A negative zero is an authored zero and not a distinct weight, which is the reading the
  // degenerate-set rule already takes.
  skipping.objective_weights[shove_ordinal] = -0.0;
  CHECK(combat_candidates(observation, skipping).candidates.size() == 1);
}

TEST_CASE("Tactical race recovery reads the profile caution and its floor is the gate radius over "
          "the road half width",
          "[unit][controllers][tactical_objectives][race]") {
  // **The floor is not a written number, it is a ratio of two published ones**: the recovery
  // threshold is `fraction * half_width`, and any fraction below `gate_radius / half_width` puts
  // that threshold inside the gate -- so a bot standing on a gate, off the centreline but within
  // the gate's own radius, is pulled off the objective it has already reached. The shipped
  // `cautious_racer` authors 0.6 against the 0.5714 this ratio comes to on `circuit-960x640`; this
  // fixture's road is 80 wide and its gates are 20, so the same floor here is exactly a quarter.
  fixture::Frame frame;
  frame.mode = fixture::Mode::kRace;
  frame.checkpoint = 1;
  frame.x = 600.0;
  frame.y = 340.0;
  const double gate_radius = fixture::race_course().checkpoint_radius;
  const double half_width = 80.0; // `race_terrain()`'s `selected_lane`.
  const double caution_floor = gate_radius / half_width;
  auto standing = policy(1.0, 0.0, 0);
  standing.road_caution_fraction = caution_floor;
  const auto on_the_gate = candidates(frame, standing);
  REQUIRE(on_the_gate.candidates.size() == 1);
  CHECK(on_the_gate.candidates.front().key.kind == controllers::TacticalObjectiveKind::kRaceGate);
  CHECK(on_the_gate.candidates.front().arrival_radius == gate_radius);
  // Arrived, which is the whole claim: the bot is standing on the gate and has nowhere to go.
  CHECK(on_the_gate.candidates.front().squared_distance == gate_radius * gate_radius);
  // One representable step below the floor inverts it. Nothing else about the world moved.
  auto below = standing;
  below.road_caution_fraction = std::nextafter(caution_floor, 0.0);
  const auto pulled_off = candidates(frame, below);
  REQUIRE(pulled_off.candidates.size() == 1);
  CHECK(pulled_off.candidates.front().key.kind ==
        controllers::TacticalObjectiveKind::kRaceRecovery);
  CHECK(pulled_off.candidates.front().arrival_radius == 0.0);
  // A policy nobody filled in races exactly as it did before this key existed, which is why this is
  // the one member of `TacticalObjectivePolicy` that does not default to zero.
  CHECK(controllers::TacticalObjectivePolicy{}.road_caution_fraction ==
        controllers::kDefaultRacerCautionFraction);
}
