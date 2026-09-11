#ifndef BLOB_ROYALE_PROTOCOL_UTC_TIMESTAMP_HPP
#define BLOB_ROYALE_PROTOCOL_UTC_TIMESTAMP_HPP

#include <string_view>

namespace blob_royale::protocol {

// canonical: utc_timestamp -- the one acceptance test for a wire `sent_at_utc` value.
//
// Both protocol versions define `utc_timestamp` identically -- a bounded RFC 3339 UTC value of 20
// to 32 characters ending in `Z`, with an optional fractional second -- so there is one
// implementation and both encoders call it. It was private to the v1 encoder's translation unit
// until protocol v3 needed the same answer; a second copy would have been two answers to one
// question, which is exactly the duplication this repository keeps closing.
//
// The check is a scan and not a `std::regex` or a locale-aware parse: the value is
// observability-only metadata, it is validated on every frame at presentation cadence, and a date
// parser that consults a locale would make one machine's wire bytes differ from another's.
// related: protocol_json_encoding.hpp -- the v1 encoder that validates it.
// related: protocol_v3_json_encoding.hpp -- the v3 encoders that validate it.
[[nodiscard]] bool is_accepted_utc_timestamp(std::string_view value) noexcept;

} // namespace blob_royale::protocol

#endif
