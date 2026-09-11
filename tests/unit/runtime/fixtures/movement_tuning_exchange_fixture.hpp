#ifndef BLOB_ROYALE_TESTING_MOVEMENT_TUNING_EXCHANGE_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_MOVEMENT_TUNING_EXCHANGE_FIXTURE_HPP

#include "command_sink.hpp"
#include "game_simulation.hpp"
#include "movement_tuning_result_delivery.hpp"
#include "seat_roster.hpp"
#include "simulation_validation_error.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace blob_royale::testing::movement_tuning_exchange_fixture {

namespace simulation = blob_royale::simulation;
namespace runtime = blob_royale::runtime;
using Clock = runtime::CommandMailbox::Clock;
inline constexpr Clock::time_point kStart{};
inline constexpr auto kInterval = std::chrono::milliseconds{500};
inline constexpr auto kEarly = std::chrono::milliseconds{100};
inline constexpr auto kJustBefore = std::chrono::microseconds{499'500};
inline constexpr auto kObservationTimeout = std::chrono::seconds{5};
inline const auto kPair = simulation::MovementTuning::create(800.0, 700.0);

inline simulation::ControllerId controller(const std::uint64_t id = 1) {
  return simulation::ControllerId::create(id);
}

inline simulation::SetMovementTuningCommand
request(const std::uint64_t id = 1, const simulation::ControllerId actor = controller(),
        const std::uint64_t revision = 0) {
  return {actor, id, revision, kPair};
}

inline simulation::MovementTuningDecision
decision(const std::uint64_t id = 1, const std::uint64_t tick = 2,
         const simulation::ControllerId actor = controller()) {
  return {actor, id, simulation::MovementTuningDecisionStatus::kApplied,
          simulation::TickSequence::create(tick), id};
}

inline void complete(runtime::CommandMailbox& mailbox,
                     const simulation::MovementTuningDecision outcome = decision()) {
  const std::array outcomes{outcome};
  mailbox.complete_tuning_decisions(outcomes);
}

inline simulation::Command thrust(const std::uint64_t id) {
  return simulation::ThrustCommand{simulation::EntityId::create(id),
                                   simulation::Vector2::create(0.0, 0.0)};
}

struct Exchange final {
  runtime::CommandMailbox mailbox{simulation::CommandKindMask::all()};
  runtime::ControllerDirectory directory;
  runtime::EntityIdAllocator allocator{
      runtime::EntityIdAllocator::create(simulation::EntityId::create(10))};
  runtime::CommandSink sink{mailbox, directory, allocator, simulation::kMinimumControllerId};
  runtime::MovementTuningResultDelivery delivery{mailbox};
};

class RejectTuningCommit final : public simulation::SimulationSystem {
public:
  std::string_view name() const noexcept override { return "reject_tuning_commit"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    if (world.match().movement.revision != 0) {
      throw simulation::SimulationValidationError{
          simulation::SimulationValidationCode::kMovementTuningOutOfRange,
          "movement_tuning_runtime_fixture.commit", "intentional failure after tuning admission"};
    }
  }
};

inline simulation::GameSimulation game(const bool fail_tuning = false) {
  auto world = simulation::GameWorld::create({});
  world.mutable_match().seats = simulation::SeatRoster::of_size(1);
  auto setup = simulation::GameSimulationSetup::engine_defaults();
  if (fail_tuning) {
    std::vector<simulation::SystemPipeline::StagedSystem> systems;
    systems.push_back(
        {simulation::SystemStage::kPostKernel, std::make_unique<RejectTuningCommit>()});
    setup = std::move(setup).with_systems(simulation::SystemPipeline::create(std::move(systems)));
  }
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(100.0, 80.0, 1.0, 400, 10, 8), std::move(world),
      std::move(setup));
}

template <typename Predicate> bool wait_until(Predicate predicate) {
  const auto deadline = Clock::now() + kObservationTimeout;
  while (Clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::yield();
  }
  return predicate();
}

} // namespace blob_royale::testing::movement_tuning_exchange_fixture

#endif
