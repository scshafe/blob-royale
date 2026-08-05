#include "protocol_test_fixture.hpp"

#include "http_error.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::test_fixture;

namespace {

struct RegistryFixture final {
  protocol::HttpError error;
  std::uint16_t status_code;
  std::string_view code;
  bool retryable;
};

[[nodiscard]] protocol::HttpError::Parameters reason(const std::string_view value) {
  protocol::HttpError::Parameters parameters;
  parameters.reason = std::string{value};
  return parameters;
}

[[nodiscard]] protocol::HttpError::Parameters retry_after(const std::uint64_t milliseconds) {
  protocol::HttpError::Parameters parameters;
  parameters.retry_after_ms = milliseconds;
  return parameters;
}

[[nodiscard]] protocol::HttpError::Parameters connection_limit(const std::uint64_t milliseconds,
                                                               const std::uint64_t limit) {
  protocol::HttpError::Parameters parameters;
  parameters.retry_after_ms = milliseconds;
  parameters.connection_limit = limit;
  return parameters;
}

} // namespace

TEST_CASE("HttpError derives every accepted status code and retry policy from its registry",
          "[unit][protocol][http-error]") {
  const RegistryFixture fixtures[] = {
      {protocol::HttpError::create(protocol::HttpErrorCode::kInvalidRequest, "Invalid request."),
       400, "PROTOCOL.INVALID_REQUEST", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kInvalidRequestId,
                                   "Invalid request ID."),
       400, "PROTOCOL.INVALID_REQUEST_ID", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kSubprotocolRequired,
                                   "Snapshot subprotocol is required.", reason("offer required")),
       400, "PROTOCOL.SUBPROTOCOL_REQUIRED", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kWebsocketVersionUnsupported,
                                   "WebSocket version is unsupported."),
       400, "PROTOCOL.WEBSOCKET_VERSION_UNSUPPORTED", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kOriginRejected, "Origin rejected."),
       403, "PROTOCOL.ORIGIN_REJECTED", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kRouteNotFound, "Route not found."),
       404, "PROTOCOL.ROUTE_NOT_FOUND", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kMethodNotAllowed,
                                   "Method not allowed."),
       405, "PROTOCOL.METHOD_NOT_ALLOWED", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kPayloadTooLarge,
                                   "Request body is forbidden."),
       413, "PROTOCOL.PAYLOAD_TOO_LARGE", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kUpgradeRequired,
                                   "WebSocket upgrade required."),
       426, "PROTOCOL.UPGRADE_REQUIRED", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kRateLimited, "Rate limited.",
                                   retry_after(1'000)),
       429, "PROTOCOL.RATE_LIMITED", true},
      {protocol::HttpError::create(protocol::HttpErrorCode::kConnectionLimitReached,
                                   "Connection limit reached.", connection_limit(1'000, 128)),
       429, "PROTOCOL.CONNECTION_LIMIT_REACHED", true},
      {protocol::HttpError::create(protocol::HttpErrorCode::kHeaderTooLarge,
                                   "Headers are too large."),
       431, "PROTOCOL.HEADER_TOO_LARGE", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kInternalFailure, "Internal failure.",
                                   reason("response invariant")),
       500, "SERVICE.INTERNAL_FAILURE", false},
      {protocol::HttpError::create(protocol::HttpErrorCode::kNotReady, "Service not ready.",
                                   retry_after(1'000)),
       503, "SERVICE.NOT_READY", true},
  };

  for (const RegistryFixture& fixture_value : fixtures) {
    CHECK(fixture_value.error.status_code() == fixture_value.status_code);
    CHECK(fixture_value.error.code() == fixture_value.code);
    CHECK(fixture_value.error.retryable() == fixture_value.retryable);
  }
}

TEST_CASE("HttpError synthesizes fixed schema details without caller duplication",
          "[unit][protocol][http-error]") {
  const protocol::HttpError method =
      protocol::HttpError::create(protocol::HttpErrorCode::kMethodNotAllowed, "Method rejected.");
  const protocol::HttpError websocket = protocol::HttpError::create(
      protocol::HttpErrorCode::kWebsocketVersionUnsupported, "Version rejected.");
  const protocol::HttpError payload =
      protocol::HttpError::create(protocol::HttpErrorCode::kPayloadTooLarge, "Body rejected.");
  const protocol::HttpError header =
      protocol::HttpError::create(protocol::HttpErrorCode::kHeaderTooLarge, "Header rejected.");

  REQUIRE(method.allowed_methods().size() == 1);
  CHECK(method.allowed_methods().front() == "GET");
  CHECK(websocket.expected_websocket_version() == std::optional<std::string_view>{"13"});
  CHECK(payload.limit() == std::optional<std::uint64_t>{0});
  CHECK(header.limit() == std::optional<std::uint64_t>{16'384});
}

TEST_CASE("HttpError accepts both retry-after boundaries", "[unit][protocol][http-error]") {
  const protocol::HttpError minimum = protocol::HttpError::create(
      protocol::HttpErrorCode::kRateLimited, "Minimum retry.", retry_after(1));
  const protocol::HttpError maximum =
      protocol::HttpError::create(protocol::HttpErrorCode::kNotReady, "Maximum retry.",
                                  retry_after(protocol::kMaximumRetryAfterMilliseconds));

  CHECK(minimum.retry_after_ms() == std::optional<std::uint64_t>{1});
  CHECK(maximum.retry_after_ms() ==
        std::optional<std::uint64_t>{protocol::kMaximumRetryAfterMilliseconds});
}

TEST_CASE("HttpError accepts both connection-limit detail boundaries",
          "[unit][protocol][http-error]") {
  const protocol::HttpError minimum =
      protocol::HttpError::create(protocol::HttpErrorCode::kConnectionLimitReached,
                                  "Minimum connection limit.", connection_limit(1, 1));
  const protocol::HttpError maximum = protocol::HttpError::create(
      protocol::HttpErrorCode::kConnectionLimitReached, "Maximum connection limit.",
      connection_limit(protocol::kMaximumRetryAfterMilliseconds,
                       protocol::kErrorDetailMaximumLimit));

  CHECK(minimum.limit() == std::optional<std::uint64_t>{1});
  CHECK(maximum.limit() == std::optional<std::uint64_t>{protocol::kErrorDetailMaximumLimit});
}

TEST_CASE("HttpError accepts the exact reason character limit", "[unit][protocol][http-error]") {
  const std::string maximum_reason(protocol::kHttpErrorReasonMaximumCharacterCount, 'r');
  const protocol::HttpError error = protocol::HttpError::create(
      protocol::HttpErrorCode::kInternalFailure, "Bounded reason.", reason(maximum_reason));

  CHECK(error.reason() == std::optional<std::string_view>{maximum_reason});
}

TEST_CASE("HttpError rejects missing details required by its registry entry",
          "[unit][protocol][http-error]") {
  for (const protocol::HttpErrorCode code :
       {protocol::HttpErrorCode::kSubprotocolRequired, protocol::HttpErrorCode::kInternalFailure,
        protocol::HttpErrorCode::kRateLimited, protocol::HttpErrorCode::kNotReady,
        protocol::HttpErrorCode::kConnectionLimitReached}) {
    fixture::require_protocol_error_code(
        [code] { static_cast<void>(protocol::HttpError::create(code, "Missing details.")); },
        protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
  }
}

TEST_CASE("HttpError rejects details forbidden by its registry entry",
          "[unit][protocol][http-error]") {
  fixture::require_protocol_error_code(
      [] {
        static_cast<void>(protocol::HttpError::create(protocol::HttpErrorCode::kOriginRejected,
                                                      "Origin rejected.", reason("unexpected")));
      },
      protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
}

TEST_CASE("HttpError rejects retry-after values outside schema bounds",
          "[unit][protocol][http-error]") {
  constexpr std::array<std::uint64_t, 2> kInvalidRetryAfterValues{
      0, protocol::kMaximumRetryAfterMilliseconds + 1};
  for (const std::uint64_t invalid_retry_after : kInvalidRetryAfterValues) {
    fixture::require_protocol_error_code(
        [invalid_retry_after] {
          static_cast<void>(protocol::HttpError::create(protocol::HttpErrorCode::kRateLimited,
                                                        "Invalid retry.",
                                                        retry_after(invalid_retry_after)));
        },
        protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
  }
}

TEST_CASE("HttpError rejects connection limits outside schema bounds",
          "[unit][protocol][http-error]") {
  constexpr std::array<std::uint64_t, 2> kInvalidConnectionLimits{
      0, protocol::kErrorDetailMaximumLimit + 1};
  for (const std::uint64_t invalid_limit : kInvalidConnectionLimits) {
    fixture::require_protocol_error_code(
        [invalid_limit] {
          static_cast<void>(protocol::HttpError::create(
              protocol::HttpErrorCode::kConnectionLimitReached, "Invalid connection limit.",
              connection_limit(1, invalid_limit)));
        },
        protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
  }
}

TEST_CASE("HttpError rejects a reason above its schema character bound",
          "[unit][protocol][http-error]") {
  const std::string oversized_reason(protocol::kHttpErrorReasonMaximumCharacterCount + 1, 'r');

  fixture::require_protocol_error_code(
      [&oversized_reason] {
        static_cast<void>(protocol::HttpError::create(protocol::HttpErrorCode::kInternalFailure,
                                                      "Oversized reason.",
                                                      reason(oversized_reason)));
      },
      protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
}

TEST_CASE("HttpError rejects empty and overlong messages", "[unit][protocol][http-error]") {
  for (const std::string& invalid_message :
       {std::string{}, std::string(protocol::kHttpErrorMessageMaximumCharacterCount + 1, 'm')}) {
    fixture::require_protocol_error_code(
        [&invalid_message] {
          static_cast<void>(protocol::HttpError::create(protocol::HttpErrorCode::kRouteNotFound,
                                                        invalid_message));
        },
        protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
  }
}

TEST_CASE("HttpError measures schema string limits as Unicode characters",
          "[unit][protocol][http-error]") {
  const std::string multibyte_message(protocol::kHttpErrorMessageMaximumCharacterCount * 2, '\xC3');
  std::string valid_message;
  valid_message.reserve(protocol::kHttpErrorMessageMaximumCharacterCount * 2);
  for (std::size_t index = 0; index < protocol::kHttpErrorMessageMaximumCharacterCount; ++index) {
    valid_message.append("\xC3\xA9");
  }

  const protocol::HttpError valid_error = protocol::HttpError::create(
      protocol::HttpErrorCode::kRouteNotFound, std::move(valid_message));
  CHECK(valid_error.message().size() == protocol::kHttpErrorMessageMaximumCharacterCount * 2);
  fixture::require_protocol_error_code(
      [&multibyte_message] {
        static_cast<void>(protocol::HttpError::create(protocol::HttpErrorCode::kRouteNotFound,
                                                      multibyte_message));
      },
      protocol::ProtocolEncodingErrorCode::kHttpErrorInvalid);
}
