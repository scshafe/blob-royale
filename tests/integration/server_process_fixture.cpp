#include "fixture_text_file.hpp"
#include "integration_test_error.hpp"
#include "loopback_http_client.hpp"
#include "protocol_contract_validation.hpp"
#include "server_exit_result.hpp"
#include "server_fixture_state.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace blob_royale::integration_test {
namespace {

namespace http = boost::beast::http;

using namespace std::chrono_literals;

constexpr std::string_view kContractFixtureDirectoryName = "server-process-fixture";
constexpr std::string_view kBackpressureFixtureDirectoryName = "server-backpressure-fixture";
constexpr std::string_view kSessionFixtureDirectoryName = "server-session-fixture";
constexpr std::string_view kConfigurationFileName = "integration-server.cfg";
constexpr std::string_view kMapDirectoryName = "integration-arena";
constexpr std::string_view kScenarioFileName = "integration-scenario.csv";
constexpr std::string_view kServerLogFileName = "server.log";
constexpr std::string_view kSupervisorLogFileName = "supervisor.log";
constexpr std::string_view kFixtureReadyRequestId = "integration.fixture-ready";
// The forwarded address the readiness probe presents to a fixture that trusts the loopback proxy.
constexpr std::string_view kFixtureProbeAddress = "100.64.0.250";
constexpr auto kSetupDeadline = 10s;
constexpr auto kShutdownDeadline = 10s;
constexpr auto kCleanupResultDeadline = 12s;
constexpr auto kTransportOperationTimeout = 1s;
constexpr std::size_t kMaximumPortAcquisitionAttempts = 8;
constexpr std::size_t kBackpressureFixturePlayerCount = 512;

enum class FixtureOperation {
  kSetup,
  kCleanup,
};

enum class FixtureWorkload {
  kContract,
  kBackpressure,
  // A `royale` match with one bot, for the protocol v3 session contracts. It is the only workload
  // that seats a bot, because the human/bot symmetry is only observable when both are present.
  kSession,
};

class FixtureArguments final {
public:
  [[nodiscard]] static FixtureArguments parse(const int argument_count,
                                              const char* const arguments[]) {
    if (argument_count < 4) {
      throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                 "server_fixture.parse_arguments",
                                 "fixture operation and directory are required"};
    }
    const std::string_view operation_text{arguments[1]};
    const FixtureOperation operation = [&] {
      if (operation_text == "setup") {
        return FixtureOperation::kSetup;
      }
      if (operation_text == "cleanup") {
        return FixtureOperation::kCleanup;
      }
      throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                 "server_fixture.parse_arguments",
                                 "operation must be setup or cleanup"};
    }();

    std::optional<std::filesystem::path> fixture_directory;
    std::optional<std::filesystem::path> server_executable;
    std::optional<FixtureWorkload> requested_workload;
    for (int index = 2; index < argument_count; index += 2) {
      if (index + 1 >= argument_count) {
        throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                   "server_fixture.parse_arguments",
                                   "fixture option is missing its value"};
      }
      const std::string_view option{arguments[index]};
      const std::string_view value{arguments[index + 1]};
      if (value.empty()) {
        throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                   "server_fixture.parse_arguments",
                                   "fixture option value must not be empty"};
      }
      if (option == "--fixture-directory" && !fixture_directory.has_value()) {
        fixture_directory.emplace(value);
      } else if (option == "--server-executable" && !server_executable.has_value()) {
        server_executable.emplace(value);
      } else if (option == "--workload" && !requested_workload.has_value() &&
                 value == "backpressure") {
        requested_workload = FixtureWorkload::kBackpressure;
      } else if (option == "--workload" && !requested_workload.has_value() && value == "session") {
        requested_workload = FixtureWorkload::kSession;
      } else {
        throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                   "server_fixture.parse_arguments",
                                   "fixture option is unknown or duplicated"};
      }
    }

    if (!fixture_directory.has_value() ||
        (operation == FixtureOperation::kSetup && !server_executable.has_value()) ||
        (operation == FixtureOperation::kCleanup &&
         (server_executable.has_value() || requested_workload.has_value()))) {
      throw IntegrationTestError{
          IntegrationTestErrorCode::kArgumentInvalid, "server_fixture.parse_arguments",
          "setup requires an executable; cleanup accepts only the fixture directory"};
    }
    const FixtureWorkload workload = requested_workload.value_or(FixtureWorkload::kContract);
    const std::string_view required_directory_name = [workload] {
      switch (workload) {
      case FixtureWorkload::kBackpressure:
        return kBackpressureFixtureDirectoryName;
      case FixtureWorkload::kSession:
        return kSessionFixtureDirectoryName;
      case FixtureWorkload::kContract:
        break;
      }
      return kContractFixtureDirectoryName;
    }();
    const bool cleanup_directory_is_supported =
        operation == FixtureOperation::kCleanup &&
        (fixture_directory->filename() == kContractFixtureDirectoryName ||
         fixture_directory->filename() == kBackpressureFixtureDirectoryName ||
         fixture_directory->filename() == kSessionFixtureDirectoryName);
    if (!fixture_directory->is_absolute() ||
        (fixture_directory->filename() != required_directory_name &&
         !cleanup_directory_is_supported) ||
        fixture_directory->parent_path().filename() != "integration" ||
        fixture_directory->parent_path() == fixture_directory->root_path()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                 "server_fixture.parse_arguments",
                                 "fixture directory must be an absolute dedicated test path"};
    }
    if (server_executable.has_value() && !server_executable->is_absolute()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                                 "server_fixture.parse_arguments",
                                 "server executable must be an absolute path"};
    }
    const FixtureWorkload resolved_workload = [&] {
      if (fixture_directory->filename() == kBackpressureFixtureDirectoryName) {
        return FixtureWorkload::kBackpressure;
      }
      if (fixture_directory->filename() == kSessionFixtureDirectoryName) {
        return FixtureWorkload::kSession;
      }
      return workload;
    }();
    return FixtureArguments{operation, resolved_workload, std::move(*fixture_directory),
                            std::move(server_executable)};
  }

  [[nodiscard]] FixtureOperation operation() const noexcept { return operation_; }
  [[nodiscard]] FixtureWorkload workload() const noexcept { return workload_; }
  [[nodiscard]] const std::filesystem::path& fixture_directory() const& noexcept {
    return fixture_directory_;
  }
  [[nodiscard]] const std::filesystem::path& fixture_directory() const&& = delete;
  [[nodiscard]] const std::optional<std::filesystem::path>& server_executable() const& noexcept {
    return server_executable_;
  }
  [[nodiscard]] const std::optional<std::filesystem::path>& server_executable() const&& = delete;

