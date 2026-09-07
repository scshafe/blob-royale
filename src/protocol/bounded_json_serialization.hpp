#ifndef BLOB_ROYALE_PROTOCOL_BOUNDED_JSON_SERIALIZATION_HPP
#define BLOB_ROYALE_PROTOCOL_BOUNDED_JSON_SERIALIZATION_HPP

#include <boost/json/fwd.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace blob_royale::protocol {

// canonical: bounded_json_serialization -- the one way a protocol document becomes bytes.
//
// **This header is `blob_protocol`-internal.** It is the single exception to the domain rule that
// no header in `src/protocol` names a Boost type, and it exists so the v1 and v2 encoders share one
// bounded serializer instead of two: only this domain's own `.cpp` files include it, so no consumer
// of `blob_protocol` needs Boost.JSON to link or to compile, which is what that rule protects
// (`src/protocol/README.md`).
//
// Serializing through a fixed buffer with the limit checked **as it fills** is the property the
// protocol needs: an oversized frame is refused rather than built and measured, so a hostile or
// misconfigured world cannot make the process allocate a document it will then throw away.
// related: protocol_json_encoding.cpp -- the v1 encoders.
// related: protocol_v2_json_encoding.cpp -- the v2 encoders.

// One JSON number in the round-trip-safe representation both schema sets expect: an exact integer
// when the value is integral and within the safe-integer range, otherwise a binary64 literal.
// Canonicalizes `-0` to `0`, which `docs/protocol/v2.md` § "Object member order" requires and v1's
// snapshot contract already required of the values it publishes.
[[nodiscard]] boost::json::value encode_json_number(double value);

// Rejects a caller's byte limit that is zero or above the protocol's own maximum.
// Throws ProtocolEncodingError with OUTPUT_BYTE_LIMIT_INVALID.
void validate_output_byte_limit(std::size_t output_byte_limit,
                                std::size_t protocol_maximum_byte_count, std::string_view context);

// Serializes one complete document, refusing it the moment it would exceed the limit.
// Throws ProtocolEncodingError with OUTPUT_BYTE_LIMIT_INVALID or PAYLOAD_TOO_LARGE. It never
// truncates: a frame that does not fit is a failure, not a shorter frame.
[[nodiscard]] std::string serialize_bounded_json(const boost::json::value& document,
                                                 std::size_t output_byte_limit,
                                                 std::size_t protocol_maximum_byte_count,
                                                 std::string_view context);

} // namespace blob_royale::protocol

#endif
