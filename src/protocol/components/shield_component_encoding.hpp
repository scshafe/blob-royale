#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_SHIELD_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_SHIELD_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "protocol_encoding_error.hpp"

#include "components/shield_component.hpp"

namespace blob_royale::protocol {

// Every stored value is public to every reader. A browser drawing the ring and a C++ bot deciding
// whether to close read the same five numbers, because the current effect parameters of a move an
// opponent can already see are not a secret worth an asymmetry
// (`docs/reviews/2026-09-12-shield-composition-contract.md`, "Publication and trust boundaries").
//
// The three windows share one activation, so the compact shape is that activation plus each
// window's expiry rather than three `{activation, expiry}` pairs saying the same thing three times.
// Absolute endpoints, never durations or countdowns, for the reason `docs/protocol/v3.md` §
// snapshot gives for `stun`: a countdown is readable only against the frame it arrived in, while
// an absolute tick stays true in a frame a client buffered, replayed, or received late.
//
// **The endpoint checks are `<=`, not `<`, on the protection windows.** Cancelling protection at
// the activation tick legitimately yields an empty `[activation, activation)` interval -- a shield
// a stun ended before it ever protected anything -- and that is a state the world can hold and must
// therefore be able to publish. Only a *reversed* interval is a defect. Cooldown is the same:
// `[abilities] shield_cooldown_seconds` may round to zero ticks, which is legal and documented, so
// an expiry equal to the activation is ordinary rather than out of range. Activation and the
// captured parry stun duration are the two values that may never be zero: a zero activation is the
// unset tick and a zero stun duration would publish an effect the defender cannot actually inflict.
//
// TickWindow already guarantees safe integers, so nothing here re-checks the tick domain; what it
// cannot guarantee is the relation between three separately constructed windows.
// related: docs/protocol/schema/v3/shield-component.schema.json -- the closed wire shape.
// related: components/stun_component_encoding.hpp -- the same fail-closed shape for the other
//          body-bound status window.
template <> struct ComponentWireEncoding<simulation::Shield> {
  static void encode(const simulation::Shield& shield, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    const auto activation = shield.activation_tick().value();
    const auto shield_expiry = shield.shield_window().expiry_tick().value();
    const auto perfect_expiry = shield.perfect_window().expiry_tick().value();
    const auto cooldown_expiry = shield.cooldown_window().expiry_tick().value();
    const auto parry_stun_duration_ticks = shield.parry_stun_duration_ticks();

    if (activation == 0) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "shield.activation_tick",
                                  "shield requires a positive activation tick");
    }
    if (parry_stun_duration_ticks == 0) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "shield.parry_stun_duration_ticks",
                                  "shield requires a positive captured parry stun duration");
    }
    if (perfect_expiry < activation || shield_expiry < perfect_expiry) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "shield.protection_windows",
                                  "shield requires activation <= perfect expiry <= shield expiry");
    }
    if (cooldown_expiry < activation) {
      throw ProtocolEncodingError(ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "shield.cooldown_expiry_tick",
                                  "shield requires activation <= cooldown expiry");
    }

    sink.set_unsigned("activation_tick", activation);
    sink.set_unsigned("shield_expiry_tick", shield_expiry);
    sink.set_unsigned("perfect_expiry_tick", perfect_expiry);
    sink.set_unsigned("cooldown_expiry_tick", cooldown_expiry);
    sink.set_unsigned("parry_stun_duration_ticks", parry_stun_duration_ticks);
  }
};

} // namespace blob_royale::protocol

#endif
