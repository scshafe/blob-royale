#ifndef BLOB_ROYALE_TESTS_INTEGRATION_SERVER_FIXTURE_STATE_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_SERVER_FIXTURE_STATE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <sys/types.h>

namespace blob_royale::integration_test {

// canonical: server_fixture_state -- the typed handoff between CTest fixture phases.
class ServerFixtureState final {
public:
  [[nodiscard]] static ServerFixtureState
  create(pid_t supervisor_process_id, pid_t server_process_id, std::uint16_t port,
         std::filesystem::path configuration_path, std::filesystem::path scenario_path,
         std::filesystem::path server_log_path, std::filesystem::path supervisor_log_path);

  [[nodiscard]] static ServerFixtureState load(const std::filesystem::path& fixture_directory);
  void save(const std::filesystem::path& fixture_directory) const;

  [[nodiscard]] pid_t supervisor_process_id() const noexcept { return supervisor_process_id_; }
  [[nodiscard]] pid_t server_process_id() const noexcept { return server_process_id_; }
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] const std::filesystem::path& configuration_path() const& noexcept {
    return configuration_path_;
  }
  [[nodiscard]] const std::filesystem::path& configuration_path() const&& = delete;
  [[nodiscard]] const std::filesystem::path& scenario_path() const& noexcept {
    return scenario_path_;
  }
  [[nodiscard]] const std::filesystem::path& scenario_path() const&& = delete;
  [[nodiscard]] const std::filesystem::path& server_log_path() const& noexcept {
    return server_log_path_;
  }
  [[nodiscard]] const std::filesystem::path& server_log_path() const&& = delete;
  [[nodiscard]] const std::filesystem::path& supervisor_log_path() const& noexcept {
    return supervisor_log_path_;
  }
  [[nodiscard]] const std::filesystem::path& supervisor_log_path() const&& = delete;

  [[nodiscard]] static std::filesystem::path
  state_path(const std::filesystem::path& fixture_directory);
  [[nodiscard]] static std::filesystem::path
  exit_result_path(const std::filesystem::path& fixture_directory);

private:
  ServerFixtureState(pid_t supervisor_process_id, pid_t server_process_id, std::uint16_t port,
                     std::filesystem::path configuration_path, std::filesystem::path scenario_path,
                     std::filesystem::path server_log_path,
                     std::filesystem::path supervisor_log_path) noexcept;

  pid_t supervisor_process_id_;
  pid_t server_process_id_;
  std::uint16_t port_;
  std::filesystem::path configuration_path_;
  std::filesystem::path scenario_path_;
  std::filesystem::path server_log_path_;
  std::filesystem::path supervisor_log_path_;
};

} // namespace blob_royale::integration_test

#endif