private:
  FixtureArguments(FixtureOperation operation, FixtureWorkload workload,
                   std::filesystem::path fixture_directory,
                   std::optional<std::filesystem::path> server_executable) noexcept
      : operation_(operation), workload_(workload),
        fixture_directory_(std::move(fixture_directory)),
        server_executable_(std::move(server_executable)) {}

  FixtureOperation operation_;
  FixtureWorkload workload_;
  std::filesystem::path fixture_directory_;
  std::optional<std::filesystem::path> server_executable_;
};

class FileDescriptor final {
public:
  explicit FileDescriptor(const int descriptor = -1) noexcept : descriptor_(descriptor) {}
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&& other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)) {}
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      close();
      descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
  }
  ~FileDescriptor() noexcept { close(); }

  [[nodiscard]] int get() const noexcept { return descriptor_; }
  [[nodiscard]] int release() noexcept { return std::exchange(descriptor_, -1); }

  void close() noexcept {
    if (descriptor_ >= 0) {
      while (::close(descriptor_) != 0 && errno == EINTR) {
      }
      descriptor_ = -1;
    }
  }

private:
  int descriptor_;
};

class LoopbackPortReservation final {
public:
  LoopbackPortReservation() : descriptor_(::socket(AF_INET, SOCK_STREAM, 0)) {
    if (descriptor_.get() < 0) {
      throw_process_error("server_fixture.reserve_port.socket");
    }
    const int descriptor_flags = ::fcntl(descriptor_.get(), F_GETFD);
    if (descriptor_flags < 0 ||
        ::fcntl(descriptor_.get(), F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
      throw_process_error("server_fixture.reserve_port.close_on_exec");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (::bind(descriptor_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) !=
        0) {
      throw_process_error("server_fixture.reserve_port.bind");
    }
    socklen_t address_size = sizeof(address);
    if (::getsockname(descriptor_.get(), reinterpret_cast<sockaddr*>(&address), &address_size) !=
            0 ||
        address_size != sizeof(address) || address.sin_port == 0) {
      throw_process_error("server_fixture.reserve_port.inspect");
    }
    port_ = ntohs(address.sin_port);
  }

  LoopbackPortReservation(const LoopbackPortReservation&) = delete;
  LoopbackPortReservation(LoopbackPortReservation&&) = delete;
  LoopbackPortReservation& operator=(const LoopbackPortReservation&) = delete;
  LoopbackPortReservation& operator=(LoopbackPortReservation&&) = delete;
  ~LoopbackPortReservation() = default;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

private:
  [[noreturn]] static void throw_process_error(const std::string_view operation) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed, std::string{operation},
                               std::error_code{errno, std::generic_category()}.message()};
  }

  FileDescriptor descriptor_;
  std::uint16_t port_{0};
};

[[nodiscard]] ServerExitResult observed_exit_result(const pid_t server_process_id,
                                                    const int process_status,
                                                    const bool forced_shutdown) {
  if (WIFEXITED(process_status)) {
    return ServerExitResult::exited(server_process_id, WEXITSTATUS(process_status),
                                    forced_shutdown);
  }
  if (WIFSIGNALED(process_status)) {
    return ServerExitResult::signaled(server_process_id, WTERMSIG(process_status), forced_shutdown);
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                             "server_fixture.observe_exit",
                             "waitpid returned a non-terminal process status"};
}

