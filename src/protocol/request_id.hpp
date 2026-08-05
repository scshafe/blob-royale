#ifndef BLOB_ROYALE_PROTOCOL_REQUEST_ID_HPP
#define BLOB_ROYALE_PROTOCOL_REQUEST_ID_HPP

#include <string>
#include <string_view>

namespace blob_royale::protocol {

// canonical: request_id -- validated untrusted correlation value; it conveys no authority.
class RequestId final {
public:
  // Validates the complete v1 ASCII grammar and returns an owned request ID.
  // Throws ProtocolEncodingError with REQUEST_ID_INVALID when the value is not accepted.
  [[nodiscard]] static RequestId create(std::string value);

  RequestId(const RequestId&) = default;
  RequestId(RequestId&&) noexcept = default;
  RequestId& operator=(const RequestId&) = default;
  RequestId& operator=(RequestId&&) noexcept = default;
  ~RequestId() = default;

  [[nodiscard]] const std::string& value() const& noexcept { return value_; }
  [[nodiscard]] const std::string& value() const&& = delete;

  friend bool operator==(const RequestId&, const RequestId&) = default;

private:
  explicit RequestId(std::string value) noexcept;

  std::string value_;
};

// Decodes an untrusted X-Request-ID field into the canonical validated value.
// Throws ProtocolEncodingError with REQUEST_ID_INVALID for any grammar violation.
[[nodiscard]] RequestId decode_request_id(std::string_view encoded_request_id);

} // namespace blob_royale::protocol

#endif
