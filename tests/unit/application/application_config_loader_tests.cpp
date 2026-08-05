#include "application_config.hpp"
#include "application_config_loader.hpp"
#include "application_input_error.hpp"
#include "server_config.hpp"
#include "simulation_config.hpp"
#include "simulation_validation_error.hpp"

#include "application_input_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace blob_royale::application {
namespace {

using test_fixture::TemporaryApplicationInputWorkspace;

[[nodiscard]] server::ServerConfig load_server_config(TemporaryApplicationInputWorkspace& workspace,
                                                      const std::string_view configuration) {
  const std::filesystem::path config_path =
      workspace.write_file("server-config.cfg", configuration);
  const ApplicationConfigLoader::Result result =
      test_fixture::load_application_config(config_path, workspace.absent_path("scenario.csv"));
  return std::get<ApplicationConfigLoader::RunRequest>(result).application_config().server_config();
}

void require_server_config_error(TemporaryApplicationInputWorkspace& workspace,
                                 const std::string_view configuration,
                                 const server::ServerConfigValidationCode expected_code) {
  const std::filesystem::path config_path =
      workspace.write_file("invalid-server.cfg", configuration);
  test_fixture::require_domain_validation_error_code<server::ServerConfigValidationError>(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      expected_code);
}

TEST_CASE("application config loader recognizes the exact help command",
          "[unit][application][config]") {
  const std::array<const char*, 2> arguments = {"blob-royale", "--help"};

  const ApplicationConfigLoader::Result result =
      ApplicationConfigLoader::load(static_cast<int>(arguments.size()), arguments.data());

  CHECK(std::holds_alternative<ApplicationConfigLoader::HelpRequest>(result));
}

TEST_CASE("application config loader exposes one stable usage string",
          "[unit][application][config]") {
  CHECK(ApplicationConfigLoader::help_text() ==
        "Usage: blob-royale --help\n"
        "       blob-royale --config <path> --scenario <path>\n");
}

TEST_CASE("application config loader creates the complete typed run request",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path config_path =
      workspace.write_file("valid.cfg", test_fixture::kValidConfiguration);
  const std::filesystem::path scenario_path = workspace.write_file("valid.csv", "scenario");

  const ApplicationConfigLoader::Result result =
      test_fixture::load_application_config(config_path, scenario_path);

  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(result));
  const ApplicationConfigLoader::RunRequest& run_request =
      std::get<ApplicationConfigLoader::RunRequest>(result);
  CHECK(run_request.scenario_path() == scenario_path);
  CHECK(run_request.application_config().server_config().bind_address() == "127.0.0.1");
  CHECK(run_request.application_config().server_config().port() == 8000);
  CHECK(run_request.application_config().server_config().snapshots_per_second() == 30);
  CHECK(run_request.application_config().server_config().allowed_hosts().size() == 3);
  CHECK(run_request.application_config().server_config().allows_host("127.0.0.1:8000"));
  CHECK(run_request.application_config().server_config().allows_host("localhost:8000"));
  CHECK(run_request.application_config().server_config().allows_host("[::1]:8000"));
  CHECK(run_request.application_config().server_config().allowed_origins().empty());
  CHECK(run_request.application_config().server_config().trusted_proxy_addresses().empty());
  CHECK(run_request.application_config().server_config().public_configuration().world_width() ==
        960.0);
  CHECK(run_request.application_config().server_config().public_configuration().world_height() ==
        640.0);
  CHECK(run_request.application_config().server_config().public_configuration().player_radius() ==
        10.0);
  CHECK(run_request.application_config()
            .server_config()
            .public_configuration()
            .snapshots_per_second() == 30);
  CHECK(run_request.application_config().simulation_config().world_width() == 960.0);
  CHECK(run_request.application_config().simulation_config().world_height() == 640.0);
  CHECK(run_request.application_config().simulation_config().player_radius() == 10.0);
  CHECK(run_request.application_config().simulation_config().ticks_per_second() == 400);
  CHECK(run_request.application_config().simulation_config().spatial_grid_columns() == 16);
  CHECK(run_request.application_config().simulation_config().spatial_grid_rows() == 16);
  CHECK(run_request.application_config().simulation_config().spatial_grid_cell_count() == 256);
}

