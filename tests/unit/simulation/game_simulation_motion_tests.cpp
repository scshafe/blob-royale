#include "game_simulation.hpp"

#include "commands/set_movement_tuning_command.hpp"
#include "components/contact_effect_admission_component.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "components/stun_component.hpp"
#include "contact_effect_admission.hpp"
#include "events/contact_event.hpp"
#include "events/despawn_event.hpp"
#include "idle_match_objective.hpp"
#include "idle_spawn_policy.hpp"
#include "motion_triggers.hpp"
#include "random_stream_registry.hpp"
#include "simulation_system.hpp"
#include "simulation_tolerance.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

using Subject = simulation::ContactRule::Subject;
using StagedSystem = simulation::SystemPipeline::StagedSystem;

simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }
simulation::EntityId entity(std::uint64_t id) { return simulation::EntityId::create(id); }
simulation::SimulationConfig config() {
  return simulation::SimulationConfig::create(100, 100, 1, 400, 10, 10, 0);
}
simulation::MapDefinition arena() {
  return simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(100, 100));
}
simulation::GameWorld::EntitySeed moving(std::uint64_t id, double x, double y, double vx = 0,
                                         double vy = 0) {
  return simulation::GameWorld::EntitySeed::create(
      entity(id),
      simulation::PhysicsBody::create(point(x, y), point(vx, vy), point(0, 0)).with_radius(1));
}
const simulation::PhysicsBody& body_of(const simulation::WorldSnapshot& snapshot,
                                       std::uint64_t id) {
  for (const auto& body : snapshot.components<simulation::PhysicsBody>()) {
    if (body.entity == entity(id)) {
      return body.value;
    }
  }
  FAIL("expected physical body was absent");
  throw simulation::SimulationValidationError(
      simulation::SimulationValidationCode::kContinuousMotionInvalidInput, "test.motion_body",
      "expected physical body was absent");
}

struct Observation final {
  std::optional<simulation::GameWorld> world;
  std::optional<simulation::SpatialGrid> grid;
};

class ObserveWorld final : public simulation::SimulationSystem {
public:
  explicit ObserveWorld(std::shared_ptr<Observation> observation,
                        std::string_view name = "observe_motion_world")
      : observation_(std::move(observation)), name_(name) {}
  std::string_view name() const noexcept override { return name_; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override {
    observation_->world = world;
    observation_->grid = context.spatial_index();
  }

private:
  std::shared_ptr<Observation> observation_;
  std::string_view name_;
};

simulation::GameSimulation
game(simulation::GameWorld world,
     simulation::ContactRuleTable contacts = simulation::ContactRuleTable::built_in(),
     simulation::MotionTriggerTable triggers = simulation::MotionTriggerTable::empty(),
     simulation::MotionLimits limits = {}, std::vector<StagedSystem> systems = {},
     simulation::MapDefinition map = arena()) {
  return simulation::GameSimulation::create(
      config(), std::move(world),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(std::move(map))
          .with_contact_rules(std::move(contacts))
          .with_motion_triggers(std::move(triggers))
          .with_motion_limits(limits)
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
}

class TimedPolicy final : public simulation::MotionTriggerPolicy {
public:
  TimedPolicy(std::uint64_t id, double time, bool support = false)
      : entity_(entity(id)), time_(simulation::MotionTime::create(time)), support_(support) {}
  std::optional<std::uint64_t> bind(const simulation::GameWorld&, simulation::EntityId id,
                                    const simulation::TickContext&) const override {
    return id == entity_ ? std::optional<std::uint64_t>{0} : std::nullopt;
  }
  std::optional<simulation::MotionTriggerProposal>
  query(const simulation::GameWorld&, const Subject&, const simulation::MotionTriggerWindow& window,
        const simulation::TickContext& context, std::uint64_t cursor,
        simulation::MotionQueryBudget& budget) const override {
    if (cursor != 0) {
      return std::nullopt;
    }
    if (support_) {
      return simulation::support_loss_motion_trigger(context.map().terrain(), window, budget);
    }
    return simulation::MotionTriggerProposal{time_, simulation::MotionEventPriority::kCheckpoint};
  }
  simulation::MotionTriggerResponse<simulation::WorldEvent>
  respond(const simulation::GameWorld&, const Subject& subject,
          const simulation::MotionTriggerEvent& event,
          const simulation::TickContext&) const override {
    return {{subject.body, simulation::MotionDisposition::kTerminate},
            event.cursor + 1,
            {simulation::DespawnEvent{entity(900)}}};
  }

private:
  simulation::EntityId entity_;
  simulation::MotionTime time_;
  bool support_;
};

simulation::MotionTriggerTable timed_trigger(std::uint64_t id, double time, bool support = false) {
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 1, std::make_unique<const TimedPolicy>(id, time, support)});
  return simulation::MotionTriggerTable::create(std::move(declarations));
}