class ManagedServerProcess final {
public:
  explicit ManagedServerProcess(const pid_t process_id) noexcept : process_id_(process_id) {}
  ManagedServerProcess(const ManagedServerProcess&) = delete;
  ManagedServerProcess(ManagedServerProcess&&) = delete;
  ManagedServerProcess& operator=(const ManagedServerProcess&) = delete;
  ManagedServerProcess& operator=(ManagedServerProcess&&) = delete;
  ~ManagedServerProcess() noexcept {
    if (!process_status_.has_value()) {
      static_cast<void>(::kill(-process_id_, SIGKILL));
      int status = 0;
      while (::waitpid(process_id_, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }

  [[nodiscard]] pid_t process_id() const noexcept { return process_id_; }

  [[nodiscard]] bool poll_exited() {
    if (process_status_.has_value()) {
      return true;
    }
    int status = 0;
    const pid_t result = ::waitpid(process_id_, &status, WNOHANG);
    if (result == 0) {
      return false;
    }
    if (result == process_id_) {
      process_status_ = status;
      return true;
    }
    if (result < 0 && errno == EINTR) {
      return false;
    }
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.poll_server",
                               std::error_code{errno, std::generic_category()}.message()};
  }

  [[nodiscard]] ServerExitResult stop_and_wait() {
    if (process_status_.has_value()) {
      return observed_exit_result(process_id_, *process_status_, false);
    }
    if (::kill(-process_id_, SIGTERM) != 0 && errno != ESRCH) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.signal_server",
                                 std::error_code{errno, std::generic_category()}.message()};
    }

    const auto deadline = std::chrono::steady_clock::now() + kShutdownDeadline;
    while (std::chrono::steady_clock::now() < deadline) {
      if (poll_exited()) {
        return observed_exit_result(process_id_, *process_status_, false);
      }
      std::this_thread::sleep_for(25ms);
    }

    if (::kill(-process_id_, SIGKILL) != 0 && errno != ESRCH) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.force_server_stop",
                                 std::error_code{errno, std::generic_category()}.message()};
    }
    int status = 0;
    pid_t wait_result = -1;
    do {
      wait_result = ::waitpid(process_id_, &status, 0);
    } while (wait_result < 0 && errno == EINTR);
    if (wait_result != process_id_) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.reap_forced_server",
                                 std::error_code{errno, std::generic_category()}.message()};
    }
    process_status_ = status;
    return observed_exit_result(process_id_, status, true);
  }

  [[nodiscard]] ServerExitResult terminal_result() const {
    if (!process_status_.has_value()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.observe_server", "server has no terminal result"};
    }
    return observed_exit_result(process_id_, *process_status_, false);
  }

private:
  pid_t process_id_;
  std::optional<int> process_status_;
};

void write_all(const int descriptor, const std::string_view contents,
               const std::string_view operation) {
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const ssize_t result = ::write(descriptor, contents.data() + offset, contents.size() - offset);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result <= 0) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed, std::string{operation},
                                 std::error_code{errno, std::generic_category()}.message()};
    }
    offset += static_cast<std::size_t>(result);
  }
}

// One authored map directory beside the fixture's configuration, on exactly the arena the
// `[world]` scalars publish so `require_map_matches_published_world` accepts the pair. Two spawn
// points, because `SandboxMode::validate_map` requires at least one and a second one proves the
// loader reads more than a single row.
void write_fixture_map(const std::filesystem::path& fixture_directory,
                       const FixtureWorkload workload) {
  const std::filesystem::path map_directory = fixture_directory / kMapDirectoryName;
  std::error_code create_error;
  std::filesystem::create_directory(map_directory, create_error);
  if (create_error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.create_map_directory", create_error.message()};
  }

  std::string map_configuration;
  map_configuration.append("[map]\n");
  map_configuration.append("name=").append(kMapDirectoryName).append("\n");
  map_configuration.append("display_name=Integration Arena\n\n");
  map_configuration.append("[bounds]\n");
  map_configuration.append("width_world_units=100\n");
  map_configuration.append("height_world_units=80\n");
  map_configuration.append("\n[terrain]\nground=solid\n");
  write_fixture_text_file_atomically(map_directory / "map.cfg", map_configuration,
                                     "server_fixture.write_map_configuration");
  write_fixture_text_file_atomically(map_directory / "static_bodies.csv",
                                     "position_x_world_units,position_y_world_units,collision_"
                                     "layer,collision_mask,contact_effect_policy\n",
                                     "server_fixture.write_map_static_bodies");
  // The session workload needs at least `lobby_seat_count` spawn markers for
  // `RoyaleMode::validate_map`, and needs enough of them that two sessions and one bot are all
  // seated at once and never in contact with each other.
  const std::string markers =
      workload == FixtureWorkload::kSession
          ? std::string{"marker_kind,position_x_world_units,position_y_world_units,team_id\n"
                        "spawn,15,20,\n"
                        "spawn,50,20,\n"
                        "spawn,85,20,\n"
                        "spawn,15,60,\n"
                        "spawn,50,60,\n"
                        "spawn,85,60,\n"}
          : std::string{"marker_kind,position_x_world_units,position_y_world_units,team_id\n"
                        "spawn,25,40,\n"
                        "spawn,75,40,\n"};
  write_fixture_text_file_atomically(map_directory / "markers.csv", markers,
                                     "server_fixture.write_map_markers");
}