TEST_CASE("application config loader trims comma-delimited server policy entries",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration},
      "allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000",
      "allowed_hosts=\t LOCALHOST:8000 \t, 127.0.0.1:8000 , [::1]:8000 \t");
  configuration = test_fixture::replace_once(
      std::move(configuration), "allowed_origins=",
      "allowed_origins= \thttps://PLAY.EXAMPLE.COM:443\t, http://localhost:5173 \t");
  configuration = test_fixture::replace_once(std::move(configuration), "trusted_proxy_addresses=",
                                             "trusted_proxy_addresses= \t127.0.0.1\t, ::1 \t");

  const server::ServerConfig server_config = load_server_config(workspace, configuration);

  constexpr std::array<std::string_view, 3> expected_hosts = {"127.0.0.1:8000", "[::1]:8000",
                                                              "localhost:8000"};
  constexpr std::array<std::string_view, 2> expected_origins = {"http://localhost:5173",
                                                                "https://play.example.com:443"};
  constexpr std::array<std::string_view, 2> expected_proxy_addresses = {"127.0.0.1", "::1"};
  CHECK(std::ranges::equal(server_config.allowed_hosts(), expected_hosts));
  CHECK(std::ranges::equal(server_config.allowed_origins(), expected_origins));
  CHECK(std::ranges::equal(server_config.trusted_proxy_addresses(), expected_proxy_addresses));
}

TEST_CASE("application config loader accepts explicitly empty optional server policy lists",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "allowed_origins=", "allowed_origins= \t");
  configuration = test_fixture::replace_once(
      std::move(configuration), "trusted_proxy_addresses=", "trusted_proxy_addresses=\t ");

  const server::ServerConfig server_config = load_server_config(workspace, configuration);

  CHECK(server_config.allowed_origins().empty());
  CHECK(server_config.trusted_proxy_addresses().empty());
}

TEST_CASE("application config loader continues to reject an explicitly empty scalar value",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "port= \t");
  const std::filesystem::path config_path =
      workspace.write_file("empty-scalar.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationValueInvalid);
}

TEST_CASE("application config loader rejects reordered run arguments",
          "[unit][application][config]") {
  const std::array<const char*, 5> arguments = {"blob-royale", "--scenario", "scenario.csv",
                                                "--config", "config.cfg"};

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(
            ApplicationConfigLoader::load(static_cast<int>(arguments.size()), arguments.data()));
      },
      ApplicationInputErrorCode::kCommandLineInvalid);
}

TEST_CASE("application config loader rejects extra arguments", "[unit][application][config]") {
  const std::array<const char*, 6> arguments = {"blob-royale", "--config",     "config.cfg",
                                                "--scenario",  "scenario.csv", "unexpected"};

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(
            ApplicationConfigLoader::load(static_cast<int>(arguments.size()), arguments.data()));
      },
      ApplicationInputErrorCode::kCommandLineInvalid);
}

TEST_CASE("application config loader rejects a missing configuration file",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            workspace.absent_path("missing.cfg"), workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationFileMissing);
}

TEST_CASE("application config loader rejects a non-file configuration path",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            workspace.create_directory("config-directory"), workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationPathNotRegularFile);
}

TEST_CASE("application config loader rejects an oversized configuration file",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string oversized_config(ApplicationConfigLoader::kMaximumConfigurationFileBytes + 1,
                                     'x');
  const std::filesystem::path config_path = workspace.write_file("oversized.cfg", oversized_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationFileTooLarge);
}

TEST_CASE("application config loader rejects an unknown section", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "[server]", "[Server]");
  const std::filesystem::path config_path =
      workspace.write_file("unknown-section.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationSectionUnknown);
}

