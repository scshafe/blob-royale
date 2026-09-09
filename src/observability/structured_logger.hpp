#ifndef BLOB_ROYALE_OBSERVABILITY_STRUCTURED_LOGGER_HPP
#define BLOB_ROYALE_OBSERVABILITY_STRUCTURED_LOGGER_HPP

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <optional>
#include <string_view>

namespace blob_royale::observability {

enum class LogSeverity { kDebug, kInfo, kWarning, kError };

[[nodiscard]] constexpr std::string_view log_severity_name(const LogSeverity severity) noexcept {
  switch (severity) {
  case LogSeverity::kDebug:
    return "debug";
  case LogSeverity::kInfo:
    return "info";
  case LogSeverity::kWarning:
    return "warning";
  case LogSeverity::kError:
    return "error";
  }
  return "error";
}

// One synchronous structured event. String views are consumed by write() before it returns.
struct StructuredLogEvent final {
  LogSeverity severity;
  std::string_view event;
  std::optional<std::string_view> lifecycle_state = std::nullopt;
  std::optional<std::string_view> request_id = std::nullopt;
  std::optional<std::string_view> connection_id = std::nullopt;
  // The room a line is about, `1..N`, on every room-scoped line: a runtime's counters, a session,
  // a bot, a phase change. Absent on process-scoped lines.
  std::optional<std::uint64_t> lobby_id = std::nullopt;
  std::optional<std::uint64_t> tick_sequence = std::nullopt;
  std::optional<unsigned int> http_status = std::nullopt;
  std::optional<std::uint16_t> close_code = std::nullopt;
  std::optional<std::string_view> error_code = std::nullopt;
  std::optional<std::string_view> context = std::nullopt;
  std::optional<std::string_view> detail = std::nullopt;
};

// canonical: structured_logger -- bounded, atomic process JSON-lines emission.
// The concrete sink and clock are injected so production has one owner and tests remain exact.
class StructuredLogger final {
public:
  using Clock = std::chrono::system_clock;
  using TimeSource = Clock::time_point (*)();

  explicit StructuredLogger(std::ostream& output, TimeSource time_source = &Clock::now) noexcept;

  StructuredLogger(const StructuredLogger&) = delete;
  StructuredLogger(StructuredLogger&&) = delete;
  StructuredLogger& operator=(const StructuredLogger&) = delete;
  StructuredLogger& operator=(StructuredLogger&&) = delete;
  ~StructuredLogger() = default;

  // Emits one complete JSON line. Logging is best-effort and never replaces the originating error.
  void write(const StructuredLogEvent& event) noexcept;

private:
  std::ostream& output_;
  TimeSource time_source_;
  std::mutex output_mutex_;
};

} // namespace blob_royale::observability

#endif
