#ifndef BLOB_ROYALE_SIMULATION_STATIC_BODY_DECLARATION_HPP
#define BLOB_ROYALE_SIMULATION_STATIC_BODY_DECLARATION_HPP

#include "motion_contact_observation.hpp"
#include "physics_body.hpp"

namespace blob_royale::simulation {

// canonical: static_body_declaration -- one authored static object and its explicit effect policy.
// The map validates position against terrain; this value rejects dynamic bodies and unknown policy
// enums. No authored radius is added: GameWorld retains configured-radius normalization.
class StaticBodyDeclaration final {
public:
  [[nodiscard]] static StaticBodyDeclaration create(PhysicsBody body, ContactEffectPolicy policy);
  [[nodiscard]] const PhysicsBody& body() const& noexcept { return body_; }
  [[nodiscard]] const PhysicsBody& body() const&& = delete;
  [[nodiscard]] ContactEffectPolicy contact_effect_policy() const noexcept { return policy_; }
  friend bool operator==(const StaticBodyDeclaration&, const StaticBodyDeclaration&) = default;

private:
  StaticBodyDeclaration(PhysicsBody body, ContactEffectPolicy policy) noexcept
      : body_(body), policy_(policy) {}
  PhysicsBody body_;
  ContactEffectPolicy policy_;
};

} // namespace blob_royale::simulation

#endif