TEST_CASE("application config loader rejects a duplicate section", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string invalid_config{test_fixture::kValidConfiguration};
  invalid_config.append("[server]\n");
  const std::filesystem::path config_path =
      workspace.write_file("duplicate-section.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationSectionDuplicate);
}

TEST_CASE("application config loader rejects an unknown key", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "listen_port=8000");
  const std::filesystem::path config_path = workspace.write_file("unknown-key.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationKeyUnknown);
}

TEST_CASE("application config loader rejects a duplicate key", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "port=8000\nport=8001");
  const std::filesystem::path config_path =
      workspace.write_file("duplicate-key.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationKeyDuplicate);
}

TEST_CASE("application config loader rejects a missing key", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config =
      test_fixture::replace_once(std::string{test_fixture::kValidConfiguration}, "port=8000\n", "");
  const std::filesystem::path config_path = workspace.write_file("missing-key.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationKeyMissing);
}

TEST_CASE(
    "application config loader requires every server policy key even when its list may be empty",
    "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  constexpr std::array<std::string_view, 3> required_lines = {
      "allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000\n", "allowed_origins=\n",
      "trusted_proxy_addresses=\n"};

  for (const std::string_view required_line : required_lines) {
    CAPTURE(required_line);
    const std::string invalid_config = test_fixture::replace_once(
        std::string{test_fixture::kValidConfiguration}, required_line, "");
    const std::filesystem::path config_path =
        workspace.write_file("missing-server-policy-key.cfg", invalid_config);

    test_fixture::require_application_input_error_code(
        [&] {
          static_cast<void>(test_fixture::load_application_config(
              config_path, workspace.absent_path("scenario.csv")));
        },
        ApplicationInputErrorCode::kConfigurationKeyMissing);
  }
}

TEST_CASE("application config loader delegates an empty required host list to ServerConfig",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration},
      "allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000", "allowed_hosts= \t");

  require_server_config_error(workspace, invalid_config,
                              server::ServerConfigValidationCode::kHostAllowlistInvalid);
}

TEST_CASE("application config loader preserves malformed list structure for domain validation",
          "[unit][application][config]") {
  struct InvalidListCase final {
    std::string_view target;
    std::string_view replacement;
    server::ServerConfigValidationCode expected_code;
  };

  constexpr std::array<InvalidListCase, 7> invalid_lists = {{
      {"allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000", "allowed_hosts=,127.0.0.1:8000",
       server::ServerConfigValidationCode::kHostAllowlistInvalid},
      {"allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000", "allowed_hosts=127.0.0.1:8000,",
       server::ServerConfigValidationCode::kHostAllowlistInvalid},
      {"allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000",
       "allowed_hosts=127.0.0.1:8000,,localhost:8000",
       server::ServerConfigValidationCode::kHostAllowlistInvalid},
      {"allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000",
       "allowed_hosts=127.0.0.1:8000, \t ,localhost:8000",
       server::ServerConfigValidationCode::kHostAllowlistInvalid},
      {"allowed_origins=", "allowed_origins=,https://play.example.com",
       server::ServerConfigValidationCode::kOriginAllowlistInvalid},
      {"allowed_origins=", "allowed_origins=https://play.example.com,",
       server::ServerConfigValidationCode::kOriginAllowlistInvalid},
      {"trusted_proxy_addresses=", "trusted_proxy_addresses=127.0.0.1,",
       server::ServerConfigValidationCode::kTrustedProxyAllowlistInvalid},
  }};

  TemporaryApplicationInputWorkspace workspace;
  for (const InvalidListCase& invalid_list : invalid_lists) {
    CAPTURE(invalid_list.replacement);
    const std::string invalid_config =
        test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                   invalid_list.target, invalid_list.replacement);
    require_server_config_error(workspace, invalid_config, invalid_list.expected_code);
  }
}

TEST_CASE("application config loader delegates duplicate and unsafe policy entries to ServerConfig",
          "[unit][application][config]") {
  struct InvalidPolicyCase final {
    std::string_view target;
    std::string_view replacement;
    server::ServerConfigValidationCode expected_code;
  };

  constexpr std::array<InvalidPolicyCase, 6> invalid_policies = {{
      {"allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000",
       "allowed_hosts=localhost:8000, LOCALHOST:8000",
       server::ServerConfigValidationCode::kHostAllowlistInvalid},
      {"allowed_hosts=127.0.0.1:8000, localhost:8000, [::1]:8000",
       "allowed_hosts=*.example.com:8000",
       server::ServerConfigValidationCode::kHostAllowlistInvalid},
      {"allowed_origins=", "allowed_origins=https://EXAMPLE.com,https://example.com",
       server::ServerConfigValidationCode::kOriginAllowlistInvalid},
      {"allowed_origins=", "allowed_origins=https://example.com/path",
       server::ServerConfigValidationCode::kOriginAllowlistInvalid},
      {"trusted_proxy_addresses=", "trusted_proxy_addresses=127.0.0.1,127.0.0.1",
       server::ServerConfigValidationCode::kTrustedProxyAllowlistInvalid},
      {"trusted_proxy_addresses=", "trusted_proxy_addresses=localhost",
       server::ServerConfigValidationCode::kTrustedProxyAllowlistInvalid},
  }};

  TemporaryApplicationInputWorkspace workspace;
  for (const InvalidPolicyCase& invalid_policy : invalid_policies) {
    CAPTURE(invalid_policy.replacement);
    const std::string invalid_config =
        test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                   invalid_policy.target, invalid_policy.replacement);
    require_server_config_error(workspace, invalid_config, invalid_policy.expected_code);
  }
}

