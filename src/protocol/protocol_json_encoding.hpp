#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_JSON_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_JSON_ENCODING_HPP

#include "http_error.hpp"
#include "protocol_constants.hpp"
#include "public_configuration.hpp"
#include "request_id.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::simulation {
class WorldSnapshot;
}

namespace blob_royale::protocol {

// Encodes the complete schema-valid GET /api/v1/config success envelope.
// Throws ProtocolEncodingError if the caller's byte limit is invalid or exceeded.
[[nodiscard]] std::string
encode_configuration_response(const PublicConfiguration& configuration, const RequestId& request_id,
                              std::size_t output_byte_limit = kHttpJsonResponseMaximumByteCount);

// Encodes the complete schema-valid GET /api/v1/health/live success envelope.
// Throws ProtocolEncodingError if the caller's byte limit is invalid or exceeded.
[[nodiscard]] std::string
encode_liveness_response(const RequestId& request_id,
                         std::size_t output_byte_limit = kHttpJsonResponseMaximumByteCount);

// Encodes the complete schema-valid GET /api/v1/health/ready success envelope.
// Throws ProtocolEncodingError if the caller's byte limit is invalid or exceeded.
[[nodiscard]] std::string
encode_readiness_response(const RequestId& request_id,
                          std::size_t output_byte_limit = kHttpJsonResponseMaximumByteCount);

// Encodes one complete schema-valid HTTP error envelope. The HttpError owns status/code parity.
// Throws ProtocolEncodingError if the caller's byte limit is invalid or exceeded.
[[nodiscard]] std::string
encode_error_response(const HttpError& error, const RequestId& request_id,
                      std::size_t output_byte_limit = kHttpJsonResponseMaximumByteCount);

// @extension-point snapshot_encoding -- encodes one immutable snapshot as protocol v1 JSON.
// WorldSnapshot's private construction boundary supplies player/scalar/order invariants. This
// function validates the protocol-only sequence and timestamp rules, then enforces the configured
// limit while serializing. It throws ProtocolEncodingError on an invalid wire value or oversized
// complete frame and never truncates or drops players.
[[nodiscard]] std::string
encode_snapshot_message(const simulation::WorldSnapshot& snapshot, const RequestId& request_id,
                        std::uint64_t message_sequence, std::string_view sent_at_utc,
                        std::size_t output_byte_limit = kSnapshotFrameMaximumByteCount);

} // namespace blob_royale::protocol

#endif
