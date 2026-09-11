#ifndef BLOB_ROYALE_TESTS_UNIT_APPLICATION_APPLICATION_INPUT_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_APPLICATION_APPLICATION_INPUT_TEST_FIXTURE_HPP

#include "application_config_loader.hpp"
#include "application_input_error.hpp"
#include "scenario_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blob_royale::application::test_fixture {

inline constexpr std::string_view kValidConfiguration =
    "[server]\n"
    "bind_address=127.0.0.1\n"
    "port=8000\n"
    "allowed_hosts=127.0.0.1:8000, "
    "localhost:8000, [::1]:8000\n"
    "allowed_origins=\n"
    "trusted_proxy_addresses=\n"
    "\n"
    "[presentation]\n"
    "snapshots_per_second=30\n"
    "\n"
    "[simulation]\n"
    "ticks_per_second=400\n"
    "drag_per_second=0\n"
    "\n"
    "[world]\n"
    "width_world_units=960\n"
    "height_world_units=640\n"
    "player_radius_world_units=10\n"
    "\n"
    "[spatial_grid]\n"
    "columns=16\n"
    "rows=16\n"
    "\n"
    "[match]\n"
    "mode=royale\n"
    "map=arena-960x640\n"
    "maps_directory=maps\n"
    "seed=1\n"
    "lobby_seat_count=4\n"
    "bots=wanderer:2, chaser:1\n"
    "\n"
    "[movement]\n"
    "acceleration_world_units_per_second_squared=400\n"
    "normal_top_speed_world_units_per_second=10000\n"
    "\n"
    "[royale]\n"
    "zone_minimum_radius_world_units=60\n"
    "zone_shrink_seconds=90\n"
    "elimination_grace_seconds=3\n"
    "countdown_seconds=5\n"
    "restart_delay_seconds=8\n"
    "\n"
    "[king_of_the_hill]\n"
    "hill_radius_world_units=90\n"
    "hill_dwell_seconds=12\n"
    "hill_travel_seconds=4\n"
    "point_interval_seconds=1\n"
    "points_to_win=30\n"
    "contested_hill_scores=false\n"
    "time_limit_seconds=240\n"
    "respawn_delay_seconds=2\n"
    "countdown_seconds=5\n"
    "restart_delay_seconds=8\n"
    "\n"
    "[race]\n"
    "road=road\n"
    "checkpoint_radius_world_units=40\n"
    "respawn_delay_seconds=2\n"
    "finish_window_seconds=20\n"
    "time_limit_seconds=240\n"
    "countdown_seconds=5\n"
    "restart_delay_seconds=8\n"
    "\n"
    "[lobbies]\n"
    "count=1\n";

inline constexpr std::string_view kValidScenarioRows = "20,500,400,2.5,1.8,0,0\n"
                                                       "3,15,70,-1,3.2,-0,-0\n";

[[nodiscard]] inline std::string valid_scenario() {
  std::string scenario{ScenarioLoader::expected_header()};
  scenario.push_back('\n');
  scenario.append(kValidScenarioRows);
  return scenario;
}

[[nodiscard]] inline std::string replace_once(std::string source, const std::string_view target,
                                              const std::string_view replacement) {
  const std::size_t position = source.find(target);
  if (position == std::string::npos) {
    throw std::logic_error{"named test fixture fragment was not found"};
  }
  source.replace(position, target.size(), replacement);
  return source;
}

class TemporaryApplicationInputWorkspace final {
public:
  TemporaryApplicationInputWorkspace() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string directory_name{"blob-royale-application-input-tests-"};
    directory_name.append(std::to_string(timestamp));
    directory_name.push_back('-');
    directory_name.append(std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    root_path_ = std::filesystem::temp_directory_path() / directory_name;
    if (!std::filesystem::create_directory(root_path_)) {
      throw std::runtime_error{"temporary test workspace already exists"};
    }
  }

  TemporaryApplicationInputWorkspace(const TemporaryApplicationInputWorkspace&) = delete;
  TemporaryApplicationInputWorkspace(TemporaryApplicationInputWorkspace&&) = delete;
  TemporaryApplicationInputWorkspace& operator=(const TemporaryApplicationInputWorkspace&) = delete;
  TemporaryApplicationInputWorkspace& operator=(TemporaryApplicationInputWorkspace&&) = delete;

  ~TemporaryApplicationInputWorkspace() {
    std::error_code cleanup_error;
    std::filesystem::remove_all(root_path_, cleanup_error);
    if (cleanup_error) {
      std::terminate();
    }
  }

  [[nodiscard]] std::filesystem::path write_file(const std::string_view filename,
                                                 const std::string_view contents) const {
    const std::filesystem::path file_path = root_path_ / filename;
    std::ofstream output{file_path, std::ios::binary};
    if (!output.is_open()) {
      throw std::runtime_error{"failed to open temporary test fixture"};
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
      throw std::runtime_error{"failed to write temporary test fixture"};
    }
    return file_path;
  }

  [[nodiscard]] std::filesystem::path create_directory(const std::string_view name) const {
    const std::filesystem::path directory_path = root_path_ / name;
    if (!std::filesystem::create_directory(directory_path)) {
      throw std::runtime_error{"temporary test directory already exists"};
    }
    return directory_path;
  }

  [[nodiscard]] std::filesystem::path absent_path(const std::string_view name) const {
    return root_path_ / name;
  }

private:
  std::filesystem::path root_path_;
};

[[nodiscard]] inline ApplicationConfigLoader::Result
load_application_config(const std::filesystem::path& configuration_path,
                        const std::filesystem::path& scenario_path) {
  const std::string configuration_text = configuration_path.string();
  const std::string scenario_text = scenario_path.string();
  const std::array<const char*, 5> arguments = {
      "blob-royale", "--config", configuration_text.c_str(), "--scenario", scenario_text.c_str()};
  return ApplicationConfigLoader::load(static_cast<int>(arguments.size()), arguments.data());
}

// The same load with no `--scenario`, which is the shape a live match uses.
[[nodiscard]] inline ApplicationConfigLoader::Result
load_application_config(const std::filesystem::path& configuration_path) {
  const std::string configuration_text = configuration_path.string();
  const std::array<const char*, 3> arguments = {"blob-royale", "--config",
                                                configuration_text.c_str()};
  return ApplicationConfigLoader::load(static_cast<int>(arguments.size()), arguments.data());
}

template <typename Action>
void require_application_input_error_code(Action&& action,
                                          const ApplicationInputErrorCode expected_code) {
  try {
    std::forward<Action>(action)();
  } catch (const ApplicationInputError& error) {
    REQUIRE(error.error_code() == expected_code);
    CHECK_FALSE(error.context().empty());
    CHECK_FALSE(error.detail().empty());
    return;
  }
  FAIL("expected ApplicationInputError");
}

template <typename Error, typename ErrorCode, typename Action>
void require_domain_validation_error_code(Action&& action, const ErrorCode expected_code) {
  try {
    std::forward<Action>(action)();
  } catch (const Error& error) {
    REQUIRE(error.validation_code() == expected_code);
    CHECK_FALSE(error.context().empty());
    CHECK_FALSE(error.detail().empty());
    return;
  }
  FAIL("expected typed domain validation error");
}

} // namespace blob_royale::application::test_fixture

#endif
