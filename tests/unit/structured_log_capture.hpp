#ifndef BLOB_ROYALE_TESTS_UNIT_STRUCTURED_LOG_CAPTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_STRUCTURED_LOG_CAPTURE_HPP

#include "structured_logger.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::test_support {

struct CapturedStructuredLogEvent final {
  std::string severity;
  std::string event;
  std::optional<std::string> lifecycle_state;
  std::optional<std::string> request_id;
  std::optional<std::string> connection_id;
  std::optional<std::uint64_t> close_code;
};

// Parses the exact JSON-lines emitted by StructuredLogger so lifecycle tests assert records and
// field values instead of accepting coincidental substrings.
class StructuredLogCapture final {
public:
  StructuredLogCapture() : logger(output_) {}

  StructuredLogCapture(const StructuredLogCapture&) = delete;
  StructuredLogCapture(StructuredLogCapture&&) = delete;
  StructuredLogCapture& operator=(const StructuredLogCapture&) = delete;
  StructuredLogCapture& operator=(StructuredLogCapture&&) = delete;
  ~StructuredLogCapture() = default;

  [[nodiscard]] std::vector<CapturedStructuredLogEvent> events() const {
    std::vector<CapturedStructuredLogEvent> result;
    std::istringstream lines{output_.str()};
    std::string line;
    while (std::getline(lines, line)) {
      if (line.empty()) {
        throw std::runtime_error{"structured log capture contains an empty line"};
      }
      const boost::json::value document = boost::json::parse(line);
      if (!document.is_object()) {
        throw std::runtime_error{"structured log line is not a JSON object"};
      }
      const boost::json::object& object = document.as_object();
      static_cast<void>(required_string(object, "timestamp_utc"));
      result.push_back({.severity = required_string(object, "severity"),
                        .event = required_string(object, "event"),
                        .lifecycle_state = optional_string(object, "lifecycle_state"),
                        .request_id = optional_string(object, "request_id"),
                        .connection_id = optional_string(object, "connection_id"),
                        .close_code = optional_unsigned(object, "close_code")});
    }
    return result;
  }

  [[nodiscard]] bool contains_event(const std::string_view event) const {
    const std::vector<CapturedStructuredLogEvent> captured = events();
    return std::ranges::any_of(captured, [event](const CapturedStructuredLogEvent& record) {
      return record.event == event;
    });
  }

  [[nodiscard]] std::vector<std::pair<std::string, std::string>> lifecycle_sequence() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const CapturedStructuredLogEvent& record : events()) {
      if (record.lifecycle_state.has_value()) {
        result.emplace_back(record.event, *record.lifecycle_state);
      }
    }
    return result;
  }

private:
  [[nodiscard]] static std::string required_string(const boost::json::object& object,
                                                   const std::string_view name) {
    const boost::json::value* const value = object.if_contains(name);
    if (value == nullptr || !value->is_string()) {
      throw std::runtime_error{
          std::string{"structured log field is missing or not a string: "}.append(name)};
    }
    const boost::json::string& text = value->as_string();
    return std::string{text.data(), text.size()};
  }

  [[nodiscard]] static std::optional<std::string> optional_string(const boost::json::object& object,
                                                                  const std::string_view name) {
    const boost::json::value* const value = object.if_contains(name);
    if (value == nullptr) {
      return std::nullopt;
    }
    if (!value->is_string()) {
      throw std::runtime_error{std::string{"structured log field is not a string: "}.append(name)};
    }
    const boost::json::string& text = value->as_string();
    return std::string{text.data(), text.size()};
  }

  [[nodiscard]] static std::optional<std::uint64_t>
  optional_unsigned(const boost::json::object& object, const std::string_view name) {
    const boost::json::value* const value = object.if_contains(name);
    if (value == nullptr) {
      return std::nullopt;
    }
    if (value->is_uint64()) {
      return value->as_uint64();
    }
    if (value->is_int64() && value->as_int64() >= 0) {
      return static_cast<std::uint64_t>(value->as_int64());
    }
    throw std::runtime_error{std::string{"structured log field is not unsigned: "}.append(name)};
  }

  std::ostringstream output_;

public:
  observability::StructuredLogger logger;
};

} // namespace blob_royale::test_support

#endif
