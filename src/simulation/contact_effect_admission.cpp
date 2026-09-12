#include "contact_effect_admission.hpp"

#include "game_world.hpp"
#include "simulation_validation_error.hpp"

#include <string>

namespace blob_royale::simulation {
namespace {

[[noreturn]] void reject(const std::string& detail) {
  throw SimulationValidationError(SimulationValidationCode::kContinuousMotionInvalidInput,
                                  "contact_effect_admission", detail);
}

void require_body(const GameWorld& world, const EntityId entity) {
  if (world.store<PhysicsBody>().find(entity) == nullptr) {
    reject("contact effect policy requires a PhysicsBody for EntityId " +
           std::to_string(entity.value()));
  }
}

} // namespace

void validate_contact_effect_policy(const ContactEffectPolicy policy) {
  if (policy != ContactEffectPolicy::kClosingImpact && policy != ContactEffectPolicy::kAnyTouch) {
    reject("contact effect policy enum is not declared");
  }
}

void validate_contact_effect_admission(const ContactEffectAdmission& admission) {
  validate_contact_effect_policy(admission.policy);
  if (admission.policy != ContactEffectPolicy::kAnyTouch) {
    reject("closing_impact is represented by absence, never a stored component");
  }
}

std::string_view contact_effect_policy_name(const ContactEffectPolicy policy) {
  validate_contact_effect_policy(policy);
  return policy == ContactEffectPolicy::kAnyTouch ? "any_touch" : "closing_impact";
}

ContactEffectPolicy parse_contact_effect_policy(const std::string_view name) {
  if (name == "closing_impact") {
    return ContactEffectPolicy::kClosingImpact;
  }
  if (name == "any_touch") {
    return ContactEffectPolicy::kAnyTouch;
  }
  reject("contact effect policy '" + std::string(name) +
         "' must be exactly closing_impact or any_touch");
}

void assign_contact_effect_policy(GameWorld& world, const EntityId entity,
                                  const std::optional<ContactEffectPolicy> instance_override,
                                  const std::optional<ContactEffectPolicy> archetype_default) {
  if (instance_override) {
    validate_contact_effect_policy(*instance_override);
  }
  if (archetype_default) {
    validate_contact_effect_policy(*archetype_default);
  }
  require_body(world, entity);
  if (const auto* stored = world.store<ContactEffectAdmission>().find(entity)) {
    validate_contact_effect_admission(*stored);
  }
  const auto policy =
      instance_override.value_or(archetype_default.value_or(ContactEffectPolicy::kClosingImpact));
  if (policy == ContactEffectPolicy::kClosingImpact) {
    world.mutable_store<ContactEffectAdmission>().erase(entity);
  } else {
    world.mutable_store<ContactEffectAdmission>().insert_or_assign(entity,
                                                                   ContactEffectAdmission{policy});
  }
}

ContactEffectPolicy effective_contact_effect_policy(const GameWorld& world, const EntityId entity) {
  require_body(world, entity);
  const auto* admission = world.store<ContactEffectAdmission>().find(entity);
  if (admission == nullptr) {
    return ContactEffectPolicy::kClosingImpact;
  }
  validate_contact_effect_admission(*admission);
  return admission->policy;
}

std::vector<MotionContactEffectPolicy> project_contact_effect_policies(const GameWorld& world) {
  std::vector<MotionContactEffectPolicy> policies;
  policies.reserve(world.store<ContactEffectAdmission>().size());
  for (const auto& entry : world.store<ContactEffectAdmission>().entries()) {
    policies.push_back({entry.entity, effective_contact_effect_policy(world, entry.entity)});
  }
  return policies;
}

} // namespace blob_royale::simulation
