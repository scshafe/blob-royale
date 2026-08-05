#include "http_header_preflight.hpp"

#include "server_limits.hpp"

#include <cstddef>
#include <string_view>

namespace blob_royale::server {
namespace {

inline constexpr std::string_view kHeaderTerminator = "\r\n\r\n";
inline constexpr std::string_view kLineTerminator = "\r\n";

} // namespace

HttpHeaderPreflightResult
HttpHeaderPreflight::inspect(const std::string_view buffered_bytes) noexcept {
  const std::size_t terminator_offset = buffered_bytes.find(kHeaderTerminator);
  const bool complete = terminator_offset != std::string_view::npos;
  const std::size_t header_byte_count = complete ? terminator_offset + kHeaderTerminator.size() : 0;
  const std::size_t inspected_byte_count = complete ? header_byte_count : buffered_bytes.size();

  if ((complete && header_byte_count > ServerLimits::kHeaderSectionMaximumByteCount) ||
      (!complete && buffered_bytes.size() >= ServerLimits::kHeaderSectionMaximumByteCount)) {
    return {HttpHeaderPreflightStatus::kHeaderTooLarge, header_byte_count};
  }

  std::size_t line_end = buffered_bytes.find(kLineTerminator);
  while (line_end != std::string_view::npos &&
         line_end + kLineTerminator.size() < inspected_byte_count) {
    const char next_character = buffered_bytes[line_end + kLineTerminator.size()];
    if (next_character == ' ' || next_character == '\t') {
      return {HttpHeaderPreflightStatus::kObsoleteLineFolding, header_byte_count};
    }
    line_end = buffered_bytes.find(kLineTerminator, line_end + kLineTerminator.size());
  }

  if (complete) {
    return {HttpHeaderPreflightStatus::kReady, header_byte_count};
  }
  return {HttpHeaderPreflightStatus::kNeedsMoreData, 0};
}

} // namespace blob_royale::server
