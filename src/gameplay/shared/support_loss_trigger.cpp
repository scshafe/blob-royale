#include "shared/support_loss_trigger.hpp"

#include "components/controllable_component.hpp"
#include "events/despawn_event.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "match_phase.hpp"
#include "motion_triggers.hpp"
#include "tick_context.hpp"

namespace blob_royale::gameplay {

SupportLossTrigger::SupportLossTrigger(const SupportLossPhasePolicy phase_policy)
    : phase_policy_(phase_policy) {
  if (phase_policy != SupportLossPhasePolicy::kAlways &&
      phase_policy != SupportLossPhasePolicy::kRunningOnly) {
    throw GameplayValidationError(GameplayValidationCode::kSupportLossPhasePolicyInvalid,
                                  "support_loss_trigger.phase_policy",
                                  "support loss phase policy must be always or running-only");
  }
}

std::unique_ptr<const simulation::MotionTriggerPolicy>
SupportLossTrigger::create(const SupportLossPhasePolicy phase_policy) {
  return std::make_unique<const SupportLossTrigger>(phase_policy);
}

std::optional<std::uint64_t> SupportLossTrigger::bind(const simulation::GameWorld& world,
                                                      const simulation::EntityId entity,
                                                      const simulation::TickContext&) const {
  if (phase_policy_ == SupportLossPhasePolicy::kRunningOnly &&
      world.match().phase != simulation::MatchPhase::kRunning) {
    return std::nullopt;
  }
  const auto* body = world.store<simulation::PhysicsBody>().find(entity);
  return body != nullptr && !body->is_static() &&
                 body->ground_attachment() == simulation::GroundAttachment::kGroundBound
             ? std::optional<std::uint64_t>{0}
             : std::nullopt;
}

std::optional<simulation::MotionTriggerProposal>
SupportLossTrigger::query(const simulation::GameWorld&, const simulation::ContactRule::Subject&,
                          const simulation::MotionTriggerWindow& window,
                          const simulation::TickContext& context, const std::uint64_t cursor,
                          simulation::MotionQueryBudget& budget) const {
  return cursor == 0
             ? simulation::support_loss_motion_trigger(context.map().terrain(), window, budget)
             : std::nullopt;
}

simulation::MotionTriggerResponse<simulation::WorldEvent> SupportLossTrigger::respond(
    const simulation::GameWorld& world, const simulation::ContactRule::Subject& subject,
    const simulation::MotionTriggerEvent& event, const simulation::TickContext&) const {
  const simulation::WorldEvent effect =
      world.store<simulation::Controllable>().find(subject.entity) != nullptr
          ? simulation::WorldEvent{simulation::EliminationEvent{subject.entity}}
          : simulation::WorldEvent{simulation::DespawnEvent{subject.entity}};
  // Termination is progress itself: support owns one feature and consumes no gate cursor range.
  return {{subject.body, simulation::MotionDisposition::kTerminate}, event.cursor, {effect}};
}

} // namespace blob_royale::gameplay