class PrepareFrozenWorld final : public simulation::SimulationSystem {
public:
  std::string_view name() const noexcept override { return "prepare_frozen_motion_world"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    world.mutable_store<simulation::Score>().insert_or_assign(entity(1), {42});
    auto* first = world.mutable_store<simulation::PhysicsBody>().mutable_find(entity(1));
    REQUIRE(first != nullptr);
    *first = first->with_acceleration(point(400'000, 0));
    world.emit(simulation::DespawnEvent{entity(901)});
  }
};

class GrowRadius final : public simulation::SimulationSystem {
public:
  std::string_view name() const noexcept override { return "grow_motion_radius"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override {
    if (context.tick_sequence().value() == 1) {
      auto* body = world.mutable_store<simulation::PhysicsBody>().mutable_find(entity(1));
      REQUIRE(body != nullptr);
      *body = body->with_radius(10);
    }
  }
};

simulation::ContactResponse frozen_response(const simulation::GameWorld& world,
                                            const Subject& first, const Subject& second,
                                            const simulation::PairContactObservation& observation,
                                            const simulation::TickContext& context) {
  REQUIRE(world.store<simulation::Score>().find(entity(1)) != nullptr);
  CHECK(world.store<simulation::Score>().find(entity(1))->points == 42);
  CHECK(world.events().size() == 1); // Neither this nor an earlier pair has published its effects.
  CHECK(world.store<simulation::PhysicsBody>().find(first.entity)->velocity() == point(0, 0));
  CHECK(first.body.velocity() == point(1'000, 0));
  return simulation::elastic_disc_response(world, first, second, observation, context);
}

bool entity_two(const simulation::GameWorld&, simulation::EntityId id) { return id == entity(2); }
bool entity_one(const simulation::GameWorld&, simulation::EntityId id) { return id == entity(1); }
simulation::ContactResponse terminate_swapped(const simulation::GameWorld&, const Subject& first,
                                              const Subject& second,
                                              const simulation::PairContactObservation& observation,
                                              const simulation::TickContext&) {
  REQUIRE(first.entity == entity(2));
  REQUIRE(second.entity == entity(1));
  CHECK(observation.touch.normal() == point(-1, 0));
  CHECK_FALSE(observation.impact.has_value());
  CHECK_FALSE(observation.first_effect_eligible);
  CHECK(observation.second_effect_eligible);
  return simulation::ContactResponse::create(
      {first.body, simulation::MotionDisposition::kTerminate}, {second.body},
      {simulation::contact_event_of(first, second, observation.touch,
                                    simulation::ContactRuleName::create("swapped_termination"))});
}

struct ModeReads final {
  std::array<unsigned, 8> calls{};
  bool destroyed = false;
};

class CountingMode final : public simulation::GameMode {
public:
  explicit CountingMode(std::shared_ptr<ModeReads> reads) : reads_(std::move(reads)) {}
  ~CountingMode() override { reads_->destroyed = true; }
  std::string_view name() const noexcept override {
    ++reads_->calls[0];
    return "counting_motion";
  }
  simulation::SystemPipeline systems() const override {
    ++reads_->calls[1];
    return simulation::SystemPipeline::empty();
  }
  simulation::ContactRuleTable contact_rules() const override {
    ++reads_->calls[2];
    return simulation::ContactRuleTable::built_in();
  }
  simulation::MotionTriggerTable motion_triggers() const override {
    ++reads_->calls[3];
    return timed_trigger(1, 0.5);
  }
  simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    ++reads_->calls[4];
    return simulation::CommandKindMask::all();
  }
  std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    ++reads_->calls[5];
    return std::make_unique<const simulation::IdleSpawnPolicy>();
  }
  std::unique_ptr<const simulation::MatchObjective> objective() const override {
    ++reads_->calls[6];
    return std::make_unique<const simulation::IdleMatchObjective>();
  }
  void validate_map(const simulation::MapDefinition&) const override { ++reads_->calls[7]; }

private:
  std::shared_ptr<ModeReads> reads_;
};

enum class Fault : std::int64_t {
  kNone,
  kPairThrow,
  kPairTeleport,
  kPairMass,
  kPairDisposition,
  kTriggerThrow,
  kTriggerTeleport,
  kTriggerDisposition,
  kTriggerNoProgress,
  kWorldEventOverflow,
  kLateThrow,
  kBodylessPolicy,
  kDefaultPolicy,
  kUnknownPolicy,
  kLateBodylessPolicy,
  kLateDefaultPolicy,
  kLateUnknownPolicy,
  kLateBodyGrowth,
  kBudget
};

struct Attempt final {
  bool failing = true;
  Fault fault = Fault::kNone;
  bool zero_work_recovery = false;
};

Fault fault_of(const simulation::GameWorld& world) {
  const auto* score = world.store<simulation::Score>().find(entity(1));
  return score ? static_cast<Fault>(score->points) : Fault::kNone;
}

// External switches belong only to fault injection. Production callbacks remain pure: the
// pre-kernel test stage projects the chosen attempt into the immutable world before solving.
class PrepareAttempt final : public simulation::SimulationSystem {
public:
  explicit PrepareAttempt(std::shared_ptr<Attempt> attempt) : attempt_(std::move(attempt)) {}
  std::string_view name() const noexcept override { return "prepare_motion_attempt"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override {
    static_cast<void>(world.random(simulation::RandomStreamKind::kHazards).next_bits());
    static_cast<void>(world.random(simulation::RandomStreamKind::kHill).next_bits());
    const auto created = world.create_entity();
    world.mutable_store<simulation::Lifetime>().insert_or_assign(created, {17});
    const auto fault = attempt_->failing
                           ? attempt_->fault
                           : (attempt_->zero_work_recovery ? Fault::kNone : Fault::kBudget);
    world.mutable_store<simulation::Score>().insert_or_assign(entity(1),
                                                              {static_cast<std::int64_t>(fault)});
    world.mutable_store<simulation::Stun>().insert_or_assign(
        entity(1), {simulation::TickWindow::create(context.tick_sequence(), 3)});
    // Only a lowered-budget retry needs a zero-work world. Ordinary callback/late failures
    // retry the identical moving subject, valid checkpoint, and later static contact.
    if (!attempt_->failing && attempt_->zero_work_recovery) {
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          entity(1), simulation::PhysicsBody::create_static(point(10, 50)).with_radius(1));
      return;
    }
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        created, simulation::PhysicsBody::create_static(point(40, 50)).with_radius(1));
    if (fault == Fault::kWorldEventOverflow) {
      for (std::size_t index = 0; index < simulation::kMaximumWorldEventCount; ++index) {
        world.emit(simulation::DespawnEvent{entity(900)});
      }
    }
    if (fault == Fault::kBodylessPolicy) {
      world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(entity(900), {});
    } else if (fault == Fault::kDefaultPolicy) {
      world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
          entity(1), {simulation::ContactEffectPolicy::kClosingImpact});
    } else if (fault == Fault::kUnknownPolicy) {
      world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
          entity(1), {static_cast<simulation::ContactEffectPolicy>(99)});
    }
  }

private:
  std::shared_ptr<Attempt> attempt_;
};

