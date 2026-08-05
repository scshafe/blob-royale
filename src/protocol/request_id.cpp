#include "request_id.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <algorithm>
#include <utility>

namespace blob_royale::protocol {
namespace {

[[nodiscard]] bool is_request_id_character(const char character) noexcept {
  return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '.' || character == '_' ||
         character == ':' || character == '-';
}

} // namespace

RequestId RequestId::create(std::string value) {
  if (value.empty() || value.size() > kRequestIdMaximumCharacterCount ||
      !std::ranges::all_of(value, is_request_id_character)) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kRequestIdInvalid, "request_id",
                                "value must contain 1 to 64 characters from [A-Za-z0-9._:-]"};
  }
  return RequestId{std::move(value)};
}

RequestId::RequestId(std::string value) noexcept : value_(std::move(value)) {}

RequestId decode_request_id(const std::string_view encoded_request_id) {
  return RequestId::create(std::string{encoded_request_id});
}

} // namespace blob_royale::protocol
