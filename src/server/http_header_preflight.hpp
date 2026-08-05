#ifndef BLOB_ROYALE_SERVER_HTTP_HEADER_PREFLIGHT_HPP
#define BLOB_ROYALE_SERVER_HTTP_HEADER_PREFLIGHT_HPP

#include <cstddef>
#include <string_view>

namespace blob_royale::server {

enum class HttpHeaderPreflightStatus {
  kNeedsMoreData,
  kReady,
  kObsoleteLineFolding,
  kHeaderTooLarge,
};

struct HttpHeaderPreflightResult final {
  HttpHeaderPreflightStatus status;
  std::size_t header_byte_count;
};

// canonical: http_header_preflight -- bounded raw-wire validation before Beast normalization.
// It inspects only the first buffered header section, leaving later pipelined bytes untouched.
class HttpHeaderPreflight final {
public:
  [[nodiscard]] static HttpHeaderPreflightResult inspect(std::string_view buffered_bytes) noexcept;
};

} // namespace blob_royale::server

#endif