simulation::ContactResponse fault_response(const simulation::GameWorld& world, const Subject& first,
                                           const Subject& second,
                                           const simulation::PairContactObservation& observation,
                                           const simulation::TickContext& context) {
  // From x=10 at 50 units/tick, the radius-one subject first touches the static body
  // centered at x=40 at t=.56. The checkpoint at t=.1 must not re-anchor this travel.
  CHECK(first.body.position().x() == Catch::Approx(38));
  CHECK((first.body.position().x() - 10) / 50 == Catch::Approx(0.56));
  CHECK(first.body.velocity() == point(20'000, 0));
  CHECK(second.body.position() == point(40, 50));
  REQUIRE(observation.impact.has_value());
  auto result = simulation::reflect_static_response(world, first, second, observation, context);
  auto changed = result.first_result();
  switch (fault_of(world)) {
  case Fault::kPairThrow:
    throw simulation::SimulationValidationError(
        simulation::SimulationValidationCode::kContinuousMotionInvalidResponse, "test.motion_pair",
        "injected callback failure");
  case Fault::kPairTeleport:
    changed.body = changed.body.with_position(changed.body.position() + point(1, 0));
    break;
  case Fault::kPairMass:
    changed.body = changed.body.with_mass(2);
    break;
  case Fault::kPairDisposition:
    changed.disposition = static_cast<simulation::MotionDisposition>(99);
    break;
  default:
    break;
  }
  return simulation::ContactResponse::create(changed, result.second_result(),
                                             {result.events().begin(), result.events().end()});
}

class FaultPolicy final : public simulation::MotionTriggerPolicy {
public:
  std::optional<std::uint64_t> bind(const simulation::GameWorld& world, simulation::EntityId id,
                                    const simulation::TickContext&) const override {
    return id == entity(1) && fault_of(world) != Fault::kNone ? std::optional<std::uint64_t>{0}
                                                              : std::nullopt;
  }
  std::optional<simulation::MotionTriggerProposal>
  query(const simulation::GameWorld&, const Subject&, const simulation::MotionTriggerWindow&,
        const simulation::TickContext&, std::uint64_t cursor,
        simulation::MotionQueryBudget&) const override {
    return cursor == 0 ? std::optional{simulation::MotionTriggerProposal{
                             simulation::MotionTime::create(0.1),
                             simulation::MotionEventPriority::kCheckpoint}}
                       : std::nullopt;
  }
  simulation::MotionTriggerResponse<simulation::WorldEvent>
  respond(const simulation::GameWorld& world, const Subject& subject,
          const simulation::MotionTriggerEvent& event,
          const simulation::TickContext&) const override {
    CHECK(event.key.time() == simulation::MotionTime::create(0.1));
    CHECK(subject.body.position() == point(15, 50));
    CHECK(subject.body.velocity() == point(20'000, 0));
    simulation::MotionTriggerResponse<simulation::WorldEvent> response{
        {subject.body}, event.cursor + 1, {simulation::DespawnEvent{entity(900)}}};
    switch (fault_of(world)) {
    case Fault::kTriggerThrow:
      throw simulation::SimulationValidationError(
          simulation::SimulationValidationCode::kContinuousMotionInvalidResponse,
          "test.motion_trigger", "injected callback failure");
    case Fault::kTriggerTeleport:
      response.body.body = subject.body.with_position(subject.body.position() + point(1, 0));
      break;
    case Fault::kTriggerDisposition:
      response.body.disposition = static_cast<simulation::MotionDisposition>(99);
      break;
    case Fault::kTriggerNoProgress:
      response.cursor = event.cursor;
      break;
    default:
      break;
    }
    return response;
  }
};

class FailLate final : public simulation::SimulationSystem {
public:
  std::string_view name() const noexcept override { return "fail_after_motion"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    switch (fault_of(world)) {
    case Fault::kLateThrow:
      throw simulation::SimulationValidationError(
          simulation::SimulationValidationCode::kContinuousMotionInvalidResponse,
          "test.motion_late_stage", "injected post-solve failure");
    case Fault::kLateBodylessPolicy:
      // Entity 900 is removed by the valid checkpoint; this malformed row must survive removal.
      world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(entity(901), {});
      break;
    case Fault::kLateDefaultPolicy:
      world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
          entity(1), {simulation::ContactEffectPolicy::kClosingImpact});
      break;
    case Fault::kLateUnknownPolicy:
      world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
          entity(1), {static_cast<simulation::ContactEffectPolicy>(99)});
      break;
    case Fault::kLateBodyGrowth:
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          world.create_entity(),
          simulation::PhysicsBody::create_static(point(70, 70)).with_radius(1));
      break;
    default:
      break;
    }
  }
};

simulation::GameSimulation attempt_game(const std::shared_ptr<Attempt>& attempt,
                                        const std::shared_ptr<Observation>& observation,
                                        const std::shared_ptr<Observation>& entry_observation,
                                        simulation::MotionLimits limits = {}, bool trigger = true) {
  auto world = simulation::GameWorld::create({moving(1, 10, 50, 20'000)});
  world.mutable_match().seats = simulation::SeatRoster::of_size(1);
  world.mutable_match().seats.assign_seat(
      0, simulation::Seat{simulation::ControllerSeat{simulation::ControllerId::create(1)}});
  std::vector<StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPreKernel,
       std::make_unique<const ObserveWorld>(entry_observation, "observe_motion_retry_entry")});
  systems.push_back(
      {simulation::SystemStage::kPreKernel, std::make_unique<const PrepareAttempt>(attempt)});
  systems.push_back({simulation::SystemStage::kPostKernel, std::make_unique<const FailLate>()});
  systems.push_back(
      {simulation::SystemStage::kLifecycle, std::make_unique<const ObserveWorld>(observation)});
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  if (trigger) {
    declarations.push_back({0, 1, std::make_unique<const FaultPolicy>()});
  }
  return game(
      std::move(world),
      simulation::ContactRuleTable::create({simulation::ContactRule::create(
          "fault_pair", simulation::body_is_dynamic, simulation::body_is_static, fault_response)}),
      simulation::MotionTriggerTable::create(std::move(declarations)), limits, std::move(systems));
}

simulation::InputBatch attempt_batch(const simulation::GameSimulation& running) {
  return simulation::InputBatch::create(
      {simulation::SetMovementTuningCommand{simulation::ControllerId::create(1), 17, 0,
                                            simulation::MovementTuning::create(300, 500)}},
      running.accepted_command_kinds(), simulation::EntityIdReservation::create(entity(2), 4));
}

void prove_retry(Fault fault, simulation::MotionLimits limits = {}, bool trigger = true) {
  const bool zero_work_recovery = fault == Fault::kBudget;
  auto attempt = std::make_shared<Attempt>(Attempt{true, fault, zero_work_recovery});
  auto observed = std::make_shared<Observation>();
  auto entry_observed = std::make_shared<Observation>();
  auto retry = attempt_game(attempt, observed, entry_observed, limits, trigger);
  const auto before = retry.snapshot();
  const auto batch = attempt_batch(retry);
  auto expected_code = simulation::SimulationValidationCode::kContinuousMotionInvalidResponse;
  if (fault == Fault::kTriggerNoProgress) {
    expected_code = simulation::SimulationValidationCode::kContinuousMotionNoProgress;
  } else if (fault == Fault::kWorldEventOverflow) {
    expected_code = simulation::SimulationValidationCode::kGameWorldEventLimitExceeded;
  } else if (fault == Fault::kBudget || fault == Fault::kLateBodyGrowth) {
    expected_code = simulation::SimulationValidationCode::kContinuousMotionBudgetExceeded;
  } else if (fault == Fault::kBodylessPolicy || fault == Fault::kDefaultPolicy ||
             fault == Fault::kUnknownPolicy || fault == Fault::kLateBodylessPolicy ||
             fault == Fault::kLateDefaultPolicy || fault == Fault::kLateUnknownPolicy) {
    expected_code = simulation::SimulationValidationCode::kContinuousMotionInvalidInput;
  }
  try {
    static_cast<void>(retry.step(simulation::FixedDelta::canonical(), batch));
    FAIL("injected motion failure was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == expected_code);
    if (fault == Fault::kPairThrow) {
      CHECK(error.context() == "test.motion_pair");
    } else if (fault == Fault::kTriggerThrow) {
      CHECK(error.context() == "test.motion_trigger");
    } else if (fault == Fault::kLateThrow) {
      CHECK(error.context() == "test.motion_late_stage");
    }
  }
  CHECK(retry.snapshot() == before);
  CHECK(retry.tick_sequence() == simulation::TickSequence::zero());

  attempt->failing = false;
  auto fresh_attempt = std::make_shared<Attempt>(Attempt{false, fault, zero_work_recovery});
  auto fresh_observed = std::make_shared<Observation>();
  auto fresh_entry = std::make_shared<Observation>();
  auto fresh = attempt_game(fresh_attempt, fresh_observed, fresh_entry, limits, trigger);
  const auto retry_decisions = retry.step(simulation::FixedDelta::canonical(), batch);
  const auto fresh_decisions = fresh.step(simulation::FixedDelta::canonical(), batch);
  REQUIRE(retry_decisions.entries().size() == 1);
  REQUIRE(fresh_decisions.entries().size() == 1);
  CHECK(retry_decisions.entries().front() == fresh_decisions.entries().front());
  CHECK(retry_decisions.entries().front().status ==
        simulation::MovementTuningDecisionStatus::kApplied);
  CHECK(retry.snapshot() == fresh.snapshot());
  REQUIRE(entry_observed->world.has_value());
  REQUIRE(entry_observed->grid.has_value());
  REQUIRE(fresh_entry->world.has_value());
  REQUIRE(fresh_entry->grid.has_value());
  CHECK(entry_observed->world == fresh_entry->world);
  CHECK(entry_observed->grid == fresh_entry->grid);
  // Observe before PrepareAttempt creates/moves anything: a later successful rebuild cannot
  // repair and conceal an index accidentally published by the failed attempt.
  CHECK(*entry_observed->grid ==
        simulation::SpatialGrid::create(config(), retry.map().bounds(), *entry_observed->world));
  REQUIRE(observed->world.has_value());
  REQUIRE(fresh_observed->world.has_value());
  CHECK(observed->world == fresh_observed->world);
  CHECK(observed->grid == fresh_observed->grid);
  CHECK(observed->world->entity_id_reservation().count() == 3);
  CHECK(observed->world->random_draw_counts() == simulation::RandomDrawCounts{1, 1});
  CHECK(observed->world->store<simulation::Stun>().find(entity(1)) != nullptr);
  if (!zero_work_recovery) {
    REQUIRE(observed->world->events().size() == (trigger ? std::size_t{2} : std::size_t{1}));
    if (trigger) {
      CHECK(std::holds_alternative<simulation::DespawnEvent>(observed->world->events().front()));
    }
    CHECK(std::holds_alternative<simulation::ContactEvent>(observed->world->events().back()));
    CHECK(body_of(retry.snapshot(), 1).position().x() == Catch::Approx(16));
    CHECK(body_of(retry.snapshot(), 1).velocity() == point(-20'000, 0));
  }
}

} // namespace

TEST_CASE("live motion callbacks see one frozen pre-kernel world and current resolved subjects",
          "[unit][simulation][game_simulation][continuous_motion]") {
  auto observed = std::make_shared<Observation>();
  std::vector<StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPreKernel, std::make_unique<const PrepareFrozenWorld>()});
  systems.push_back(
      {simulation::SystemStage::kPostKernel, std::make_unique<const ObserveWorld>(observed)});
  auto running =
      game(simulation::GameWorld::create({moving(1, 10, 50), moving(2, 12, 50), moving(3, 14, 50)}),
           simulation::ContactRuleTable::create(
               {simulation::ContactRule::create("frozen_pair", simulation::body_is_dynamic,
                                                simulation::body_is_dynamic, frozen_response)}),
           simulation::MotionTriggerTable::empty(), {}, std::move(systems));
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  REQUIRE(observed->world.has_value());
  CHECK(observed->world->events().size() == 3);
  CHECK(body_of(running.snapshot(), 3).velocity() == point(1'000, 0));
  CHECK(body_of(running.snapshot(), 3).position() == point(16.5, 50));
}

TEST_CASE(
    "live swapped touch dispatch maps source flags normals and termination without later effects",
    "[unit][simulation][game_simulation][continuous_motion]") {
  auto world = simulation::GameWorld::create({moving(1, 10, 50), moving(2, 12, 50)});
  simulation::assign_contact_effect_policy(world, entity(1),
                                           simulation::ContactEffectPolicy::kAnyTouch);
  auto observed = std::make_shared<Observation>();
  std::vector<StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPostKernel, std::make_unique<const ObserveWorld>(observed)});
  auto running = game(std::move(world),
                      simulation::ContactRuleTable::create({simulation::ContactRule::create(
                          "swapped_termination", entity_two, entity_one, terminate_swapped)}),
                      timed_trigger(2, 0.5), {}, std::move(systems));
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  REQUIRE(observed->world.has_value());
  REQUIRE(observed->world->events().size() == 1);
  const auto& event = std::get<simulation::ContactEvent>(observed->world->events().front());
  CHECK(event.normal == point(1, 0));
  CHECK(body_of(running.snapshot(), 2).position() == point(12, 50));
}

TEST_CASE("injected support loss stops live motion before later pair effects without production "
          "registration",
          "[unit][simulation][game_simulation][continuous_motion]") {
  auto terrain = simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(100, 100), simulation::TerrainGround::kSolid, {},
      {simulation::TerrainHole::create("pit", point(30, 50), 1 + simulation::kPositionTolerance)});
  auto map = simulation::MapDefinition::create("support_motion", std::move(terrain), {}, {},
                                               simulation::MapMetadata::none());
  auto observed = std::make_shared<Observation>();
  std::vector<StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPostKernel, std::make_unique<const ObserveWorld>(observed)});
  auto running = game(simulation::GameWorld::create({moving(1, 10, 50, 20'000), moving(2, 40, 50)}),
                      simulation::ContactRuleTable::built_in(), timed_trigger(1, 0.5, true), {},
                      std::move(systems), std::move(map));
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  REQUIRE(observed->world.has_value());
  REQUIRE(observed->world->events().size() == 1);
  CHECK(std::holds_alternative<simulation::DespawnEvent>(observed->world->events().front()));
  CHECK(body_of(running.snapshot(), 1).position().x() == Catch::Approx(29));
  CHECK(body_of(running.snapshot(), 2).velocity() == point(0, 0));
}

TEST_CASE("eighth motion declaration is read once and remains usable after mode destruction",
          "[unit][simulation][game_simulation][game_mode]") {
  auto reads = std::make_shared<ModeReads>();
  auto running = simulation::GameSimulation::create(
      config(), simulation::GameWorld::create({moving(1, 10, 50, 20'000)}),
      simulation::GameSimulationSetup::of_mode(arena(),
                                               std::make_unique<const CountingMode>(reads)));
  CHECK(reads->destroyed);
  CHECK(reads->calls == std::array<unsigned, 8>{1, 1, 1, 1, 1, 1, 1, 1});
  CHECK(running.motion_triggers().size() == 1);
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  CHECK(body_of(running.snapshot(), 1).position() == point(35, 50));
  CHECK(reads->calls == std::array<unsigned, 8>{1, 1, 1, 1, 1, 1, 1, 1});
}

TEST_CASE("mode and explicit trigger declarations conflict while ordinary defaults remain empty",
          "[unit][simulation][game_simulation][game_mode]") {
  auto reads = std::make_shared<ModeReads>();
  REQUIRE_THROWS_AS(simulation::GameSimulation::create(
                        config(), simulation::GameWorld::create({}),
                        simulation::GameSimulationSetup::of_mode(
                            arena(), std::make_unique<const CountingMode>(reads))
                            .with_motion_triggers(simulation::MotionTriggerTable::empty())),
                    simulation::SimulationValidationError);
  auto defaults = game(simulation::GameWorld::create({}));
  CHECK(defaults.motion_triggers().size() == 0);
}

TEST_CASE(
    "live callback projection and late failures roll back full state and retry like a fresh tick",
    "[unit][simulation][game_simulation][continuous_motion][rollback]") {
  for (const auto fault :
       {Fault::kPairThrow, Fault::kPairTeleport, Fault::kPairMass, Fault::kPairDisposition,
        Fault::kTriggerThrow, Fault::kTriggerTeleport, Fault::kTriggerDisposition,
        Fault::kTriggerNoProgress, Fault::kWorldEventOverflow, Fault::kLateThrow,
        Fault::kBodylessPolicy, Fault::kDefaultPolicy, Fault::kUnknownPolicy,
        Fault::kLateBodylessPolicy, Fault::kLateDefaultPolicy, Fault::kLateUnknownPolicy}) {
    CAPTURE(fault);
    prove_retry(fault);
  }
}

TEST_CASE("the moving rollback control commits its checkpoint at .1 then its pair at .56",
          "[unit][simulation][game_simulation][continuous_motion][rollback]") {
  auto attempt = std::make_shared<Attempt>(Attempt{true, Fault::kBudget});
  auto observed = std::make_shared<Observation>();
  auto entry = std::make_shared<Observation>();
  auto running = attempt_game(attempt, observed, entry);
  const auto decisions = running.step(simulation::FixedDelta::canonical(), attempt_batch(running));
  REQUIRE(decisions.entries().size() == 1);
  CHECK(decisions.entries().front().status == simulation::MovementTuningDecisionStatus::kApplied);
  REQUIRE(observed->world.has_value());
  REQUIRE(observed->world->events().size() == 2);
  CHECK(std::holds_alternative<simulation::DespawnEvent>(observed->world->events()[0]));
  CHECK(std::holds_alternative<simulation::ContactEvent>(observed->world->events()[1]));
  CHECK(body_of(running.snapshot(), 1).position().x() == Catch::Approx(16));
  CHECK(body_of(running.snapshot(), 1).velocity() == point(-20'000, 0));
  CHECK(observed->world->random_draw_counts() == simulation::RandomDrawCounts{1, 1});
  CHECK(observed->world->entity_id_reservation().count() == 3);
}

TEST_CASE("every practical lowered live motion budget rolls back and supports a clean retry",
          "[unit][simulation][game_simulation][continuous_motion][rollback]") {
  for (const auto field :
       {&simulation::MotionLimits::bodies, &simulation::MotionLimits::candidate_pairs,
        &simulation::MotionLimits::pair_examinations, &simulation::MotionLimits::root_queries,
        &simulation::MotionLimits::events, &simulation::MotionLimits::trigger_queries,
        &simulation::MotionLimits::paths, &simulation::MotionLimits::effects,
        &simulation::MotionLimits::trigger_declarations}) {
    simulation::MotionLimits limits;
    limits.*field = field == &simulation::MotionLimits::bodies ? 1 : 0;
    prove_retry(Fault::kBudget, limits);
  }
  simulation::MotionLimits late_limits;
  late_limits.bodies = 2;
  prove_retry(Fault::kLateBodyGrowth, late_limits);
}

TEST_CASE("live startup rejects excessive physical bodies and malformed policy projections",
          "[unit][simulation][game_simulation][continuous_motion][validation]") {
  auto crowded = simulation::GameWorld::create({});
  for (std::size_t index = 0; index <= simulation::kMaximumMotionBodyCount; ++index) {
    crowded.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        entity(index + 1), simulation::PhysicsBody::create_static(point(50, 50)).with_radius(1));
  }
  REQUIRE_THROWS_AS(game(std::move(crowded)), simulation::SimulationValidationError);
  for (const auto policy : {simulation::ContactEffectPolicy::kClosingImpact,
                            static_cast<simulation::ContactEffectPolicy>(99)}) {
    auto world = simulation::GameWorld::create({moving(1, 10, 50)});
    world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(entity(1), {policy});
    REQUIRE_THROWS_AS(game(std::move(world)), simulation::SimulationValidationError);
  }
  auto bodyless = simulation::GameWorld::create({});
  bodyless.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(entity(1), {});
  REQUIRE_THROWS_AS(game(std::move(bodyless)), simulation::SimulationValidationError);
}

TEST_CASE("live walls use effective body radius and preserve initial overlap without snapping",
          "[unit][simulation][game_simulation][continuous_motion][wall]") {
  auto wide = moving(1, 94, 50, 4'000);
  wide.body = wide.body.with_radius(4);
  auto running = game(simulation::GameWorld::create({wide}));
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  // Ten units of travel: two reach the radius-four wall at x=96, then eight return.
  CHECK(body_of(running.snapshot(), 1).position().x() == Catch::Approx(88));
  CHECK(body_of(running.snapshot(), 1).velocity() == point(-4'000, 0));

  auto overlapping = moving(1, 1, 50, -400);
  overlapping.body = overlapping.body.with_radius(4);
  auto overlap = game(simulation::GameWorld::create({overlapping}));
  overlap.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  CHECK(body_of(overlap.snapshot(), 1).position() == point(2, 50));
  CHECK(body_of(overlap.snapshot(), 1).velocity() == point(400, 0));

  auto static_crossing = moving(1, 1, 50);
  static_crossing.body = simulation::PhysicsBody::create_static(point(-1, 50))
                             .with_radius(1)
                             .with_bounds_behavior(simulation::BoundsBehavior::kCross);
  CHECK_THROWS_AS(game(simulation::GameWorld::create({static_crossing})),
                  simulation::SimulationValidationError);
}

TEST_CASE("live zero span axes allow only zero velocity on that axis including after acceleration",
          "[unit][simulation][game_simulation][continuous_motion][wall]") {
  const auto map = simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(2, 100));
  auto running = game(simulation::GameWorld::create({moving(1, 0.25, 50, 0, 400)}),
                      simulation::ContactRuleTable::empty(),
                      simulation::MotionTriggerTable::empty(), {}, {}, map);
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  CHECK(body_of(running.snapshot(), 1).position() == point(0.25, 51));
  auto accelerating = moving(1, 1, 50);
  accelerating.body = accelerating.body.with_acceleration(point(400, 0));
  auto invalid =
      game(simulation::GameWorld::create({accelerating}), simulation::ContactRuleTable::empty(),
           simulation::MotionTriggerTable::empty(), {}, {}, map);
  const auto before = invalid.snapshot();
  CHECK_THROWS_AS(
      invalid.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty()),
      simulation::SimulationValidationError);
  CHECK(invalid.snapshot() == before);
}

TEST_CASE("late radius changes rebuild the next tick spatial coverage before a system reads it",
          "[unit][simulation][game_simulation][continuous_motion][spatial_grid]") {
  auto observed = std::make_shared<Observation>();
  std::vector<StagedSystem> systems;
  systems.push_back(
      {simulation::SystemStage::kPreKernel, std::make_unique<const ObserveWorld>(observed)});
  systems.push_back({simulation::SystemStage::kPostKernel, std::make_unique<const GrowRadius>()});
  auto running = game(simulation::GameWorld::create({moving(1, 10, 50), moving(2, 25, 50)}),
                      simulation::ContactRuleTable::empty(),
                      simulation::MotionTriggerTable::empty(), {}, std::move(systems));
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  running.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  REQUIRE(observed->world.has_value());
  REQUIRE(observed->grid.has_value());
  CHECK(observed->world->store<simulation::PhysicsBody>().find(entity(1))->radius() == 10);
  CHECK(*observed->grid ==
        simulation::SpatialGrid::create(config(), running.map().bounds(), *observed->world));
}
