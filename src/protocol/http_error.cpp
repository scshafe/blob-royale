#include "http_error.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace blob_royale::protocol {
namespace {

enum class ErrorDetailPolicy {
  kNone,
  kOptionalReason,
  kRequiredReason,
  kAllowedGet,
  kExpectedWebsocketVersion,
  kPayloadLimit,
  kHeaderLimit,
  kRetryAfter,
  kConnectionLimitAndRetryAfter,
};

struct ErrorRegistryEntry final {
  std::uint16_t status_code;
  std::string_view code;
  bool retryable;
  ErrorDetailPolicy detail_policy;
};

[[nodiscard]] const ErrorRegistryEntry& registry_entry(const HttpErrorCode error_code) {
  static constexpr ErrorRegistryEntry kConnectionLimitReached{
      429, "PROTOCOL.CONNECTION_LIMIT_REACHED", true,
      ErrorDetailPolicy::kConnectionLimitAndRetryAfter};
  static constexpr ErrorRegistryEntry kHeaderTooLarge{431, "PROTOCOL.HEADER_TOO_LARGE", false,
                                                      ErrorDetailPolicy::kHeaderLimit};
  static constexpr ErrorRegistryEntry kInvalidRequest{400, "PROTOCOL.INVALID_REQUEST", false,
                                                      ErrorDetailPolicy::kOptionalReason};
  static constexpr ErrorRegistryEntry kInvalidRequestId{400, "PROTOCOL.INVALID_REQUEST_ID", false,
                                                        ErrorDetailPolicy::kNone};
  static constexpr ErrorRegistryEntry kMethodNotAllowed{405, "PROTOCOL.METHOD_NOT_ALLOWED", false,
                                                        ErrorDetailPolicy::kAllowedGet};
  static constexpr ErrorRegistryEntry kOriginRejected{403, "PROTOCOL.ORIGIN_REJECTED", false,
                                                      ErrorDetailPolicy::kNone};
  static constexpr ErrorRegistryEntry kPayloadTooLarge{413, "PROTOCOL.PAYLOAD_TOO_LARGE", false,
                                                       ErrorDetailPolicy::kPayloadLimit};
  static constexpr ErrorRegistryEntry kRateLimited{429, "PROTOCOL.RATE_LIMITED", true,
                                                   ErrorDetailPolicy::kRetryAfter};
  static constexpr ErrorRegistryEntry kRouteNotFound{404, "PROTOCOL.ROUTE_NOT_FOUND", false,
                                                     ErrorDetailPolicy::kNone};
  static constexpr ErrorRegistryEntry kSubprotocolRequired{
      400, "PROTOCOL.SUBPROTOCOL_REQUIRED", false, ErrorDetailPolicy::kRequiredReason};
  static constexpr ErrorRegistryEntry kUpgradeRequired{426, "PROTOCOL.UPGRADE_REQUIRED", false,
                                                       ErrorDetailPolicy::kNone};
  static constexpr ErrorRegistryEntry kWebsocketVersionUnsupported{
      400, "PROTOCOL.WEBSOCKET_VERSION_UNSUPPORTED", false,
      ErrorDetailPolicy::kExpectedWebsocketVersion};
  static constexpr ErrorRegistryEntry kInternalFailure{500, "SERVICE.INTERNAL_FAILURE", false,
                                                       ErrorDetailPolicy::kRequiredReason};
  static constexpr ErrorRegistryEntry kNotReady{503, "SERVICE.NOT_READY", true,
                                                ErrorDetailPolicy::kRetryAfter};

  switch (error_code) {
  case HttpErrorCode::kConnectionLimitReached:
    return kConnectionLimitReached;
  case HttpErrorCode::kHeaderTooLarge:
    return kHeaderTooLarge;
  case HttpErrorCode::kInvalidRequest:
    return kInvalidRequest;
  case HttpErrorCode::kInvalidRequestId:
    return kInvalidRequestId;
  case HttpErrorCode::kMethodNotAllowed:
    return kMethodNotAllowed;
  case HttpErrorCode::kOriginRejected:
    return kOriginRejected;
  case HttpErrorCode::kPayloadTooLarge:
    return kPayloadTooLarge;
  case HttpErrorCode::kRateLimited:
    return kRateLimited;
  case HttpErrorCode::kRouteNotFound:
    return kRouteNotFound;
  case HttpErrorCode::kSubprotocolRequired:
    return kSubprotocolRequired;
  case HttpErrorCode::kUpgradeRequired:
    return kUpgradeRequired;
  case HttpErrorCode::kWebsocketVersionUnsupported:
    return kWebsocketVersionUnsupported;
  case HttpErrorCode::kInternalFailure:
    return kInternalFailure;
  case HttpErrorCode::kNotReady:
    return kNotReady;
  }
  throw ProtocolEncodingError{ProtocolEncodingErrorCode::kHttpErrorInvalid, "http_error.code",
                              "value is not a registered protocol v1 HTTP error code"};
}

[[nodiscard]] std::optional<std::size_t>
valid_utf8_code_point_count(const std::string_view text) noexcept {
  std::size_t code_point_count = 0;
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum_code_point = 0;
    if (first <= 0x7FU) {
      code_point = first;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      continuation_count = 1;
      code_point = first & 0x1FU;
      minimum_code_point = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuation_count = 2;
      code_point = first & 0x0FU;
      minimum_code_point = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuation_count = 3;
      code_point = first & 0x07U;
      minimum_code_point = 0x10000U;
    } else {
      return std::nullopt;
    }

    if (index + continuation_count >= text.size()) {
      return std::nullopt;
    }
    for (std::size_t continuation_index = 0; continuation_index < continuation_count;
         ++continuation_index) {
      const auto continuation = static_cast<unsigned char>(text[index + continuation_index + 1]);
      if ((continuation & 0xC0U) != 0x80U) {
        return std::nullopt;
      }
      code_point = (code_point << 6U) | (continuation & 0x3FU);
    }
    if ((continuation_count > 0 && code_point < minimum_code_point) || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return std::nullopt;
    }

    index += continuation_count + 1;
    ++code_point_count;
  }
  return code_point_count;
}

