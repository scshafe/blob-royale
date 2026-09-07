#include "bounded_json_serialization.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <boost/json/serializer.hpp>
#include <boost/json/string_view.hpp>
#include <boost/json/value.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::protocol {

namespace json = boost::json;

json::value encode_json_number(const double value) {
  if (std::trunc(value) == value && std::abs(value) <= static_cast<double>(kMaximumSafeInteger)) {
    return json::value(static_cast<std::int64_t>(value));
  }
  return json::value(value);
}

void validate_output_byte_limit(const std::size_t output_byte_limit,
                                const std::size_t protocol_maximum_byte_count,
                                const std::string_view context) {
  if (output_byte_limit == 0 || output_byte_limit > protocol_maximum_byte_count) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kOutputByteLimitInvalid, std::string{context},
        "output byte limit must be positive and no greater than the protocol maximum"};
  }
}

std::string serialize_bounded_json(const json::value& document, const std::size_t output_byte_limit,
                                   const std::size_t protocol_maximum_byte_count,
                                   const std::string_view context) {
  validate_output_byte_limit(output_byte_limit, protocol_maximum_byte_count, context);

  json::serializer serializer;
  serializer.reset(&document);
  std::string output;
  output.reserve(std::min<std::size_t>(output_byte_limit, 16'384));
  std::array<char, 4'096> buffer{};
  while (!serializer.done()) {
    const std::size_t remaining_byte_count = output_byte_limit - output.size();
    const std::size_t next_read_byte_count =
        std::min(buffer.size(), remaining_byte_count + std::size_t{1});
    const json::string_view chunk = serializer.read(buffer.data(), next_read_byte_count);
    if (chunk.size() > remaining_byte_count) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kEncodedPayloadTooLarge,
                                  std::string{context},
                                  "complete encoded JSON exceeds the configured byte limit"};
    }
    output.append(chunk.data(), chunk.size());
  }
  return output;
}

} // namespace blob_royale::protocol
