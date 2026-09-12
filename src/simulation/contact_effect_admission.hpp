#ifndef BLOB_ROYALE_SIMULATION_CONTACT_EFFECT_ADMISSION_HPP
#define BLOB_ROYALE_SIMULATION_CONTACT_EFFECT_ADMISSION_HPP

#include "components/contact_effect_admission_component.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

class GameWorld;

// canonical: contact_effect_admission -- validation, sparse storage, and frozen solver projection.
// All invalid enum/string values, redundant stored defaults, and bodyless policy rows throw
// SimulationValidationError with kContinuousMotionInvalidInput and a contact_effect_admission
// context. No invalid input is interpreted as the default policy.
void validate_contact_effect_policy(ContactEffectPolicy policy);
void validate_contact_effect_admission(const ContactEffectAdmission& admission);
[[nodiscard]] std::string_view contact_effect_policy_name(ContactEffectPolicy policy);
[[nodiscard]] ContactEffectPolicy parse_contact_effect_policy(std::string_view name);

// Requires a PhysicsBody for entity. Validates both optional inputs before mutation, then applies
// instance override > archetype default > closing impact. Closing impact erases sparse storage.
void assign_contact_effect_policy(
    GameWorld& world, EntityId entity,
    std::optional<ContactEffectPolicy> instance_override = std::nullopt,
    std::optional<ContactEffectPolicy> archetype_default = std::nullopt);

// Requires a PhysicsBody and validates a present sparse row; absence means closing impact.
[[nodiscard]] ContactEffectPolicy effective_contact_effect_policy(const GameWorld& world,
                                                                  EntityId entity);

// Validates every stored row and returns sparse any-touch rows in ascending EntityId order.
// Call at startup and against the immutable kernel-entry world before solving.
[[nodiscard]] std::vector<MotionContactEffectPolicy>
project_contact_effect_policies(const GameWorld& world);

} // namespace blob_royale::simulation

#endif