void write_fixture_inputs(const std::filesystem::path& fixture_directory, const std::uint16_t port,
                          const FixtureWorkload workload) {
  const bool backpressure_workload = workload == FixtureWorkload::kBackpressure;
  const bool session_workload = workload == FixtureWorkload::kSession;
  const std::string authority = std::string{"127.0.0.1:"}.append(std::to_string(port));
  std::string configuration;
  configuration.append("[server]\n");
  configuration.append("bind_address=127.0.0.1\n");
  configuration.append("port=").append(std::to_string(port)).append("\n");
  configuration.append("allowed_hosts=").append(authority).append("\n");
  configuration.append("allowed_origins=http://").append(authority).append("\n");
  // The session workload is the deployed shape: the loopback proxy is trusted, so every connection
  // must present exactly one `X-Forwarded-For` and is accounted to that address rather than to
  // 127.0.0.1. That is what lets one test process open the sessions the room contracts need
  // without sharing one upgrade bucket between them.
  configuration.append(session_workload ? "trusted_proxy_addresses=127.0.0.1\n\n"
                                        : "trusted_proxy_addresses=\n\n");
  configuration.append("[presentation]\n");
  configuration.append("snapshots_per_second=").append(backpressure_workload ? "60\n\n" : "20\n\n");
  configuration.append("[simulation]\n");
  configuration.append("ticks_per_second=400\n");
  // Zero drag keeps this fixture's committed physics identical to the accepted ADR 0003 horizons.
  configuration.append("drag_per_second=0\n\n");
  configuration.append("[world]\n");
  configuration.append("width_world_units=100\n");
  configuration.append("height_world_units=80\n");
  configuration.append("player_radius_world_units=")
      .append(backpressure_workload ? "1\n\n" : "2\n\n");
  configuration.append("[spatial_grid]\n");
  configuration.append("columns=10\n");
  configuration.append("rows=8\n\n");
  // `sandbox`, not `royale`: these tests assert protocol contracts over a seeded roster, and a
  // shrinking zone would eliminate that roster mid-assertion. The map is written beside the
  // configuration so the fixture is self-contained and so the production `MapLoader` is the one
  // that reads it.
  configuration.append("[match]\n");
  configuration.append(session_workload ? "mode=royale\n" : "mode=sandbox\n");
  configuration.append("map=").append(kMapDirectoryName).append("\n");
  configuration.append("maps_directory=").append(fixture_directory.string()).append("\n");
  configuration.append("seed=1\n");
  // The session workload holds the match in `lobby` forever, and since Step 2 it does so for a
  // simpler reason than the six-player minimum it used to declare: a royale match leaves `lobby`
  // only when every seat is filled and somebody presses Start, and nothing in this fixture presses
  // it. A running royale match would shrink a zone and eliminate the very entities these contracts
  // assert about, and the mode's systems, spawn policy, and command mask are the same in every
  // phase. The six seats are kept because the six spawn markers written above are sized for them.
  configuration.append(session_workload ? "lobby_seat_count=6\n" : "lobby_seat_count=2\n");
  // One bot, so the session contracts can assert that a command from one session moves that
  // session's entity and nothing else -- including an entity nobody on the network drives.
  configuration.append(session_workload ? "bots=wanderer:1\n\n" : "bots=\n\n");
  configuration.append("[movement]\n");
  configuration.append("acceleration_world_units_per_second_squared=400\n");
  configuration.append("normal_top_speed_world_units_per_second=10000\n\n");
  // Required for every mode, this fixture's `sandbox` included: the ability system is shared, so a
  // room whose shield and charge tuning nobody authored is refused before the server ever binds a
  // port. The charge keys are the shipped values; nothing in this fixture sends a `charge`.
  configuration.append("[abilities]\n");
  configuration.append("shield_duration_seconds=0.4\n");
  configuration.append("shield_perfect_window_seconds=0.08\n");
  configuration.append("shield_cooldown_seconds=0.9\n");
  configuration.append("parry_stun_duration_seconds=0.6\n");
  configuration.append("charge_cooldown_seconds=1.2\n");
  configuration.append("charge_speed_fraction=0.75\n");
  configuration.append("charge_safety_envelope_speed=20000\n\n");
  configuration.append("[royale]\n");
  configuration.append("zone_minimum_radius_world_units=10\n");
  configuration.append("zone_shrink_seconds=90\n");
  configuration.append("elimination_grace_seconds=3\n");
  configuration.append("countdown_seconds=5\n");
  configuration.append("restart_delay_seconds=8\n");
  // Every mode section is required even when this fixture runs sandbox or royale.
  configuration.append("\n[king_of_the_hill]\n");
  configuration.append("hill_motion=marker_tour\n");
  configuration.append("hill_speed_minimum=20\n");
  configuration.append("hill_speed_maximum=70\n");
  configuration.append("hill_retarget_minimum_seconds=0.35\n");
  configuration.append("hill_retarget_maximum_seconds=1.2\n");
  configuration.append("hill_radius_world_units=90\n");
  configuration.append("hill_dwell_seconds=12\n");
  configuration.append("hill_travel_seconds=4\n");
  configuration.append("point_interval_seconds=1\n");
  configuration.append("points_to_win=30\n");
  configuration.append("contested_hill_scores=false\n");
  configuration.append("time_limit_seconds=240\n");
  configuration.append("respawn_delay_seconds=2\n");
  configuration.append("countdown_seconds=5\n");
  configuration.append("restart_delay_seconds=8\n");
  configuration.append("\n[race]\n");
  configuration.append("road=road\n");
  configuration.append("checkpoint_radius_world_units=40\n");
  configuration.append("respawn_delay_seconds=2\n");
  configuration.append("finish_window_seconds=20\n");
  configuration.append("time_limit_seconds=240\n");
  configuration.append("countdown_seconds=5\n");
  configuration.append("restart_delay_seconds=8\n");
  // Two rooms for the session workload, so the directory lists more than the room every other
  // route serves and a room target can name one that `/api/v3/lobbies/1/session` does not.
  configuration.append("\n[sandbox]\nrespawn_delay_seconds=2.0\n\n[lobbies]\n");
  configuration.append(session_workload ? "count=2\n" : "count=1\n");
  write_fixture_text_file_atomically(fixture_directory / kConfigurationFileName, configuration,
                                     "server_fixture.write_configuration");

  write_fixture_map(fixture_directory, workload);

  constexpr std::string_view kScenarioHeader =
      "entity_id,position_x_world_units,position_y_world_units,"
      "velocity_x_world_units_per_second,velocity_y_world_units_per_second,"
      "acceleration_x_world_units_per_second_squared,"
      "acceleration_y_world_units_per_second_squared\n";
  std::string scenario{kScenarioHeader};
  if (session_workload) {
    // No seeded entities: every entity in this workload is one a session or a bot asked for, which
    // is what makes "this entity moved" attributable to exactly one command source.
  } else if (!backpressure_workload) {
    scenario.append("1,20,20,1,0,0,0\n").append("2,80,60,-1,0,0,0\n");
  } else {
    scenario.reserve(kScenarioHeader.size() + (kBackpressureFixturePlayerCount * 24));
    for (std::size_t player_index = 0; player_index < kBackpressureFixturePlayerCount;
         ++player_index) {
      const std::size_t entity_id = player_index + 1;
      const std::size_t position_x = 2 + ((player_index % 32) * 3);
      const std::size_t position_y = 2 + ((player_index / 32) * 5);
      scenario.append(std::to_string(entity_id))
          .append(",")
          .append(std::to_string(position_x))
          .append(",")
          .append(std::to_string(position_y))
          .append(",0,0,0,0\n");
    }
  }
  write_fixture_text_file_atomically(fixture_directory / kScenarioFileName, scenario,
                                     "server_fixture.write_scenario");
}

