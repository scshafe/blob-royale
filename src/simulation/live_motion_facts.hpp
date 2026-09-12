#ifndef BLOB_ROYALE_SIMULATION_LIVE_MOTION_FACTS_HPP
#define BLOB_ROYALE_SIMULATION_LIVE_MOTION_FACTS_HPP

#include "simulation_validation_error.hpp"

#include <functional>
#include <variant>

namespace blob_royale::simulation {

class ContactRuleTable;
class MotionTriggerPolicy;

// canonical: live_motion_facts -- typed borrowed capabilities for one synchronous live solve.
// The owning contact/trigger tables must outlive the solve. Accessing the wrong capability is
// an invalid-input failure, never a missing policy silently interpreted as a no-op.
class LiveMotionFacts final {
public:
  [[nodiscard]] static LiveMotionFacts for_contacts(const ContactRuleTable& contacts) noexcept {
    return LiveMotionFacts{std::cref(contacts)};
  }
  [[nodiscard]] static LiveMotionFacts for_contacts(const ContactRuleTable&&) = delete;
  [[nodiscard]] static LiveMotionFacts for_trigger(const MotionTriggerPolicy& trigger) noexcept {
    return LiveMotionFacts{std::cref(trigger)};
  }
  [[nodiscard]] static LiveMotionFacts for_trigger(const MotionTriggerPolicy&&) = delete;
  [[nodiscard]] const ContactRuleTable& require_contacts() const {
    if (const auto* contacts =
            std::get_if<std::reference_wrapper<const ContactRuleTable>>(&capability_)) {
      return contacts->get();
    }
    throw SimulationValidationError(SimulationValidationCode::kContinuousMotionInvalidInput,
                                    "live_motion_facts.contacts",
                                    "trigger facts cannot supply the contact rule table");
  }
  [[nodiscard]] const MotionTriggerPolicy& require_trigger() const {
    if (const auto* trigger =
            std::get_if<std::reference_wrapper<const MotionTriggerPolicy>>(&capability_)) {
      return trigger->get();
    }
    throw SimulationValidationError(SimulationValidationCode::kContinuousMotionInvalidInput,
                                    "live_motion_facts.trigger",
                                    "contact facts cannot supply a trigger policy");
  }

private:
  using Capability = std::variant<std::reference_wrapper<const ContactRuleTable>,
                                  std::reference_wrapper<const MotionTriggerPolicy>>;
  explicit LiveMotionFacts(Capability capability) noexcept : capability_(capability) {}
  Capability capability_;
};

} // namespace blob_royale::simulation

#endif
