#ifndef BLOB_ROYALE_GAMEPLAY_RACE_ORDERED_CHECKPOINT_TRIGGER_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_ORDERED_CHECKPOINT_TRIGGER_HPP

#include "motion_trigger_policy.hpp"
#include "motion_triggers.hpp"
#include "race/race_course.hpp"

#include <memory>
#include <vector>

namespace blob_royale::gameplay {

// canonical: ordered_checkpoint_trigger -- owned race gate declaration and certified credit.
// Binds unfinished dynamic Controllables only while running; absent progress starts at zero.
// Query delegates all geometry to ordered_gate_motion_trigger. A finish terminates this quantum
// at rest; checkpoint_progress consumes its fact and clears intent before later ticks can steer.
class OrderedCheckpointTrigger final : public simulation::MotionTriggerPolicy {
public:
  [[nodiscard]] static std::unique_ptr<const simulation::MotionTriggerPolicy>
  create(const RaceCourse& course);
  explicit OrderedCheckpointTrigger(const RaceCourse& course);

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
  const std::vector<simulation::MotionCircleGate> gates_;
};

} // namespace blob_royale::gameplay

#endif