void require_bounded_utf8(const std::string_view value, const std::size_t maximum_character_count,
                          const std::string_view context) {
  const std::optional<std::size_t> character_count = valid_utf8_code_point_count(value);
  if (!character_count.has_value() || *character_count == 0 ||
      *character_count > maximum_character_count) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kHttpErrorInvalid, std::string{context},
        "value must be valid UTF-8 within the accepted schema character bound"};
  }
}

void require_no_unexpected_parameters(const HttpError::Parameters& parameters) {
  if (parameters.reason.has_value() || parameters.retry_after_ms.has_value() ||
      parameters.connection_limit.has_value()) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kHttpErrorInvalid,
                                "http_error.parameters",
                                "the selected error code does not accept detail parameters"};
  }
}

void require_retry_after(const HttpError::Parameters& parameters) {
  if (!parameters.retry_after_ms.has_value() || *parameters.retry_after_ms == 0 ||
      *parameters.retry_after_ms > kMaximumRetryAfterMilliseconds ||
      parameters.reason.has_value() || parameters.connection_limit.has_value()) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kHttpErrorInvalid, "http_error.parameters.retry_after_ms",
        "the selected error requires only retry_after_ms in the inclusive range 1 to 60000"};
  }
}

} // namespace

HttpError HttpError::create(const HttpErrorCode error_code, std::string message,
                            Parameters parameters) {
  require_bounded_utf8(message, kHttpErrorMessageMaximumCharacterCount, "http_error.message");
  const ErrorRegistryEntry& entry = registry_entry(error_code);

  bool includes_allowed_get = false;
  bool includes_expected_websocket_version = false;
  std::optional<std::uint64_t> limit;
  std::optional<std::string> reason;
  std::optional<std::uint64_t> retry_after_ms;

  switch (entry.detail_policy) {
  case ErrorDetailPolicy::kNone:
    require_no_unexpected_parameters(parameters);
    break;
  case ErrorDetailPolicy::kOptionalReason:
    if (parameters.retry_after_ms.has_value() || parameters.connection_limit.has_value()) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kHttpErrorInvalid,
                                  "http_error.parameters",
                                  "this error accepts only an optional reason"};
    }
    if (parameters.reason.has_value()) {
      require_bounded_utf8(*parameters.reason, kHttpErrorReasonMaximumCharacterCount,
                           "http_error.parameters.reason");
      reason = std::move(parameters.reason);
    }
    break;
  case ErrorDetailPolicy::kRequiredReason:
    if (!parameters.reason.has_value() || parameters.retry_after_ms.has_value() ||
        parameters.connection_limit.has_value()) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kHttpErrorInvalid,
                                  "http_error.parameters.reason",
                                  "this error requires exactly one reason detail"};
    }
    require_bounded_utf8(*parameters.reason, kHttpErrorReasonMaximumCharacterCount,
                         "http_error.parameters.reason");
    reason = std::move(parameters.reason);
    break;
  case ErrorDetailPolicy::kAllowedGet:
    require_no_unexpected_parameters(parameters);
    includes_allowed_get = true;
    break;
  case ErrorDetailPolicy::kExpectedWebsocketVersion:
    require_no_unexpected_parameters(parameters);
    includes_expected_websocket_version = true;
    break;
  case ErrorDetailPolicy::kPayloadLimit:
    require_no_unexpected_parameters(parameters);
    limit = 0;
    break;
  case ErrorDetailPolicy::kHeaderLimit:
    require_no_unexpected_parameters(parameters);
    limit = 16'384;
    break;
  case ErrorDetailPolicy::kRetryAfter:
    require_retry_after(parameters);
    retry_after_ms = parameters.retry_after_ms;
    break;
  case ErrorDetailPolicy::kConnectionLimitAndRetryAfter:
    if (!parameters.retry_after_ms.has_value() || *parameters.retry_after_ms == 0 ||
        *parameters.retry_after_ms > kMaximumRetryAfterMilliseconds ||
        !parameters.connection_limit.has_value() || *parameters.connection_limit == 0 ||
        *parameters.connection_limit > kErrorDetailMaximumLimit || parameters.reason.has_value()) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kHttpErrorInvalid,
                                  "http_error.parameters",
                                  "connection-limit errors require retry_after_ms and "
                                  "connection_limit within schema bounds"};
    }
    retry_after_ms = parameters.retry_after_ms;
    limit = parameters.connection_limit;
    break;
  }

  return HttpError{error_code,
                   entry.status_code,
                   entry.code,
                   std::move(message),
                   entry.retryable,
                   includes_allowed_get,
                   includes_expected_websocket_version,
                   limit,
                   std::move(reason),
                   retry_after_ms};
}

HttpError::HttpError(const HttpErrorCode error_code, const std::uint16_t status_code,
                     const std::string_view code, std::string message, const bool retryable,
                     const bool includes_allowed_get,
                     const bool includes_expected_websocket_version,
                     std::optional<std::uint64_t> limit, std::optional<std::string> reason,
                     std::optional<std::uint64_t> retry_after_ms) noexcept
    : error_code_(error_code), status_code_(status_code), code_(code), message_(std::move(message)),
      retryable_(retryable), includes_allowed_get_(includes_allowed_get),
      includes_expected_websocket_version_(includes_expected_websocket_version), limit_(limit),
      reason_(std::move(reason)), retry_after_ms_(retry_after_ms) {}

std::span<const std::string_view> HttpError::allowed_methods() const& noexcept {
  if (!includes_allowed_get_) {
    return {};
  }
  return allowed_get_;
}

std::optional<std::string_view> HttpError::expected_websocket_version() const noexcept {
  if (!includes_expected_websocket_version_) {
    return std::nullopt;
  }
  return "13";
}

std::optional<std::string_view> HttpError::reason() const& noexcept {
  if (!reason_.has_value()) {
    return std::nullopt;
  }
  return *reason_;
}

} // namespace blob_royale::protocol
