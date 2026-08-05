#include "http_header_preflight.hpp"
#include "server_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

namespace server = blob_royale::server;

TEST_CASE("HttpHeaderPreflight rejects obsolete folding in every sensitive header family",
          "[unit][server][http][preflight]") {
  constexpr std::array<std::string_view, 7> folded_fields{
      "Host: localhost\r\n continuation",
      "Origin: https://game.example.test\r\n\tcontinued",
      "Content-Length: 0\r\n continued",
      "Transfer-Encoding: chunked\r\n\tcontinued",
      "Connection: Upgrade\r\n continued",
      "Sec-WebSocket-Protocol: blob-royale.snapshot.v1\r\n\tcontinued",
      "X-Unrelated: bounded\r\n continuation",
  };

  for (const std::string_view folded_field : folded_fields) {
    DYNAMIC_SECTION(folded_field) {
      const std::string raw =
          std::string{"GET /api/v1/config HTTP/1.1\r\n"}.append(folded_field).append("\r\n\r\n");
      CHECK(server::HttpHeaderPreflight::inspect(raw).status ==
            server::HttpHeaderPreflightStatus::kObsoleteLineFolding);
    }
  }
}

TEST_CASE("HttpHeaderPreflight detects folding split across raw reads",
          "[unit][server][http][preflight]") {
  const std::string partial = "GET /api/v1/config HTTP/1.1\r\nHost: localhost\r\n";
  CHECK(server::HttpHeaderPreflight::inspect(partial).status ==
        server::HttpHeaderPreflightStatus::kNeedsMoreData);
  CHECK(server::HttpHeaderPreflight::inspect(partial + "\tcontinued\r\n\r\n").status ==
        server::HttpHeaderPreflightStatus::kObsoleteLineFolding);
}

TEST_CASE("HttpHeaderPreflight preserves the next pipelined header section",
          "[unit][server][http][preflight][pipeline]") {
  const std::string first = "GET /api/v1/health/live HTTP/1.1\r\nHost: localhost\r\n\r\n";
  const std::string second =
      "GET /api/v1/config HTTP/1.1\r\nHost: localhost\r\nX-Test: one\r\n two\r\n\r\n";
  const std::string pipeline = first + second;

  const server::HttpHeaderPreflightResult first_result =
      server::HttpHeaderPreflight::inspect(pipeline);
  REQUIRE(first_result.status == server::HttpHeaderPreflightStatus::kReady);
  REQUIRE(first_result.header_byte_count == first.size());

  const std::string_view remaining{pipeline.data() + first_result.header_byte_count,
                                   pipeline.size() - first_result.header_byte_count};
  CHECK(server::HttpHeaderPreflight::inspect(remaining).status ==
        server::HttpHeaderPreflightStatus::kObsoleteLineFolding);
}

TEST_CASE("HttpHeaderPreflight shares the normative raw header byte bound",
          "[unit][server][http][preflight][bounds]") {
  const std::string incomplete(server::ServerLimits::kHeaderSectionMaximumByteCount, 'x');
  CHECK(server::HttpHeaderPreflight::inspect(incomplete).status ==
        server::HttpHeaderPreflightStatus::kHeaderTooLarge);

  const std::string valid = "GET /api/v1/config HTTP/1.1\r\nHost: localhost\r\n\r\n";
  const server::HttpHeaderPreflightResult result = server::HttpHeaderPreflight::inspect(valid);
  CHECK(result.status == server::HttpHeaderPreflightStatus::kReady);
  CHECK(result.header_byte_count == valid.size());
}
