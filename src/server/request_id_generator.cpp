#include "request_id_generator.hpp"

#include "game_server_error.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <utility>

namespace blob_royale::server {
namespace {

[[nodiscard]] std::uint64_t random_uint64() {
  std::random_device entropy;
  std::uint64_t value = 0;
  constexpr std::size_t kBitsPerByte = 8;
  for (std::size_t byte_index = 0; byte_index < sizeof(value); ++byte_index) {
    value = (value << kBitsPerByte) | (static_cast<std::uint64_t>(entropy()) & std::uint64_t{0xFF});
  }
  return value;
}

void append_hex(std::string& output, const std::uint64_t value) {
  std::array<char, 16> encoded{};
  const auto [end, error] =
      std::to_chars(encoded.data(), encoded.data() + encoded.size(), value, 16);
  if (error != std::errc{}) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "request_id.generation",
                          "hexadecimal conversion failed"};
  }
  output.append(encoded.data(), end);
}

} // namespace

RequestIdGenerator::RequestIdGenerator()
    : process_nonce_(random_uint64()), sequence_(random_uint64()) {}

protocol::RequestId RequestIdGenerator::next() {
  if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "request_id.sequence",
                          "opaque request-ID sequence exhausted"};
  }
  ++sequence_;
  std::string value{"br-"};
  value.reserve(35);
  append_hex(value, process_nonce_);
  value.push_back('-');
  append_hex(value, sequence_);
  return protocol::RequestId::create(std::move(value));
}

} // namespace blob_royale::server