[[nodiscard]] std::array<FileDescriptor, 2> create_status_pipe() {
  std::array<int, 2> raw_descriptors{};
  if (::pipe(raw_descriptors.data()) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.create_status_pipe",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  std::array<FileDescriptor, 2> descriptors = {FileDescriptor{raw_descriptors[0]},
                                               FileDescriptor{raw_descriptors[1]}};
  for (const FileDescriptor& descriptor : descriptors) {
    const int flags = ::fcntl(descriptor.get(), F_GETFD);
    if (flags < 0 || ::fcntl(descriptor.get(), F_SETFD, flags | FD_CLOEXEC) != 0) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.configure_status_pipe",
                                 std::error_code{errno, std::generic_category()}.message()};
    }
  }
  return descriptors;
}

void redirect_supervisor_output(const std::filesystem::path& log_path) {
  const int descriptor = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor < 0 || ::dup2(descriptor, STDOUT_FILENO) < 0 ||
      ::dup2(descriptor, STDERR_FILENO) < 0) {
    if (descriptor >= 0) {
      static_cast<void>(::close(descriptor));
    }
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.redirect_supervisor",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  static_cast<void>(::close(descriptor));
}

[[nodiscard]] sigset_t block_supervisor_signals() {
  sigset_t signal_set{};
  if (::sigemptyset(&signal_set) != 0 || ::sigaddset(&signal_set, SIGTERM) != 0 ||
      ::sigaddset(&signal_set, SIGINT) != 0 ||
      ::sigprocmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.block_signals",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  return signal_set;
}

[[nodiscard]] bool supervisor_stop_is_pending() {
  sigset_t pending_signals{};
  if (::sigpending(&pending_signals) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.poll_supervisor_signal",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  return ::sigismember(&pending_signals, SIGTERM) == 1 ||
         ::sigismember(&pending_signals, SIGINT) == 1;
}

[[nodiscard]] int wait_for_supervisor_stop(const sigset_t& signal_set) {
  int signal_number = 0;
  const int wait_error = ::sigwait(&signal_set, &signal_number);
  if (wait_error != 0 || (signal_number != SIGTERM && signal_number != SIGINT)) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.wait_for_stop",
                               std::error_code{wait_error, std::generic_category()}.message()};
  }
  return signal_number;
}

[[nodiscard]] pid_t launch_server_process(const std::filesystem::path& server_executable,
                                          const std::filesystem::path& fixture_directory,
                                          const sigset_t& blocked_signals,
                                          const FixtureWorkload workload) {
  const std::filesystem::path log_path = fixture_directory / kServerLogFileName;
  const int log_descriptor = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (log_descriptor < 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_fixture.open_server_log",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  FileDescriptor owned_log{log_descriptor};

  const pid_t process_id = ::fork();
  if (process_id < 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.fork_server",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  if (process_id == 0) {
    static_cast<void>(::setpgid(0, 0));
    static_cast<void>(::sigprocmask(SIG_UNBLOCK, &blocked_signals, nullptr));
    if (::dup2(owned_log.get(), STDOUT_FILENO) < 0 || ::dup2(owned_log.get(), STDERR_FILENO) < 0) {
      _exit(126);
    }
    owned_log.close();

    const std::string executable_text = server_executable.string();
    const std::string configuration_text = (fixture_directory / kConfigurationFileName).string();
    const std::string scenario_text = (fixture_directory / kScenarioFileName).string();
    // The session workload seeds nothing and runs two rooms, and a scenario seeds exactly one
    // world, so the server is started without one: `--scenario` with `[lobbies] count=2` is the
    // configuration the loader refuses as a fixture that lies about itself.
    std::array<char*, 6> child_arguments = {
        const_cast<char*>(executable_text.c_str()),    const_cast<char*>("--config"),
        const_cast<char*>(configuration_text.c_str()), const_cast<char*>("--scenario"),
        const_cast<char*>(scenario_text.c_str()),      nullptr,
    };
    if (workload == FixtureWorkload::kSession) {
      child_arguments[3] = nullptr;
    }
    ::execv(executable_text.c_str(), child_arguments.data());
    _exit(127);
  }
  static_cast<void>(::setpgid(process_id, process_id));
  return process_id;
}

void wait_until_ready(ManagedServerProcess& server_process, const std::uint16_t port,
                      const FixtureWorkload workload) {
  LoopbackHttpClient http_client{port, kTransportOperationTimeout};
  // The session workload trusts the loopback proxy, so the probe must say who it is forwarding.
  const std::optional<std::string_view> forwarded_client =
      workload == FixtureWorkload::kSession ? std::optional<std::string_view>{kFixtureProbeAddress}
                                            : std::nullopt;
  const auto deadline = std::chrono::steady_clock::now() + kSetupDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    if (server_process.poll_exited()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.wait_until_ready",
                                 "server exited before readiness"};
    }
    if (supervisor_stop_is_pending()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.wait_until_ready", "fixture setup was cancelled"};
    }
    try {
      const IntegrationHttpResponse response =
          http_client.request(http::verb::get, "/api/v1/health/ready", kFixtureReadyRequestId,
                              std::nullopt, forwarded_client);
      if (response.result() == http::status::ok) {
        validate_readiness_response(response, kFixtureReadyRequestId);
        return;
      }
      if (response.result() != http::status::service_unavailable) {
        throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                                   "server_fixture.wait_until_ready",
                                   "readiness returned an unexpected status"};
      }
      std::this_thread::sleep_for(500ms);
    } catch (const IntegrationTestError& error) {
      if (error.error_code() != IntegrationTestErrorCode::kTransportFailed) {
        throw;
      }
      std::this_thread::sleep_for(100ms);
    }
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kDeadlineExceeded,
                             "server_fixture.wait_until_ready",
                             "server did not become ready before the setup deadline"};
}

