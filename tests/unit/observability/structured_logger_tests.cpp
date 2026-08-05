#include "structured_logger.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <latch>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace blob_royale::observability {
namespace {

[[nodiscard]] StructuredLogger::Clock::time_point fixed_timestamp() {
  return StructuredLogger::Clock::time_point{std::chrono::milliseconds{1'786'147'445'123LL}};
}

[[nodiscard]] StructuredLogger::Clock::time_point throwing_timestamp() {
  throw std::runtime_error{"expected clock failure"};
}

TEST_CASE("structured logger emits one deterministic JSON line", "[unit][observability]") {
  std::ostringstream output;
  StructuredLogger logger{output, &fixed_timestamp};

  logger.write(StructuredLogEvent{.severity = LogSeverity::kError,
                                  .event = "server.failed",
                                  .lifecycle_state = "failed",
                                  .request_id = "request-7",
                                  .connection_id = "request-7",
                                  .tick_sequence = 42,
                                  .http_status = 503,
                                  .close_code = 1011,
                                  .error_code = "SERVER.FAILURE",
                                  .context = "test",
                                  .detail = "quote=\" newline=\n byte=\xFF"});

  CHECK(
      output.str() ==
      R"({"timestamp_utc":"2026-08-08T00:04:05.123Z","severity":"error","event":"server.failed","lifecycle_state":"failed","request_id":"request-7","connection_id":"request-7","tick_sequence":42,"http_status":503,"close_code":1011,"error_code":"SERVER.FAILURE","context":"test","detail":"quote=\" newline=\n byte=\u00ff"})"
      "\n");
}

TEST_CASE("structured logger explicitly marks bounded field truncation", "[unit][observability]") {
  std::ostringstream output;
  StructuredLogger logger{output, &fixed_timestamp};
  const std::string oversized_detail(4'097, 'x');

  logger.write(StructuredLogEvent{
      .severity = LogSeverity::kWarning, .event = "test.truncated", .detail = oversized_detail});

  CHECK(output.str().find(R"("truncated":true)") != std::string::npos);
  CHECK(output.str().size() < 4'300);
  CHECK(output.str().ends_with("}\n"));
}

TEST_CASE("structured logger emits complete atomic lines across concurrent writers",
          "[unit][observability][concurrency]") {
  constexpr std::size_t kWriterCount = 8;
  constexpr std::size_t kEventsPerWriter = 32;
  std::ostringstream output;
  StructuredLogger logger{output, &fixed_timestamp};
  std::latch writers_ready{static_cast<std::ptrdiff_t>(kWriterCount)};
  std::vector<std::jthread> writers;
  writers.reserve(kWriterCount);

  for (std::size_t writer_index = 0; writer_index < kWriterCount; ++writer_index) {
    writers.emplace_back([&logger, &writers_ready, writer_index] {
      writers_ready.arrive_and_wait();
      for (std::size_t event_index = 0; event_index < kEventsPerWriter; ++event_index) {
        const std::string detail =
            "writer-" + std::to_string(writer_index) + "-event-" + std::to_string(event_index);
        logger.write(
            {.severity = LogSeverity::kInfo, .event = "test.concurrent", .detail = detail});
      }
    });
  }
  writers.clear();

  std::istringstream emitted_lines{output.str()};
  std::string line;
  std::size_t line_count = 0;
  while (std::getline(emitted_lines, line)) {
    ++line_count;
    CHECK(line.starts_with(R"({"timestamp_utc":"2026-08-08T00:04:05.123Z")"));
    CHECK(line.find(R"("event":"test.concurrent")") != std::string::npos);
    CHECK(line.find(R"("detail":"writer-)") != std::string::npos);
    CHECK(line.ends_with('}'));
  }
  CHECK(line_count == kWriterCount * kEventsPerWriter);
}

TEST_CASE("structured logger emits its bounded fallback when event encoding fails",
          "[unit][observability][failure]") {
  std::ostringstream output;
  StructuredLogger logger{output, &throwing_timestamp};

  logger.write({.severity = LogSeverity::kInfo, .event = "test.unencodable"});

  CHECK(output.str() ==
        R"({"timestamp_utc":"unavailable","severity":"error","event":"observability.write_failed"})"
        "\n");
}

} // namespace
} // namespace blob_royale::observability
