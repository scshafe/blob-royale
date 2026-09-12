#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_CHARGE_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_CHARGE_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "protocol_encoding_error.hpp"

#include "components/charge_component.hpp"

namespace blob_royale::protocol {

// Both stored values are public to every reader, on the argument the shield encoder beside this one
// makes: the current effect parameters of a move an opponent can already see are not a secret worth
// an asymmetry between a browser drawing a cooldown arc and a C++ bot deciding whether to commit
// (`docs/reviews/2026-09-12-shield-composition-contract.md`, "Publication and trust boundaries").
//
// **Two numbers, and the activation is one of them even though the cooldown expiry alone would say
// when the next charge is admissible.** A cooldown arc needs a denominator, and
// `[abilities] charge_cooldown_seconds` is deliberately server-side, so a client that received only
// the expiry could tell the player *whether* charge is ready and never *how nearly*. Absolute
// endpoints, never a countdown, for the reason `docs/protocol/v3.md` § snapshot gives for `stun`:
// a countdown is readable only against the frame it arrived in, while an absolute tick stays true
// in a frame a client buffered, replayed, or received late.
//
// **The expiry check is `<`, not `<=`, and that is a deliberate difference from `shield`'s.** The
// shield encoder admits an expiry equal to its activation twice over: `shield_cooldown_seconds` may
// legally round to zero ticks, and a stun cancelling protection on the activation tick yields a
// genuinely empty `[activation, activation)` protection window. Charge has neither escape. Its
// cooldown is validated strictly positive at configuration -- `require_positive_ticks`, because a
// charge cooldown of zero ticks would admit a burst on every one of four hundred ticks a second,
// where the shield's zero-cooldown exemption is covered by a second gate charge does not have --
// and charge is one-shot, so it owns no cancellable window for anything to shorten
// (`docs/reviews/2026-09-12-charge-contract.md` § "Authored tuning and value ownership" and
// § "The component and the command"). An expiry that did not strictly exceed the activation would
// therefore be a broken world, and publishing one would advertise a charge as permanently ready.
//
// TickWindow already guarantees safe integers, so nothing here re-checks the tick domain; what it
// cannot guarantee is that the window it was handed came from a validated activation.
// related: docs/protocol/schema/v3/charge-component.schema.json -- the closed wire shape.
// related: components/shield_component_encoding.hpp -- the other ability value, whose endpoint
//          checks are inclusive for the two reasons this one's are not.
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

    sink.set_unsigned("activation_tick", activation);
    sink.set_unsigned("cooldown_expiry_tick", cooldown_expiry);
  }
};

} // namespace blob_royale::protocol

#endif
