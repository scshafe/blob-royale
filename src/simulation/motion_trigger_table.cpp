#include "motion_trigger_table.hpp"

#include "game_world.hpp"

namespace blob_royale::simulation {
namespace {

std::optional<MotionTriggerProposal>
query_policy(const GameWorld& world, const ContactRule::Subject& subject,
             const MotionTriggerWindow& window, const TickContext& context,
             const LiveMotionFacts& facts, const std::uint64_t cursor, MotionQueryBudget& budget) {
  return facts.require_trigger().query(world, subject, window, context, cursor, budget);
}

MotionTriggerResponse<WorldEvent> respond_policy(const GameWorld& world,
                                                 const ContactRule::Subject& subject,
                                                 const MotionTriggerEvent& event,
                                                 const TickContext& context,
                                                 const LiveMotionFacts& facts) {
  return facts.require_trigger().respond(world, subject, event, context);
}

} // namespace

MotionTriggerTable MotionTriggerTable::create(std::vector<Declaration> declarations) {
  detail::require_motion_budget(declarations.size(), kMaximumMotionTriggerDeclarationCount,
                                "motion trigger policy declaration budget exhausted");
  for (const auto& declaration : declarations) {
    if (!declaration.policy || declaration.cursor_limit > kMaximumMotionTriggerCursorValue ||
        declaration.feature_base > kMaximumProtocolSafeInteger - declaration.cursor_limit) {
      detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                          "invalid motion trigger policy, cursor, or feature range");
    }
  }
  return MotionTriggerTable{std::move(declarations)};
}

MotionTriggerTable MotionTriggerTable::empty() { return MotionTriggerTable::create({}); }

BoundMotionTriggers MotionTriggerTable::bind(const GameWorld& world, const TickContext& context,
                                             const MotionLimits& limits) const& {
  detail::validate_motion_limits(limits);
  const auto bodies = world.store<PhysicsBody>().entries();
  detail::require_motion_budget(bodies.size(), limits.bodies,
                                "motion trigger binding body budget exhausted");
  for (const auto& declaration : declarations_) {
    if (declaration.cursor_limit > limits.trigger_cursor) {
      detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                          "motion trigger policy cursor exceeds the solve limit");
    }
  }
  struct Binding final {
    EntityId entity;
    std::size_t declaration;
    std::uint64_t cursor;
  };
  std::vector<Binding> bindings;
  for (const auto& body : bodies) {
    for (std::size_t index = 0; index < declarations_.size(); ++index) {
      const auto& declaration = declarations_[index];
      const auto cursor = declaration.policy->bind(world, body.entity, context);
      if (!cursor) {
        continue;
      }
      if (*cursor > declaration.cursor_limit) {
        detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                            "motion trigger policy bound an out-of-range initial cursor");
      }
      detail::require_motion_budget(bindings.size() + 1, limits.trigger_declarations,
                                    "motion trigger binding row budget exhausted");
      bindings.push_back({body.entity, index, *cursor});
    }
  }
  BoundMotionTriggers result;
  result.facts_.reserve(bindings.size());
  result.rows_.reserve(bindings.size());
  for (const auto& binding : bindings) {
    result.facts_.push_back(
        LiveMotionFacts::for_trigger(*declarations_[binding.declaration].policy));
  }
  for (std::size_t index = 0; index < bindings.size(); ++index) {
    const auto& binding = bindings[index];
    const auto& declaration = declarations_[binding.declaration];
    result.rows_.push_back({binding.entity, declaration.feature_base, binding.cursor,
                            declaration.cursor_limit, query_policy, respond_policy,
                            std::cref(result.facts_[index])});
  }
  return result;
}

} // namespace blob_royale::simulation
