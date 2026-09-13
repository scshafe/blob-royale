#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_CHARGE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_CHARGE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "protocol_encoding_error.hpp"

#include "components/charge_component.hpp"

namespace blob_royale::protocol {

// Charge publishes independent active and cooldown windows. A hit consumes the active attempt;
// success removes the component (refund), while cancellation preserves cooldown. Empty active
// windows are legal; cooldown remains strictly positive. Momentum belongs only to PhysicsBody.
template <> struct ComponentWireEncoding<simulation::Charge> {
  static void encode(const simulation::Charge& charge, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    const auto activation = charge.activation_tick().value();
    const auto cooldown_expiry = charge.cooldown_window().expiry_tick().value();

    if (activation == 0) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "charge.activation_tick",
                                  "charge requires a positive activation tick");
    }
    if (cooldown_expiry <= activation) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "charge.cooldown_expiry_tick",
                                  "charge requires a cooldown expiry after its activation");
    }

    const auto active_expiry = charge.active_window().expiry_tick().value();
    if (active_expiry < activation ||
        (active_expiry > activation && charge.hit_stun_duration_ticks() == 0)) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "charge.active_expiry_tick",
                                  "an active attempt requires ordered endpoints and positive stun");
    }
    sink.set_unsigned("activation_tick", activation);
    sink.set_unsigned("cooldown_expiry_tick", cooldown_expiry);
    sink.set_unsigned("active_expiry_tick", active_expiry);
    sink.set_unsigned("hit_stun_duration_ticks", charge.hit_stun_duration_ticks());
  }
};

} // namespace blob_royale::protocol

#endif