TEST_CASE("application config loader rejects malformed INI syntax", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "port:8000");
  const std::filesystem::path config_path = workspace.write_file("malformed.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationSyntaxInvalid);
}

TEST_CASE("application config loader rejects malformed numeric values",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "port=8000trailing");
  const std::filesystem::path config_path =
      workspace.write_file("invalid-number.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationValueInvalid);
}

TEST_CASE("application config loader rejects overflowing numeric values",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "port=184467440737095516160");
  const std::filesystem::path config_path = workspace.write_file("overflow.cfg", invalid_config);

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      ApplicationInputErrorCode::kConfigurationValueOutOfRange);
}

TEST_CASE("simulation config rejects non-finite world values", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config =
      test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                 "width_world_units=960", "width_world_units=nan");
  const std::filesystem::path config_path = workspace.write_file("non-finite.cfg", invalid_config);

  test_fixture::require_domain_validation_error_code<simulation::SimulationValidationError>(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      simulation::SimulationValidationCode::kConfigWorldScalarNotFinite);
}

TEST_CASE("simulation config rejects a world too small for its player radius",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config =
      test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                 "width_world_units=960", "width_world_units=20");
  const std::filesystem::path config_path = workspace.write_file("small-world.cfg", invalid_config);

  test_fixture::require_domain_validation_error_code<simulation::SimulationValidationError>(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      simulation::SimulationValidationCode::kConfigWorldTooSmallForPlayer);
}

TEST_CASE("simulation config rejects a different fixed tick rate", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "ticks_per_second=400", "ticks_per_second=0");
  const std::filesystem::path config_path =
      workspace.write_file("invalid-rate.cfg", invalid_config);

  test_fixture::require_domain_validation_error_code<simulation::SimulationValidationError>(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      simulation::SimulationValidationCode::kConfigTickRateUnsupported);
}

TEST_CASE("simulation config rejects zero spatial-grid dimensions", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "columns=16", "columns=0");
  const std::filesystem::path config_path = workspace.write_file("zero-grid.cfg", invalid_config);

  CHECK_THROWS_AS(
      test_fixture::load_application_config(config_path, workspace.absent_path("scenario.csv")),
      simulation::SimulationValidationError);
}

TEST_CASE("simulation config rejects unsafe spatial-grid allocation",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "columns=16", "columns=1048576");
  invalid_config = test_fixture::replace_once(std::move(invalid_config), "rows=16", "rows=2");
  const std::filesystem::path config_path = workspace.write_file("large-grid.cfg", invalid_config);

  test_fixture::require_domain_validation_error_code<simulation::SimulationValidationError>(
      [&] {
        static_cast<void>(test_fixture::load_application_config(
            config_path, workspace.absent_path("scenario.csv")));
      },
      simulation::SimulationValidationCode::kConfigSpatialGridCellLimitExceeded);
}

TEST_CASE("server config rejects an invalid bind address", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config =
      test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                 "bind_address=127.0.0.1", "bind_address=localhost");
  const std::filesystem::path config_path =
      workspace.write_file("invalid-bind.cfg", invalid_config);

  CHECK_THROWS_AS(
      test_fixture::load_application_config(config_path, workspace.absent_path("scenario.csv")),
      server::ServerConfigValidationError);
}

TEST_CASE("server config rejects port zero", "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "port=8000", "port=0");
  const std::filesystem::path config_path =
      workspace.write_file("invalid-port.cfg", invalid_config);

  CHECK_THROWS_AS(
      test_fixture::load_application_config(config_path, workspace.absent_path("scenario.csv")),
      server::ServerConfigValidationError);
}

TEST_CASE("server config rejects presentation rates above protocol v1",
          "[unit][application][config]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string invalid_config =
      test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                 "snapshots_per_second=30", "snapshots_per_second=61");
  const std::filesystem::path config_path =
      workspace.write_file("invalid-presentation.cfg", invalid_config);

  CHECK_THROWS_AS(
      test_fixture::load_application_config(config_path, workspace.absent_path("scenario.csv")),
      server::ServerConfigValidationError);
}

} // namespace
} // namespace blob_royale::application
