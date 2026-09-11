#ifndef BLOB_ROYALE_TESTING_MOVEMENT_TUNING_COMMAND_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_MOVEMENT_TUNING_COMMAND_FIXTURE_HPP

#include "game_simulation.hpp"
#include "movement_tuning.hpp"
#include "seat_roster.hpp"
#include "simulation_system.hpp"
#include "simulation_validation_error.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace blob_royale::testing::movement_tuning_command_fixture {

namespace simulation = blob_royale::simulation;
inline const auto kFirstPair = simulation::MovementTuning::create(800.0, 700.0);
inline const auto kSecondPair = simulation::MovementTuning::create(1'200.0, 900.0);
inline const auto kRejectedPair = simulation::MovementTuning::create(2'000.0, 1'000.0);
inline constexpr std::uint64_t kFirstController = 1;
inline constexpr std::uint64_t kSecondController = 2;
inline constexpr std::uint64_t kNpcController = 3;
inline constexpr std::uint64_t kUnseatedController = 4;

inline simulation::ControllerId controller(const std::uint64_t id) {
  return simulation::ControllerId::create(id);
}

inline simulation::SetMovementTuningCommand
command(const std::uint64_t id, const std::uint64_t request, const std::uint64_t revision,
        const simulation::MovementTuning tuning = kFirstPair) {
  return {controller(id), request, revision, tuning};
}

inline simulation::InputBatch batch(std::vector<simulation::Command> commands) {
  return simulation::InputBatch::create(std::move(commands), simulation::CommandKindMask::all(),
                                        simulation::EntityIdReservation::none());
}

// Failure occurs after phase0 has selected/applied its request but before any commit can escape.
class RejectSelectedTuning final : public simulation::SimulationSystem {
public:
  std::string_view name() const noexcept override { return "reject_selected_tuning"; }
  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    if (world.match().movement.current == kRejectedPair) {
      throw simulation::SimulationValidationError{
          simulation::SimulationValidationCode::kMovementTuningOutOfRange,
          "movement_tuning_fixture.after_phase0", "intentional later-stage failure"};
    }
  }
};

inline simulation::GameSimulation
game(const simulation::MatchPhase phase = simulation::MatchPhase::kLobby,
     const std::uint64_t revision = 0, const bool reject_selected = false) {
  auto world = simulation::GameWorld::create({});
  auto& match = world.mutable_match();
  match.phase = phase;
  match.previous_phase = phase;
  match.movement.revision = revision;
  match.seats = simulation::SeatRoster::of_size(4);
  match.seats.assign_seat(0, simulation::ControllerSeat{controller(kFirstController)});
  match.seats.assign_seat(1, simulation::ControllerSeat{controller(kSecondController)});
  match.seats.assign_seat(2, simulation::NpcSeat{simulation::SeatKindName::create("wanderer"),
                                                 controller(kNpcController)});
  auto setup = simulation::GameSimulationSetup::engine_defaults();
  if (reject_selected) {
    std::vector<simulation::SystemPipeline::StagedSystem> systems;
    systems.push_back(
        {simulation::SystemStage::kPostKernel, std::make_unique<RejectSelectedTuning>()});
    setup = std::move(setup).with_systems(simulation::SystemPipeline::create(std::move(systems)));
  }
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500.0, 500.0, 10.0, 400, 8, 8), std::move(world),
      std::move(setup));
}

} // namespace blob_royale::testing::movement_tuning_command_fixture

#endif