[[nodiscard]] boost::json::object integration_error_document(const IntegrationTestError& error) {
  boost::json::object document;
  document.emplace("code", error.code());
  document.emplace("operation", error.operation());
  document.emplace("context", error.context());
  return document;
}

void notify_setup_ready(const int status_descriptor) {
  write_all(status_descriptor, "READY\n", "server_fixture.notify_ready");
}

void notify_setup_error(const int status_descriptor, const IntegrationTestError& error) noexcept {
  try {
    std::string status{"ERROR "};
    status.append(boost::json::serialize(integration_error_document(error)));
    status.push_back('\n');
    write_all(status_descriptor, status, "server_fixture.notify_error");
  } catch (...) {
    // The original setup failure remains authoritative if its best-effort pipe report fails.
  }
}

[[nodiscard]] bool server_log_reports_bind_failure(const std::filesystem::path& fixture_directory) {
  try {
    const std::string contents = read_bounded_fixture_text_file(
        fixture_directory / kServerLogFileName, 65'536, "server_fixture.read_server_log");
    return contents.find("SERVER.LISTENER.BIND_FAILED") != std::string::npos;
  } catch (const IntegrationTestError&) {
    return false;
  }
}

int supervise_server(const std::filesystem::path& server_executable,
                     const std::filesystem::path& fixture_directory, const int status_descriptor,
                     const FixtureWorkload workload) {
  const sigset_t blocked_signals = block_supervisor_signals();
  std::uint16_t port = 0;
  pid_t server_process_id = -1;
  std::unique_ptr<ManagedServerProcess> server_process;
  for (std::size_t attempt = 1; attempt <= kMaximumPortAcquisitionAttempts; ++attempt) {
    {
      LoopbackPortReservation port_reservation;
      port = port_reservation.port();
      write_fixture_inputs(fixture_directory, port, workload);
      server_process_id =
          launch_server_process(server_executable, fixture_directory, blocked_signals, workload);
    }
    auto candidate = std::make_unique<ManagedServerProcess>(server_process_id);
    try {
      wait_until_ready(*candidate, port, workload);
      server_process = std::move(candidate);
      break;
    } catch (const IntegrationTestError&) {
      const bool exited_before_readiness = candidate->poll_exited();
      const ServerExitResult candidate_result =
          exited_before_readiness ? candidate->terminal_result() : candidate->stop_and_wait();
      const bool port_was_lost = exited_before_readiness && candidate_result.exit_code() == 1 &&
                                 server_log_reports_bind_failure(fixture_directory);
      if (!port_was_lost || attempt == kMaximumPortAcquisitionAttempts) {
        throw;
      }
      boost::json::object retry;
      retry.emplace("status", "retrying");
      retry.emplace("operation", "server_fixture.acquire_port");
      retry.emplace("attempt", attempt);
      retry.emplace("reason", "SERVER.LISTENER.BIND_FAILED");
      std::cerr << boost::json::serialize(retry) << '\n';
    }
  }
  if (!server_process) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.acquire_port",
                               "no server process acquired its reserved loopback port"};
  }

  const ServerFixtureState fixture_state = ServerFixtureState::create(
      ::getpid(), server_process_id, port, fixture_directory / kConfigurationFileName,
      fixture_directory / kScenarioFileName, fixture_directory / kServerLogFileName,
      fixture_directory / kSupervisorLogFileName);
  fixture_state.save(fixture_directory);

  try {
    notify_setup_ready(status_descriptor);
    static_cast<void>(::close(status_descriptor));
    static_cast<void>(wait_for_supervisor_stop(blocked_signals));
    const ServerExitResult result = server_process->stop_and_wait();
    result.save(fixture_directory);
    return 0;
  } catch (const IntegrationTestError& error) {
    ServerExitResult result = server_process->poll_exited() ? server_process->terminal_result()
                                                            : server_process->stop_and_wait();
    result.save(fixture_directory);
    notify_setup_error(status_descriptor, error);
    static_cast<void>(::close(status_descriptor));
    return 1;
  }
}

void validate_server_executable(const std::filesystem::path& server_executable) {
  std::error_code status_error;
  const std::filesystem::file_status status =
      std::filesystem::status(server_executable, status_error);
  if (status_error || !std::filesystem::is_regular_file(status) ||
      ::access(server_executable.c_str(), X_OK) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "server_fixture.validate_executable",
                               "server executable is not a runnable regular file"};
  }
}

