#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_SUPPORT_LOSS_TRIGGER_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_SUPPORT_LOSS_TRIGGER_HPP

#include "motion_trigger_policy.hpp"

#include <cstdint>
#include <memory>

namespace blob_royale::gameplay {

enum class SupportLossPhasePolicy : std::uint8_t {
  kAlways,
  kRunningOnly,
};

// canonical: support_loss_trigger -- shared falling policy, using canonical swept center support.
// Static and floating bodies do not bind. Ground-bound dynamic bodies terminate on first support
// loss; controllers emit elimination for mode-owned return/placement, other bodies emit despawn.
// The phase policy is owned by the declaration, never borrowed from the destroyed mode.
class SupportLossTrigger final : public simulation::MotionTriggerPolicy {
public:
  // Rejects unknown phase policies with GAMEPLAY.SUPPORT_LOSS_PHASE_POLICY_INVALID.
  [[nodiscard]] static std::unique_ptr<const simulation::MotionTriggerPolicy>
  create(SupportLossPhasePolicy phase_policy);

  explicit SupportLossTrigger(SupportLossPhasePolicy phase_policy);

  [[nodiscard]] std::optional<std::uint64_t>
  bind(const simulation::GameWorld& world, simulation::EntityId entity,
       const simulation::TickContext& context) const override;
  [[nodiscard]] std::optional<simulation::MotionTriggerProposal>
  query(const simulation::GameWorld& world, const simulation::ContactRule::Subject& subject,
        const simulation::MotionTriggerWindow& window, const simulation::TickContext& context,
        std::uint64_t cursor, simulation::MotionQueryBudget& budget) const override;
  [[nodiscard]] simulation::MotionTriggerResponse<simulation::WorldEvent>
  respond(const simulation::GameWorld& world, const simulation::ContactRule::Subject& subject,
          const simulation::MotionTriggerEvent& event,
          const simulation::TickContext& context) const override;

private:
  SupportLossPhasePolicy phase_policy_;
};

} // namespace blob_royale::gameplay

#endif
