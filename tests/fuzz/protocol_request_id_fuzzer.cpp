#include "protocol_encoding_error.hpp"
#include "request_id.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace {

namespace protocol = blob_royale::protocol;

[[nodiscard]] std::string_view materialize_seed(std::string_view encoded) noexcept {
  constexpr std::string_view kValidSeedPrefix = "VALID:";
  if (!encoded.starts_with(kValidSeedPrefix)) {
    return encoded;
  }
  encoded.remove_prefix(kValidSeedPrefix.size());
  if (encoded.ends_with('\n')) {
    encoded.remove_suffix(1);
  }
  return encoded;
}

[[nodiscard]] bool is_documented_request_id_character(const char character) noexcept {
  return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '.' || character == '_' ||
         character == ':' || character == '-';
}

[[nodiscard]] bool matches_documented_request_id_grammar(const std::string_view value) noexcept {
  constexpr std::size_t kDocumentedMaximumCharacterCount = 64;
  return !value.empty() && value.size() <= kDocumentedMaximumCharacterCount &&
         std::ranges::all_of(value, is_documented_request_id_character);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  const char* const characters = size == 0 ? "" : reinterpret_cast<const char*>(data);
  const std::string_view encoded_request_id = materialize_seed({characters, size});
  const bool expected_to_be_valid = matches_documented_request_id_grammar(encoded_request_id);
  bool accepted = false;

  try {
    const protocol::RequestId request_id = protocol::decode_request_id(encoded_request_id);
    accepted = true;
    if (request_id.value() != encoded_request_id) {
      std::abort();
    }
  } catch (const protocol::ProtocolEncodingError& error) {
    if (error.error_code() != protocol::ProtocolEncodingErrorCode::kRequestIdInvalid) {
      std::abort();
    }
  } catch (...) {
    std::abort();
  }
  if (accepted != expected_to_be_valid) {
    std::abort();
  }
  return 0;
}
