#ifndef BLOB_ROYALE_TESTS_INTEGRATION_STRUCTURED_SERVER_LOG_OBSERVER_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_STRUCTURED_SERVER_LOG_OBSERVER_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace blob_royale::integration_test {

// Reads the process JSON-lines log through one bounded, retry-aware observation boundary.
class StructuredServerLogObserver final {
public:
  explicit StructuredServerLogObserver(std::filesystem::path server_log_path);

  StructuredServerLogObserver(const StructuredServerLogObserver&) = default;
  StructuredServerLogObserver(StructuredServerLogObserver&&) noexcept = default;
  StructuredServerLogObserver& operator=(const StructuredServerLogObserver&) = default;
  StructuredServerLogObserver& operator=(StructuredServerLogObserver&&) noexcept = default;
  ~StructuredServerLogObserver() = default;

  // Waits for the exact correlated close record or fails at the caller's bounded deadline.
  void require_websocket_close(std::string_view request_id, std::uint16_t close_code,
                               std::chrono::steady_clock::duration timeout) const;

private:
  [[nodiscard]] bool contains_websocket_close(std::string_view request_id,
                                              std::uint16_t close_code) const;

  std::filesystem::path server_log_path_;
};

} // namespace blob_royale::integration_test

#endif
