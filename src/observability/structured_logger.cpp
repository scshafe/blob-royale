#include "structured_logger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <ostream>
#include <string>
#include <system_error>

namespace blob_royale::observability {
namespace {

constexpr std::size_t kMaximumLogStringFieldByteCount = 4'096;
constexpr std::string_view kFallbackLogLine =
    R"({"timestamp_utc":"unavailable","severity":"error","event":"observability.write_failed"})"
    "\n";

[[nodiscard]] std::string utc_timestamp(const StructuredLogger::Clock::time_point timestamp) {
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch());
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(milliseconds);
  const auto fractional_milliseconds = milliseconds - seconds;
  const std::time_t encoded_seconds = static_cast<std::time_t>(seconds.count());
  std::tm utc_time{};
  if (gmtime_r(&encoded_seconds, &utc_time) == nullptr) {
    return "unavailable";
  }

  std::array<char, 25> encoded{};
  const int encoded_size = std::snprintf(
      encoded.data(), encoded.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
      utc_time.tm_year + 1900, utc_time.tm_mon + 1, utc_time.tm_mday, utc_time.tm_hour,
      utc_time.tm_min, utc_time.tm_sec, static_cast<long long>(fractional_milliseconds.count()));
  if (encoded_size != static_cast<int>(encoded.size() - 1U)) {
    return "unavailable";
  }
  return std::string(encoded.data(), static_cast<std::size_t>(encoded_size));
}

void append_json_string(std::string& output, const std::string_view value, bool& truncated) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  const std::size_t retained_size = std::min(value.size(), kMaximumLogStringFieldByteCount);
  truncated = truncated || retained_size != value.size();
  output.push_back('"');
  for (std::size_t index = 0; index < retained_size; ++index) {
    const auto character = static_cast<unsigned char>(value[index]);
    switch (character) {
    case '"':
      output.append(R"(\")");
      break;
    case '\\':
      output.append(R"(\\)");
      break;
    case '\b':
      output.append(R"(\b)");
      break;
    case '\f':
      output.append(R"(\f)");
      break;
    case '\n':
      output.append(R"(\n)");
      break;
    case '\r':
      output.append(R"(\r)");
      break;
    case '\t':
      output.append(R"(\t)");
      break;
    default:
      if (character < 0x20U || character >= 0x7fU) {
        output.append("\\u00");
        output.push_back(kHexDigits[(character >> 4U) & 0x0fU]);
        output.push_back(kHexDigits[character & 0x0fU]);
      } else {
        output.push_back(static_cast<char>(character));
      }
      break;
    }
  }
  output.push_back('"');
}

void append_string_field(std::string& output, const std::string_view name,
                         const std::string_view value, bool& truncated) {
  output.append(",\"");
  output.append(name);
  output.append("\":");
  append_json_string(output, value, truncated);
}

template <typename Integer>
void append_integer_field(std::string& output, const std::string_view name, const Integer value) {
  std::array<char, 32> encoded{};
  const auto [end, error] = std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
  if (error != std::errc{}) {
    throw std::system_error(std::make_error_code(error));
  }
  output.append(",\"");
  output.append(name);
  output.append("\":");
  output.append(encoded.data(), end);
}

[[nodiscard]] std::string encode_event(const StructuredLogEvent& event,
                                       const StructuredLogger::Clock::time_point timestamp) {
  bool truncated = false;
  std::string encoded;
  encoded.reserve(512);
  encoded.append("{\"timestamp_utc\":");
  append_json_string(encoded, utc_timestamp(timestamp), truncated);
  append_string_field(encoded, "severity", log_severity_name(event.severity), truncated);
  append_string_field(encoded, "event", event.event, truncated);
  if (event.lifecycle_state.has_value()) {
    append_string_field(encoded, "lifecycle_state", *event.lifecycle_state, truncated);
  }
  if (event.request_id.has_value()) {
    append_string_field(encoded, "request_id", *event.request_id, truncated);
  }
  if (event.connection_id.has_value()) {
    append_string_field(encoded, "connection_id", *event.connection_id, truncated);
  }
  if (event.tick_sequence.has_value()) {
    append_integer_field(encoded, "tick_sequence", *event.tick_sequence);
  }
  if (event.http_status.has_value()) {
    append_integer_field(encoded, "http_status", *event.http_status);
  }
  if (event.close_code.has_value()) {
    append_integer_field(encoded, "close_code", *event.close_code);
  }
  if (event.error_code.has_value()) {
    append_string_field(encoded, "error_code", *event.error_code, truncated);
  }
  if (event.context.has_value()) {
    append_string_field(encoded, "context", *event.context, truncated);
  }
  if (event.detail.has_value()) {
    append_string_field(encoded, "detail", *event.detail, truncated);
  }
  if (truncated) {
    encoded.append(",\"truncated\":true");
  }
  encoded.append("}\n");
  return encoded;
}

} // namespace

StructuredLogger::StructuredLogger(std::ostream& output, const TimeSource time_source) noexcept
    : output_(output), time_source_(time_source == nullptr ? &Clock::now : time_source) {}

void StructuredLogger::write(const StructuredLogEvent& event) noexcept {
  try {
    const std::string encoded = encode_event(event, time_source_());
    std::scoped_lock output_lock{output_mutex_};
    output_.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output_.flush();
  } catch (...) {
    try {
      std::scoped_lock output_lock{output_mutex_};
      output_.write(kFallbackLogLine.data(), static_cast<std::streamsize>(kFallbackLogLine.size()));
      output_.flush();
    } catch (...) {
      // Observability must never hide or replace the originating process failure.
    }
  }
}

} // namespace blob_royale::observability