void prepare_fixture_directory(const std::filesystem::path& fixture_directory) {
  std::error_code exists_error;
  const bool state_exists =
      std::filesystem::exists(ServerFixtureState::state_path(fixture_directory), exists_error);
  if (exists_error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_fixture.inspect_directory", exists_error.message()};
  }
  if (state_exists) {
    const ServerFixtureState stale_state = ServerFixtureState::load(fixture_directory);
    std::error_code result_exists_error;
    const bool result_exists = std::filesystem::exists(
        ServerFixtureState::exit_result_path(fixture_directory), result_exists_error);
    if (result_exists_error) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 "server_fixture.inspect_exit_result",
                                 result_exists_error.message()};
    }
    const bool server_is_active = ::kill(stale_state.server_process_id(), 0) == 0;
    const bool unfinished_supervisor_is_active =
        !result_exists && ::kill(stale_state.supervisor_process_id(), 0) == 0;
    if (server_is_active || unfinished_supervisor_is_active) {
      throw IntegrationTestError{
          IntegrationTestErrorCode::kProcessFailed, "server_fixture.prepare_directory",
          "an earlier fixture process is still active; run the cleanup fixture first"};
    }
  }

  std::error_code remove_error;
  std::filesystem::remove_all(fixture_directory, remove_error);
  if (remove_error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_fixture.reset_directory", remove_error.message()};
  }
  std::error_code create_error;
  if (!std::filesystem::create_directories(fixture_directory, create_error) && create_error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_fixture.create_directory", create_error.message()};
  }
}

[[nodiscard]] std::string read_setup_status(const int status_descriptor,
                                            const pid_t supervisor_process_id) {
  const auto deadline = std::chrono::steady_clock::now() + kSetupDeadline + 2s;
  std::string status;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd descriptor_poll{};
    descriptor_poll.fd = status_descriptor;
    descriptor_poll.events = POLLIN | POLLHUP;
    const int poll_result = ::poll(&descriptor_poll, 1, 100);
    if (poll_result < 0 && errno == EINTR) {
      continue;
    }
    if (poll_result < 0) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.read_setup_status",
                                 std::error_code{errno, std::generic_category()}.message()};
    }
    if (poll_result > 0 && (descriptor_poll.revents & (POLLIN | POLLHUP)) != 0) {
      std::array<char, 4'096> buffer{};
      const ssize_t byte_count = ::read(status_descriptor, buffer.data(), buffer.size());
      if (byte_count < 0 && errno == EINTR) {
        continue;
      }
      if (byte_count < 0) {
        throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                   "server_fixture.read_setup_status",
                                   std::error_code{errno, std::generic_category()}.message()};
      }
      if (byte_count == 0) {
        break;
      }
      status.append(buffer.data(), static_cast<std::size_t>(byte_count));
      if (status.find('\n') != std::string::npos) {
        return status.substr(0, status.find('\n'));
      }
      if (status.size() >= buffer.size()) {
        throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                   "server_fixture.read_setup_status",
                                   "supervisor status exceeded its byte bound"};
      }
    }

    int supervisor_status = 0;
    const pid_t wait_result = ::waitpid(supervisor_process_id, &supervisor_status, WNOHANG);
    if (wait_result == supervisor_process_id) {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.read_setup_status",
                                 "fixture supervisor exited before reporting readiness"};
    }
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kDeadlineExceeded,
                             "server_fixture.read_setup_status",
                             "fixture supervisor did not report before the setup deadline"};
}

void stop_failed_setup(const pid_t supervisor_process_id,
                       const std::filesystem::path& fixture_directory) noexcept {
  static_cast<void>(::kill(supervisor_process_id, SIGTERM));
  // The supervisor owns the server child and gets its full graceful-shutdown window before the
  // setup parent applies the terminal leak-prevention fallback.
  const auto deadline = std::chrono::steady_clock::now() + kShutdownDeadline + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    const pid_t result = ::waitpid(supervisor_process_id, &status, WNOHANG);
    if (result == supervisor_process_id || (result < 0 && errno == ECHILD)) {
      return;
    }
    std::this_thread::sleep_for(25ms);
  }

  try {
    if (std::filesystem::exists(ServerFixtureState::state_path(fixture_directory))) {
      const ServerFixtureState state = ServerFixtureState::load(fixture_directory);
      static_cast<void>(::kill(-state.server_process_id(), SIGKILL));
    }
  } catch (...) {
    // This is the terminal fallback after the owned supervisor exceeded its full shutdown bound.
    // The original setup failure remains visible and the exact supervisor is still reaped below.
  }
  static_cast<void>(::kill(supervisor_process_id, SIGKILL));
  int status = 0;
  while (::waitpid(supervisor_process_id, &status, 0) < 0 && errno == EINTR) {
  }
}

int setup_fixture(const FixtureArguments& arguments) {
  const std::filesystem::path& fixture_directory = arguments.fixture_directory();
  const std::filesystem::path& server_executable = *arguments.server_executable();
  validate_server_executable(server_executable);
  prepare_fixture_directory(fixture_directory);
  auto status_pipe = create_status_pipe();

  const pid_t supervisor_process_id = ::fork();
  if (supervisor_process_id < 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.fork_supervisor",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  if (supervisor_process_id == 0) {
    status_pipe[0].close();
    int result = 1;
    try {
      if (::setsid() < 0) {
        throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                   "server_fixture.create_supervisor_session",
                                   std::error_code{errno, std::generic_category()}.message()};
      }
      redirect_supervisor_output(fixture_directory / kSupervisorLogFileName);
      result = supervise_server(server_executable, fixture_directory, status_pipe[1].get(),
                                arguments.workload());
    } catch (const IntegrationTestError& error) {
      notify_setup_error(status_pipe[1].get(), error);
    } catch (const std::exception& error) {
      const IntegrationTestError wrapped{IntegrationTestErrorCode::kProcessFailed,
                                         "server_fixture.supervisor", error.what()};
      notify_setup_error(status_pipe[1].get(), wrapped);
    }
    _exit(result);
  }

  status_pipe[1].close();
  try {
    const std::string setup_status = read_setup_status(status_pipe[0].get(), supervisor_process_id);
    if (setup_status != "READY") {
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed, "server_fixture.setup",
                                 setup_status};
    }
  } catch (...) {
    stop_failed_setup(supervisor_process_id, fixture_directory);
    throw;
  }

  const ServerFixtureState state = ServerFixtureState::load(fixture_directory);
  boost::json::object success;
  success.emplace("status", "passed");
  success.emplace("operation", "setup");
  success.emplace("supervisor_process_id", state.supervisor_process_id());
  success.emplace("server_process_id", state.server_process_id());
  success.emplace("port", state.port());
  std::cout << boost::json::serialize(success) << '\n';
  return 0;
}

