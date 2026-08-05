#ifndef BLOB_ROYALE_TESTS_INTEGRATION_SERVER_EXIT_RESULT_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_SERVER_EXIT_RESULT_HPP

#include <filesystem>
#include <optional>
#include <sys/types.h>

namespace blob_royale::integration_test {

// Carries the exact waitpid observation from the persistent fixture supervisor to cleanup.
class ServerExitResult final {
public:
  [[nodiscard]] static ServerExitResult exited(pid_t server_process_id, int exit_code,
                                               bool forced_shutdown);
  [[nodiscard]] static ServerExitResult signaled(pid_t server_process_id, int signal_number,
                                                 bool forced_shutdown);
  [[nodiscard]] static ServerExitResult load(const std::filesystem::path& fixture_directory);

  void save(const std::filesystem::path& fixture_directory) const;

  [[nodiscard]] pid_t server_process_id() const noexcept { return server_process_id_; }
  [[nodiscard]] std::optional<int> exit_code() const noexcept { return exit_code_; }
  [[nodiscard]] std::optional<int> signal_number() const noexcept { return signal_number_; }
  [[nodiscard]] bool forced_shutdown() const noexcept { return forced_shutdown_; }

private:
  ServerExitResult(pid_t server_process_id, std::optional<int> exit_code,
                   std::optional<int> signal_number, bool forced_shutdown) noexcept;

  pid_t server_process_id_;
  std::optional<int> exit_code_;
  std::optional<int> signal_number_;
  bool forced_shutdown_;
};

} // namespace blob_royale::integration_test

#endif
