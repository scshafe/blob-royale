#ifndef BLOB_ROYALE_PROTOCOL_HTTP_ERROR_HPP
#define BLOB_ROYALE_PROTOCOL_HTTP_ERROR_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace blob_royale::protocol {

enum class HttpErrorCode {
  kConnectionLimitReached,
  kHeaderTooLarge,
  kInvalidRequest,
  kInvalidRequestId,
  kMethodNotAllowed,
  kOriginRejected,
  kPayloadTooLarge,
  kRateLimited,
  kRouteNotFound,
  kSubprotocolRequired,
  kUpgradeRequired,
  kWebsocketVersionUnsupported,
  kInternalFailure,
  kNotReady,
};

// canonical: http_error -- one schema-valid member of the accepted status/error registry.
class HttpError final {
public:
  struct Parameters final {
    std::optional<std::string> reason;
    std::optional<std::uint64_t> retry_after_ms;
    std::optional<std::uint64_t> connection_limit;
  };

  // Creates one error whose status, code, retryability, and details agree with the v1 registry.
  // Throws ProtocolEncodingError with HTTP_ERROR_INVALID for an invalid message or parameter set.
  [[nodiscard]] static HttpError create(HttpErrorCode error_code, std::string message,
                                        Parameters parameters = {});

  HttpError(const HttpError&) = default;
  HttpError(HttpError&&) noexcept = default;
  HttpError& operator=(const HttpError&) = default;
  HttpError& operator=(HttpError&&) noexcept = default;
  ~HttpError() = default;

  [[nodiscard]] HttpErrorCode error_code() const noexcept { return error_code_; }
  [[nodiscard]] std::uint16_t status_code() const noexcept { return status_code_; }
  [[nodiscard]] std::string_view code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const& noexcept { return message_; }
  [[nodiscard]] const std::string& message() const&& = delete;
  [[nodiscard]] bool retryable() const noexcept { return retryable_; }
  [[nodiscard]] std::span<const std::string_view> allowed_methods() const& noexcept;
  [[nodiscard]] std::span<const std::string_view> allowed_methods() const&& = delete;
  [[nodiscard]] std::optional<std::string_view> expected_websocket_version() const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> limit() const noexcept { return limit_; }
  [[nodiscard]] std::optional<std::string_view> reason() const& noexcept;
  [[nodiscard]] std::optional<std::string_view> reason() const&& = delete;
  [[nodiscard]] std::optional<std::uint64_t> retry_after_ms() const noexcept {
    return retry_after_ms_;
  }

  friend bool operator==(const HttpError&, const HttpError&) = default;

private:
  HttpError(HttpErrorCode error_code, std::uint16_t status_code, std::string_view code,
            std::string message, bool retryable, bool includes_allowed_get,
            bool includes_expected_websocket_version, std::optional<std::uint64_t> limit,
            std::optional<std::string> reason,
            std::optional<std::uint64_t> retry_after_ms) noexcept;

  HttpErrorCode error_code_;
  std::uint16_t status_code_;
  std::string_view code_;
  std::string message_;
  bool retryable_;
  bool includes_allowed_get_;
  bool includes_expected_websocket_version_;
  std::optional<std::uint64_t> limit_;
  std::optional<std::string> reason_;
  std::optional<std::uint64_t> retry_after_ms_;
  std::array<std::string_view, 1> allowed_get_{"GET"};
};

} // namespace blob_royale::protocol

#endif
