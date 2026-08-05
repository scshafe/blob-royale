#include "protocol_test_fixture.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "request_id.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::test_fixture;

TEST_CASE("RequestId accepts every character in the protocol v1 grammar",
          "[unit][protocol][request-id]") {
  constexpr std::string_view kEveryCharacterClass = "Az09._:-";
  const protocol::RequestId request_id = protocol::decode_request_id(kEveryCharacterClass);

  CHECK(request_id.value() == kEveryCharacterClass);
}

TEST_CASE("RequestId accepts the exact maximum character count", "[unit][protocol][request-id]") {
  const std::string maximum_request_id(protocol::kRequestIdMaximumCharacterCount, 'a');
  const protocol::RequestId request_id = protocol::decode_request_id(maximum_request_id);

  CHECK(request_id.value() == maximum_request_id);
}

TEST_CASE("RequestId rejects an empty value with a typed error", "[unit][protocol][request-id]") {
  fixture::require_protocol_error_code([] { static_cast<void>(protocol::decode_request_id("")); },
                                       protocol::ProtocolEncodingErrorCode::kRequestIdInvalid);
}

TEST_CASE("RequestId rejects a value above the maximum character count",
          "[unit][protocol][request-id]") {
  const std::string oversized_request_id(protocol::kRequestIdMaximumCharacterCount + 1, 'a');

  fixture::require_protocol_error_code(
      [&oversized_request_id] {
        static_cast<void>(protocol::decode_request_id(oversized_request_id));
      },
      protocol::ProtocolEncodingErrorCode::kRequestIdInvalid);
}

TEST_CASE("RequestId rejects every character outside its safe ASCII grammar",
          "[unit][protocol][request-id]") {
  for (const std::string_view invalid_request_id :
       {"contains space", "slash/value", "line\nfeed", "utf8-\xC3\xA9"}) {
    fixture::require_protocol_error_code(
        [invalid_request_id] {
          static_cast<void>(protocol::decode_request_id(invalid_request_id));
        },
        protocol::ProtocolEncodingErrorCode::kRequestIdInvalid);
  }
}