void fallback_stop_orphaned_server(const ServerFixtureState& state) noexcept {
  if (::kill(state.server_process_id(), 0) == 0) {
    static_cast<void>(::kill(-state.server_process_id(), SIGKILL));
  }
}

[[nodiscard]] ServerExitResult
wait_for_exit_result(const ServerFixtureState& state,
                     const std::filesystem::path& fixture_directory) {
  const auto deadline = std::chrono::steady_clock::now() + kCleanupResultDeadline;
  while (std::chrono::steady_clock::now() < deadline) {
    std::error_code exists_error;
    const bool result_exists = std::filesystem::exists(
        ServerFixtureState::exit_result_path(fixture_directory), exists_error);
    if (exists_error) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 "server_fixture.wait_for_exit_result", exists_error.message()};
    }
    if (result_exists) {
      return ServerExitResult::load(fixture_directory);
    }
    if (::kill(state.supervisor_process_id(), 0) != 0 && errno == ESRCH) {
      fallback_stop_orphaned_server(state);
      throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                                 "server_fixture.wait_for_exit_result",
                                 "supervisor exited without publishing the server result"};
    }
    std::this_thread::sleep_for(25ms);
  }
  fallback_stop_orphaned_server(state);
  throw IntegrationTestError{IntegrationTestErrorCode::kDeadlineExceeded,
                             "server_fixture.wait_for_exit_result",
                             "server did not stop before the cleanup deadline"};
}

int cleanup_fixture(const FixtureArguments& arguments) {
  const std::filesystem::path& fixture_directory = arguments.fixture_directory();
  std::error_code exists_error;
  if (!std::filesystem::exists(ServerFixtureState::state_path(fixture_directory), exists_error)) {
    if (exists_error) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 "server_fixture.cleanup", exists_error.message()};
    }
    boost::json::object success;
    success.emplace("status", "passed");
    success.emplace("operation", "cleanup");
    success.emplace("resource_state", "absent");
    std::cout << boost::json::serialize(success) << '\n';
    return 0;
  }

  const ServerFixtureState state = ServerFixtureState::load(fixture_directory);
  if (::kill(state.supervisor_process_id(), SIGTERM) != 0 && errno != ESRCH) {
    fallback_stop_orphaned_server(state);
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.signal_supervisor",
                               std::error_code{errno, std::generic_category()}.message()};
  }
  const ServerExitResult result = wait_for_exit_result(state, fixture_directory);
  if (result.server_process_id() != state.server_process_id() || result.forced_shutdown() ||
      !result.exit_code().has_value() || *result.exit_code() != 0 ||
      result.signal_number().has_value()) {
    throw IntegrationTestError{
        IntegrationTestErrorCode::kProcessFailed, "server_fixture.validate_clean_exit",
        "SIGTERM did not produce one bounded clean server exit with status zero"};
  }
  if (::kill(state.server_process_id(), 0) == 0 || errno != ESRCH) {
    fallback_stop_orphaned_server(state);
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_fixture.validate_reaped_server",
                               "server process still exists after supervisor waitpid"};
  }

  boost::json::object success;
  success.emplace("status", "passed");
  success.emplace("operation", "cleanup");
  success.emplace("signal", "SIGTERM");
  success.emplace("server_process_id", state.server_process_id());
  success.emplace("exit_code", *result.exit_code());
  success.emplace("forced_shutdown", false);
  std::cout << boost::json::serialize(success) << '\n';
  return 0;
}

[[nodiscard]] boost::json::object failure_document(const IntegrationTestError& error) {
  boost::json::object failure;
  failure.emplace("status", "failed");
  failure.emplace("code", error.code());
  failure.emplace("operation", error.operation());
  failure.emplace("context", error.context());
  return failure;
}

int run_fixture(const int argument_count, const char* const arguments[]) {
  const FixtureArguments parsed_arguments = FixtureArguments::parse(argument_count, arguments);
  if (parsed_arguments.operation() == FixtureOperation::kSetup) {
    return setup_fixture(parsed_arguments);
  }
  return cleanup_fixture(parsed_arguments);
}

} // namespace
} // namespace blob_royale::integration_test

int main(const int argument_count, const char* const arguments[]) {
  try {
    return blob_royale::integration_test::run_fixture(argument_count, arguments);
  } catch (const blob_royale::integration_test::IntegrationTestError& error) {
    std::cerr << boost::json::serialize(blob_royale::integration_test::failure_document(error))
              << '\n';
  } catch (const std::exception& error) {
    boost::json::object failure;
    failure.emplace("status", "failed");
    failure.emplace("code", "INTEGRATION.UNEXPECTED_FAILURE");
    failure.emplace("operation", "server_fixture.run");
    failure.emplace("context", error.what());
    std::cerr << boost::json::serialize(failure) << '\n';
  }
  return 1;
}
