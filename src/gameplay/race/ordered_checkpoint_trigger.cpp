#include "race/ordered_checkpoint_trigger.hpp"

#include "components/controllable_component.hpp"
#include "components/race_progress_component.hpp"
#include "events/race_checkpoint_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"

#include <string>

namespace blob_royale::gameplay {
namespace {

[[nodiscard]] std::vector<simulation::MotionCircleGate> gates_of(const RaceCourse& course) {
  std::vector<simulation::MotionCircleGate> gates;
  gates.reserve(course.checkpoints().size());
  for (const auto& checkpoint : course.checkpoints()) {
    gates.push_back({checkpoint, course.checkpoint_radius()});
  }
  return gates;
}

} // namespace

std::unique_ptr<const simulation::MotionTriggerPolicy>
OrderedCheckpointTrigger::create(const RaceCourse& course) {
  return std::make_unique<const OrderedCheckpointTrigger>(course);
}

OrderedCheckpointTrigger::OrderedCheckpointTrigger(const RaceCourse& course)
    : gates_(gates_of(course)) {}

std::optional<std::uint64_t> OrderedCheckpointTrigger::bind(const simulation::GameWorld& world,
                                                            const simulation::EntityId entity,
                                                            const simulation::TickContext&) const {
  const auto* body = world.store<simulation::PhysicsBody>().find(entity);
  if (world.match().phase != simulation::MatchPhase::kRunning || body == nullptr ||
      body->is_static() || world.store<simulation::Controllable>().find(entity) == nullptr) {
    return std::nullopt;
  }
  const auto* progress = world.store<simulation::RaceProgress>().find(entity);
  const std::uint64_t cursor = progress == nullptr ? 0 : progress->next_checkpoint;
  if (cursor > gates_.size()) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceProgressBeyondCourse, "ordered_checkpoint.race_progress",
        "entity " + std::to_string(entity.value()) + " has progress beyond the bound course");
  }
  return cursor == gates_.size() ? std::nullopt : std::optional{cursor};
}

std::optional<simulation::MotionTriggerProposal> OrderedCheckpointTrigger::query(
    const simulation::GameWorld&, const simulation::ContactRule::Subject&,
    const simulation::MotionTriggerWindow& window, const simulation::TickContext&,
    const std::uint64_t cursor, simulation::MotionQueryBudget& budget) const {
  return simulation::ordered_gate_motion_trigger(gates_, cursor, window, budget);
}

simulation::MotionTriggerResponse<simulation::WorldEvent> OrderedCheckpointTrigger::respond(
    const simulation::GameWorld&, const simulation::ContactRule::Subject& subject,
    const simulation::MotionTriggerEvent& event, const simulation::TickContext&) const {
  if (event.cursor >= gates_.size()) {
    throw GameplayValidationError(GameplayValidationCode::kRaceCheckpointEventInvalid,
                                  "ordered_checkpoint.response",
                                  "checkpoint response cursor must name an unfinished gate");
  }
  const std::uint64_t next = event.cursor + 1;
  const bool finished = next == gates_.size();
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  return {{finished ? subject.body.with_velocity(zero).with_acceleration(zero) : subject.body,
           finished ? simulation::MotionDisposition::kTerminate
                    : simulation::MotionDisposition::kContinue},
          next,
          {simulation::RaceCheckpointEvent{subject.entity, next, event.key.time()}}};
}

} // namespace blob_royale::gameplay
