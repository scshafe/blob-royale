#include "static_body_declaration.hpp"

#include "contact_effect_admission.hpp"
#include "simulation_validation_error.hpp"

namespace blob_royale::simulation {

StaticBodyDeclaration StaticBodyDeclaration::create(const PhysicsBody body,
                                                    const ContactEffectPolicy policy) {
  if (!body.is_static()) {
    throw SimulationValidationError(SimulationValidationCode::kMapStaticBodyNotStatic,
                                    "static_body_declaration.is_static",
                                    "an authored static object must carry a static PhysicsBody");
  }
  validate_contact_effect_policy(policy);
  return StaticBodyDeclaration(body, policy);
}

} // namespace blob_royale::simulation
