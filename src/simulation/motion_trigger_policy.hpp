#ifndef BLOB_ROYALE_SIMULATION_MOTION_TRIGGER_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_TRIGGER_POLICY_HPP

#include "continuous_motion.hpp"
#include "world_event_registry.hpp"

namespace blob_royale::simulation {

// @extension-point motion_trigger_policy -- one independently owned immutable unary policy.
// bind returns eligibility/initial cursor only; all geometry is queried through the canonical
// helpers in query using its charged budget. Callbacks read the same frozen kernel-entry world
// and cannot retain world/context/budget references. Current motion is the supplied subject.
// Responses are checked by the solver and must progress, change velocity, or terminate.
class MotionTriggerPolicy {
public:
  virtual ~MotionTriggerPolicy() = default;
  [[nodiscard]] virtual std::optional<std::uint64_t> bind(const GameWorld& world, EntityId entity,
                                                          const TickContext& context) const = 0;
  [[nodiscard]] virtual std::optional<MotionTriggerProposal>
  query(const GameWorld& world, const ContactRule::Subject& subject,
        const MotionTriggerWindow& window, const TickContext& context, std::uint64_t cursor,
        MotionQueryBudget& budget) const = 0;
  [[nodiscard]] virtual MotionTriggerResponse<WorldEvent>
  respond(const GameWorld& world, const ContactRule::Subject& subject,
          const MotionTriggerEvent& event, const TickContext& context) const = 0;
};

} // namespace blob_royale::simulation

#endif
