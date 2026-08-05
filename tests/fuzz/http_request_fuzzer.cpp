#include "http_header_preflight.hpp"
#include "server_limits.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

namespace http = boost::beast::http;
using blob_royale::server::HttpHeaderPreflight;
using blob_royale::server::HttpHeaderPreflightResult;
using blob_royale::server::HttpHeaderPreflightStatus;
using blob_royale::server::ServerLimits;

[[nodiscard]] std::string materialize_seed(const std::string_view encoded) {
  constexpr std::string_view kCrLfSeedPrefix = "CRLF:";
  if (!encoded.starts_with(kCrLfSeedPrefix)) {
    return std::string{encoded};
  }

  std::string result;
  result.reserve(encoded.size() * 2U);
  for (const char byte : encoded.substr(kCrLfSeedPrefix.size())) {
    if (byte == '\n') {
      result.append("\r\n");
    } else {
      result.push_back(byte);
    }
  }
  return result;
}

void verify_preflight_result(const std::string_view bytes, const HttpHeaderPreflightResult result) {
  if (result.header_byte_count > bytes.size()) {
    std::abort();
  }
  if (result.status != HttpHeaderPreflightStatus::kReady) {
    return;
  }
  if (result.header_byte_count < 4 || bytes.substr(result.header_byte_count - 4, 4) != "\r\n\r\n") {
    std::abort();
  }

  http::request_parser<http::string_body> parser;
  parser.header_limit(ServerLimits::kHeaderSectionMaximumByteCount);
  parser.body_limit(ServerLimits::kRequestBodyMaximumByteCount);
  parser.skip(true);
  boost::system::error_code parse_error;
  static_cast<void>(
      parser.put(boost::asio::buffer(bytes.data(), result.header_byte_count), parse_error));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  // Textual seed files cannot portably retain CRLF through patch tooling. A seed-only prefix
  // expands line feeds; every other input reaches the raw parser byte-for-byte.
  const std::string materialized =
      materialize_seed(std::string_view{reinterpret_cast<const char*>(data), size});
  const std::string_view bytes{materialized};
  verify_preflight_result(bytes, HttpHeaderPreflight::inspect(bytes));

  if (!bytes.empty()) {
    const std::size_t split = static_cast<std::size_t>(data[0]) % bytes.size();
    const std::string_view prefix = bytes.substr(0, split);
    verify_preflight_result(prefix, HttpHeaderPreflight::inspect(prefix));
  }
  return 0;
}
