#include "structured_server_log_observer.hpp"

#include "fixture_text_file.hpp"
#include "integration_test_error.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace blob_royale::integration_test {
namespace {

using namespace std::chrono_literals;

constexpr std::size_t kMaximumServerLogByteCount = 1'048'576;
constexpr auto kObservationPollInterval = 10ms;

[[nodiscard]] bool matches_string_field(const boost::json::object& event,
                                        const std::string_view name,
                                        const std::string_view expected) {
  const boost::json::value* const value = event.if_contains(name);
  return value != nullptr && value->is_string() && value->as_string() == expected;
}

[[nodiscard]] bool matches_close_code(const boost::json::object& event,
                                      const std::uint16_t expected) {
  const boost::json::value* const value = event.if_contains("close_code");
  if (value == nullptr) {
    return false;
  }
  if (value->is_uint64()) {
    return value->as_uint64() == expected;
  }
  return value->is_int64() && value->as_int64() == expected;
}

} // namespace

StructuredServerLogObserver::StructuredServerLogObserver(std::filesystem::path server_log_path)
    : server_log_path_(std::move(server_log_path)) {
  if (server_log_path_.empty()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "server_log_observer.construct",
                               "server log path must not be empty"};
  }
}

void StructuredServerLogObserver::require_websocket_close(
    const std::string_view request_id, const std::uint16_t close_code,
    const std::chrono::steady_clock::duration timeout) const {
  if (request_id.empty() || timeout <= std::chrono::steady_clock::duration::zero() ||
      timeout > std::chrono::seconds{30}) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "server_log_observer.require_websocket_close",
                               "close observation input is invalid"};
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  do {
    if (contains_websocket_close(request_id, close_code)) {
      return;
    }
    std::this_thread::sleep_for(kObservationPollInterval);
  } while (std::chrono::steady_clock::now() < deadline);

  throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                             "server_log_observer.require_websocket_close",
                             "server did not record the correlated WebSocket close code"};
}

bool StructuredServerLogObserver::contains_websocket_close(const std::string_view request_id,
                                                           const std::uint16_t close_code) const {
  const std::string server_log = read_bounded_fixture_text_file(
      server_log_path_, kMaximumServerLogByteCount, "server_log_observer.read");
  std::size_t line_offset = 0;
  while (line_offset < server_log.size()) {
    const std::size_t line_end = server_log.find('\n', line_offset);
    if (line_end == std::string::npos) {
      break;
    }
    const std::string_view encoded_line{server_log.data() + line_offset, line_end - line_offset};
    try {
      const boost::json::value document = boost::json::parse(encoded_line);
      if (document.is_object()) {
        const boost::json::object& event = document.as_object();
        if (matches_string_field(event, "event", "websocket.closed") &&
            matches_string_field(event, "request_id", request_id) &&
            matches_string_field(event, "connection_id", request_id) &&
            matches_close_code(event, close_code)) {
          return true;
        }
      }
    } catch (const std::exception&) {
      throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                                 "server_log_observer.parse",
                                 "server emitted a complete non-JSON log line"};
    }
    line_offset = line_end + 1;
  }
  return false;
}

} // namespace blob_royale::integration_test
