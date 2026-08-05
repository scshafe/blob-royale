#ifndef BLOB_ROYALE_TESTS_INTEGRATION_PROTOCOL_CONTRACT_VALIDATION_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_PROTOCOL_CONTRACT_VALIDATION_HPP

#include "loopback_http_client.hpp"
#include "snapshot_contract_observation.hpp"

#include <boost/beast/http/status.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace blob_royale::integration_test {

// Requires the complete successful configuration schema, configured fixture values, and CORS
// transport metadata. Throws IntegrationTestError on the first contract violation.
void validate_configuration_response(const IntegrationHttpResponse& response,
                                     std::string_view expected_request_id,
                                     std::string_view expected_allowed_origin);

// Requires the complete ready response schema and a correlated request ID.
void validate_readiness_response(const IntegrationHttpResponse& response,
                                 std::string_view expected_request_id);

// Requires the shared closed error schema plus the selected stable status/code pair.
void validate_error_response(const IntegrationHttpResponse& response,
                             boost::beast::http::status expected_status,
                             std::string_view expected_error_code,
                             std::string_view expected_request_id);

// Requires the method-specific Allow header and exact allowed_methods detail.
void validate_method_not_allowed_details(const IntegrationHttpResponse& response);

// Requires the transport header paired with PROTOCOL.UPGRADE_REQUIRED.
void validate_upgrade_required_header(const IntegrationHttpResponse& response);

// Requires the accepted v1 subprotocol, request ID, and absence of compression negotiation.
void validate_snapshot_handshake(const IntegrationHttpResponse& response,
                                 std::string_view expected_request_id);

// Requires one complete snapshot schema and returns its comparable ordered sequences.
[[nodiscard]] SnapshotContractObservation
validate_snapshot_message(std::string_view encoded_message, std::string_view expected_request_id,
                          std::uint64_t expected_message_sequence,
                          std::size_t expected_player_count = 2);

} // namespace blob_royale::integration_test

#endif
