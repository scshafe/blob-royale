#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_CONTACT_EFFECT_ADMISSION_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_CONTACT_EFFECT_ADMISSION_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "protocol_encoding_error.hpp"

#include "contact_effect_admission.hpp"

namespace blob_royale::protocol {

// Sparse nondefault source-effect admission; closing impact is represented by absence.
// Validate before writing so a malformed store cannot publish a plausible default policy.
// related: docs/protocol/schema/v3/contact-effect-admission-component.schema.json.
template <> struct ComponentWireEncoding<simulation::ContactEffectAdmission> {
  static void encode(const simulation::ContactEffectAdmission& admission,
                     const ComponentEncodingContext&, ComponentObjectSink& sink) {
    try {
      simulation::validate_contact_effect_admission(admission);
    } catch (const simulation::SimulationValidationError& error) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "contact_effect_admission.policy", error.what());
    }
    sink.set_string("policy", simulation::contact_effect_policy_name(admission.policy));
  }
};

} // namespace blob_royale::protocol

#endif
