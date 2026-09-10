#include "application_config.hpp"
#include "application_config_loader.hpp"
#include "application_input_error.hpp"
#include "game_mode_configuration.hpp"
#include "gameplay_validation_error.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "match_configuration.hpp"
#include "race/race_configuration.hpp"
#include "royale/royale_configuration.hpp"
#include "server_config.hpp"
#include "shared/hazard_archetype.hpp"
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

// Two hazard kinds, declared only here. **Neither `plaid_meteorite` nor `velvet_boulder` appears in
// any C++ file under `src/`**, which is the property the acceptance test below exists to hold: a
// reader can run `grep -r plaid_meteorite src/` and see no matches rather than take it on trust.
// They are deliberately absurd names so that nobody adds a builtin that happens to collide.
//
// There is no `[hazards]` section and no `kinds=` line, and that is the whole declaration: the set
// of hazard kinds is exactly the set of `[hazard.*]` sections, so there is no second list to keep
// in step (`gameplay/shared/hazard_archetype.hpp`).
constexpr std::string_view kHazardSections = "\n"
                                             "[hazard.plaid_meteorite]\n"
                                             "radius_world_units=10\n"
                                             "mass=1\n"
                                             "restitution=1\n"
                                             "speed_world_units_per_second=260\n"
                                             "spawn_interval_seconds=6\n"
                                             "lethal_on_contact=true\n"
                                             "\n"
                                             "[hazard.velvet_boulder]\n"
                                             "radius_world_units=26\n"
                                             "mass=40\n"
                                             "restitution=0.35\n"
                                             "speed_world_units_per_second=90\n"
                                             "spawn_interval_seconds=20\n"
                                             "lethal_on_contact=false\n";

[[nodiscard]] std::string configuration_with_hazards() {
  std::string configuration{test_fixture::kValidConfiguration};
  configuration.append(kHazardSections);
  return configuration;
}

[[nodiscard]] gameplay::GameModeConfiguration
load_game_mode_configuration(TemporaryApplicationInputWorkspace& workspace,
                             const std::string_view configuration) {
  const std::filesystem::path config_path = workspace.write_file("hazards.cfg", configuration);
  const ApplicationConfigLoader::Result result = test_fixture::load_application_config(config_path);
  return std::get<ApplicationConfigLoader::RunRequest>(result)
      .application_config()
      .game_mode_configuration();
}

void require_configuration_load_error(TemporaryApplicationInputWorkspace& workspace,
                                      const std::string_view configuration,
                                      const ApplicationInputErrorCode expected_code) {
  const std::filesystem::path config_path =
      workspace.write_file("invalid-configuration.cfg", configuration);
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(test_fixture::load_application_config(config_path)); },
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
        "       blob-royale --config <path> [--scenario <path>]\n");
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
  REQUIRE(run_request.scenario_path().has_value());
  CHECK(*run_request.scenario_path() == scenario_path);
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
  CHECK(run_request.application_config().simulation_config().drag_per_second() == 0.0);

  const MatchConfiguration& match = run_request.application_config().match_configuration();
  CHECK(match.mode_name() == "royale");
  CHECK(match.map_name() == "arena-960x640");
  CHECK(match.maps_directory() == std::filesystem::path{"maps"});
  CHECK(match.map_directory() == std::filesystem::path{"maps"} / "arena-960x640");
  CHECK(match.seed() == 1);
  CHECK(match.lobby_seat_count() == 4);
  REQUIRE(match.bot_roster().size() == 2);
  CHECK(match.bot_roster()[0] == MatchConfiguration::BotRosterEntry{"wanderer", 2});
  CHECK(match.bot_roster()[1] == MatchConfiguration::BotRosterEntry{"chaser", 1});
  CHECK(match.total_bot_count() == 3);

  // The `[royale]` section arrives already converted into the tick counts the mode's systems read;
  // no system ever sees a value in seconds (ADR 0005 section "Mode configuration").
  const gameplay::RoyaleConfiguration& royale =
      run_request.application_config().game_mode_configuration().royale;
  CHECK(royale.thrust_maximum() == 400.0);
  CHECK(royale.zone_minimum_radius() == 60.0);
  CHECK(royale.zone_shrink_ticks() == 36'000);
  CHECK(royale.elimination_grace_ticks() == 1'200);
  CHECK(royale.countdown_ticks() == 2'000);
  CHECK(royale.restart_delay_ticks() == 3'200);

  // `[king_of_the_hill]` arrives the same way, whatever `[match] mode` names: required, converted
  // once, and read only by the mode that owns it (ADR 0007 section "King of the hill").
  const gameplay::KingOfTheHillConfiguration& hill =
      run_request.application_config().game_mode_configuration().king_of_the_hill;
  CHECK(hill.thrust_maximum() == 400.0);
  CHECK(hill.hill_radius() == 90.0);
  CHECK(hill.hill_dwell_ticks() == 4'800);
  CHECK(hill.hill_travel_ticks() == 1'600);
  CHECK(hill.point_interval_ticks() == 400);
  CHECK(hill.points_to_win() == 30);
  CHECK_FALSE(hill.contested_hill_scores());
  CHECK(hill.time_limit_ticks() == 96'000);
  CHECK(hill.respawn_delay_ticks() == 800);
  CHECK(hill.countdown_ticks() == 2'000);
  CHECK(hill.restart_delay_ticks() == 3'200);

  const gameplay::RaceConfiguration& race =
      run_request.application_config().game_mode_configuration().race;
  CHECK(race.thrust_maximum() == 400.0);
  CHECK(race.track_half_width() == 70.0);
  CHECK(race.checkpoint_radius() == 40.0);
  CHECK(race.respawn_delay_ticks() == 800);
  CHECK(race.finish_window_ticks() == 8'000);
  CHECK(race.time_limit_ticks() == 96'000);
  CHECK(race.countdown_ticks() == 2'000);
  CHECK(race.restart_delay_ticks() == 3'200);

  // `[lobbies] count` is the one deployment-topology key: one room is the single-match server.
  CHECK(run_request.application_config().lobbies_configuration().count() == 1);
}

TEST_CASE("application config loader refuses a scenario with more than one lobby",
          "[unit][application][config][lobbies]") {
  // A scenario seeds one specific world and a room is built per lobby from the map alone, so a
  // scenario with several rooms would be several rooms of which only the first plays it.
  TemporaryApplicationInputWorkspace workspace;
  const std::string two_lobbies = test_fixture::replace_once(
      std::string{test_fixture::kValidConfiguration}, "count=1", "count=2");
  const std::filesystem::path config_path = workspace.write_file("two-lobbies.cfg", two_lobbies);
  const std::filesystem::path scenario_path = workspace.write_file("valid.csv", "scenario");

  // Two rooms without a scenario is the deployed shape.
  const ApplicationConfigLoader::Result accepted =
      test_fixture::load_application_config(config_path);
  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(accepted));
  CHECK(std::get<ApplicationConfigLoader::RunRequest>(accepted)
            .application_config()
            .lobbies_configuration()
            .count() == 2);

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(test_fixture::load_application_config(config_path, scenario_path)); },
      ApplicationInputErrorCode::kLobbiesScenarioRequiresOneLobby);
}

TEST_CASE("application config loader accepts a run request with no scenario",
          "[unit][application][config][match]") {
  // A match is fully described by `[match]` and the map it names; a scenario only seeds extra
  // entities, which is what fixtures need and a live deployment does not.
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path config_path =
      workspace.write_file("valid.cfg", test_fixture::kValidConfiguration);

  const ApplicationConfigLoader::Result result = test_fixture::load_application_config(config_path);

  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(result));
  CHECK_FALSE(std::get<ApplicationConfigLoader::RunRequest>(result).scenario_path().has_value());
}

TEST_CASE("application config loader rejects an unknown mode, map name, and bot kind",
          "[unit][application][config][match][validation]") {
  TemporaryApplicationInputWorkspace workspace;

  std::size_t rejection_ordinal = 0;
  const auto reject = [&workspace, &rejection_ordinal](
                          const std::string_view target, const std::string_view replacement,
                          const ApplicationInputErrorCode expected_code) {
    INFO("configuration replacement " << replacement);
    const std::string configuration = test_fixture::replace_once(
        std::string{test_fixture::kValidConfiguration}, target, replacement);
    const std::filesystem::path config_path = workspace.write_file(
        "match-" + std::to_string(rejection_ordinal++) + ".cfg", configuration);
    test_fixture::require_application_input_error_code(
        [&] { static_cast<void>(test_fixture::load_application_config(config_path)); },
        expected_code);
  };

  // A name the v2 grammar would reject, before the registry is even consulted.
  reject("mode=royale", "mode=Royale", ApplicationInputErrorCode::kMatchModeNameInvalid);
  // A well-formed name no row declares.
  reject("mode=royale", "mode=capture_the_flag", ApplicationInputErrorCode::kMatchModeUnknown);
  // A map name outside `common.schema.json#/$defs/map_name`, which is also what keeps `map=` a
  // name rather than a path.
  reject("map=arena-960x640", "map=../etc", ApplicationInputErrorCode::kMatchMapNameInvalid);
  // A registered-looking roster naming a kind no row declares.
  reject("bots=wanderer:2, chaser:1", "bots=stalker:1",
         ApplicationInputErrorCode::kMatchBotKindUnknown);
  // Malformed roster terms.
  reject("bots=wanderer:2, chaser:1", "bots=wanderer",
         ApplicationInputErrorCode::kMatchBotRosterInvalid);
  reject("bots=wanderer:2, chaser:1", "bots=wanderer:0",
         ApplicationInputErrorCode::kMatchBotRosterInvalid);
  reject("bots=wanderer:2, chaser:1", "bots=wanderer:1, wanderer:1",
         ApplicationInputErrorCode::kMatchBotRosterInvalid);
  // A lobby of no seats cannot be sat in, and the engine's roster bound is the ceiling.
  reject("lobby_seat_count=4", "lobby_seat_count=0",
         ApplicationInputErrorCode::kMatchLobbySeatCountOutOfRange);
  reject("lobby_seat_count=4", "lobby_seat_count=65",
         ApplicationInputErrorCode::kMatchLobbySeatCountOutOfRange);
  // A process runs at least one room and no more than the protocol's directory can list.
  reject("count=1", "count=0", ApplicationInputErrorCode::kLobbiesCountOutOfRange);
  reject("count=1", "count=9", ApplicationInputErrorCode::kLobbiesCountOutOfRange);
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

TEST_CASE("a hazard kind the source has never named loads from configuration alone",
          "[unit][application][config][hazard]") {
  // **The acceptance test of the whole feature.** Adding a hazard kind must be a configuration
  // section and no C++ at all, so this configuration declares two kinds whose names appear in no
  // C++ file under `src/`, and asserts they arrive carrying exactly the radius, mass, restitution,
  // speed, interval, and lethality the file declared. The claim is checkable rather than asserted:
  //
  //     grep -r plaid_meteorite src/    # no matches
  //     grep -r velvet_boulder src/     # no matches
  //
  // The day either name appears under `src/`, the code has learned about a specific kind and this
  // test is no longer testing what it says it is.
  TemporaryApplicationInputWorkspace workspace;

  const gameplay::GameModeConfiguration configuration =
      load_game_mode_configuration(workspace, configuration_with_hazards());

  REQUIRE(configuration.hazards.size() == 2);
  // The order is the file's, so a seeded spawner's choice among kinds is reproducible from the
  // configuration alone rather than from whatever order a container happened to yield.
  const gameplay::HazardArchetype& meteorite = configuration.hazards[0];
  CHECK(meteorite.kind_name() == "plaid_meteorite");
  CHECK(meteorite.radius() == 10.0);
  CHECK(meteorite.mass() == 1.0);
  CHECK(meteorite.restitution() == 1.0);
  CHECK(meteorite.speed() == 260.0);
  CHECK(meteorite.lethal_on_contact());
  // Authored as 6 s and stored as ticks, converted once at load like every other duration.
  CHECK(meteorite.spawn_interval_ticks() == 2'400);

  const gameplay::HazardArchetype& boulder = configuration.hazards[1];
  CHECK(boulder.kind_name() == "velvet_boulder");
  CHECK(boulder.radius() == 26.0);
  CHECK(boulder.mass() == 40.0);
  CHECK(boulder.restitution() == 0.35);
  CHECK(boulder.speed() == 90.0);
  CHECK(boulder.spawn_interval_ticks() == 8'000);
  CHECK_FALSE(boulder.lethal_on_contact());

  // The mode section is untouched by the family: adding hazards changed no `[royale]` value.
  CHECK(configuration.royale == gameplay::RoyaleConfiguration::defaults());
}

TEST_CASE("a configuration that declares no hazard section has no hazards",
          "[unit][application][config][hazard]") {
  // Zero instances is legal, and it is what every configuration in this tree looked like before
  // hazards existed. `[hazards]` is not a required section because there is no `[hazards]` section
  // at all, so `deploy/ubuntu-pc/blob-royale.cfg` and every fixture load exactly as they did.
  TemporaryApplicationInputWorkspace workspace;

  const gameplay::GameModeConfiguration configuration =
      load_game_mode_configuration(workspace, test_fixture::kValidConfiguration);

  CHECK(configuration.hazards.empty());
  CHECK(configuration.royale == gameplay::RoyaleConfiguration::defaults());
}

TEST_CASE("an unknown key inside a hazard section is rejected like one in a fixed section",
          "[unit][application][config][hazard][validation]") {
  // The instance *name* is open; the key schema inside it is not. This is the same
  // `APPLICATION.CONFIG.KEY_UNKNOWN` a misspelled `[royale]` key gets, by the same lookup.
  TemporaryApplicationInputWorkspace workspace;
  const std::string configuration =
      test_fixture::replace_once(configuration_with_hazards(), "\nmass=1\n", "\nweight=1\n");

  require_configuration_load_error(workspace, configuration,
                                   ApplicationInputErrorCode::kConfigurationKeyUnknown);
}

TEST_CASE("a section matching no fixed name and no family prefix is still rejected",
          "[unit][application][config][hazard][validation]") {
  // Exactly one thing became open, and these are the near misses that prove nothing else did.
  constexpr std::array<std::string_view, 6> unknown_sections = {
      // There is no family-wide settings section, so the plural is not a section name.
      "[hazards]\n",
      // A family prefix is not itself a section: an instance name is required.
      "[hazard]\n",
      // ...and an empty one is not an instance name.
      "[hazard.]\n",
      // The prefix must match a declared family exactly rather than by prefix.
      "[hazards.comet]\n",
      // A family that has not been declared yet is still unknown, which is what keeps the second
      // customer of this seam a deliberate edit rather than an accident.
      "[bot.wanderer]\n",
      // And an ordinary typo is unchanged.
      "[royal]\n"};

  TemporaryApplicationInputWorkspace workspace;
  for (const std::string_view unknown_section : unknown_sections) {
    CAPTURE(unknown_section);
    std::string configuration{test_fixture::kValidConfiguration};
    configuration.append("\n");
    configuration.append(unknown_section);

    require_configuration_load_error(workspace, configuration,
                                     ApplicationInputErrorCode::kConfigurationSectionUnknown);
  }
}

TEST_CASE("a repeated hazard kind is rejected like a repeated section",
          "[unit][application][config][hazard][validation]") {
  // Two sections claiming one kind are two archetypes claiming one name, and the spawner would have
  // no way to say which the configuration meant.
  TemporaryApplicationInputWorkspace workspace;
  const std::string configuration = test_fixture::replace_once(
      configuration_with_hazards(), "[hazard.velvet_boulder]", "[hazard.plaid_meteorite]");

  require_configuration_load_error(workspace, configuration,
                                   ApplicationInputErrorCode::kConfigurationSectionDuplicate);
}

TEST_CASE("a hazard section that omits any one of its keys is rejected",
          "[unit][application][config][hazard][validation]") {
  // No key has a silent default, which is the same rule every fixed section already answers to. The
  // fragments carry their surrounding newlines so removing `radius_world_units` cannot accidentally
  // strike `[world] player_radius_world_units`.
  constexpr std::array<std::string_view, 6> required_lines = {
      "\nradius_world_units=10\n",
      "\nmass=1\n",
      "\nrestitution=1\n",
      "\nspeed_world_units_per_second=260\n",
      "\nspawn_interval_seconds=6\n",
      "\nlethal_on_contact=true\n"};

  TemporaryApplicationInputWorkspace workspace;
  for (const std::string_view required_line : required_lines) {
    CAPTURE(required_line);
    const std::string configuration =
        test_fixture::replace_once(configuration_with_hazards(), required_line, "\n");

    require_configuration_load_error(workspace, configuration,
                                     ApplicationInputErrorCode::kConfigurationKeyMissing);
  }
}

TEST_CASE("every fixed section still rejects an unknown key",
          "[unit][application][config][validation]") {
  // The regression that says opening instance names opened nothing else: each of the ten fixed
  // sections refuses a key it does not declare, exactly as it did before families existed.
  constexpr std::array<std::string_view, 10> section_headers = {
      "[server]\n", "[presentation]\n", "[simulation]\n",       "[world]\n", "[spatial_grid]\n",
      "[match]\n",  "[royale]\n",       "[king_of_the_hill]\n", "[race]\n",  "[lobbies]\n"};

  TemporaryApplicationInputWorkspace workspace;
  for (const std::string_view section_header : section_headers) {
    CAPTURE(section_header);
    const std::string replacement = std::string{section_header} + "not_a_declared_key=1\n";
    const std::string configuration = test_fixture::replace_once(
        std::string{test_fixture::kValidConfiguration}, section_header, replacement);

    require_configuration_load_error(workspace, configuration,
                                     ApplicationInputErrorCode::kConfigurationKeyUnknown);
  }
}

TEST_CASE("a configuration without the [king_of_the_hill] section is refused naming its keys",
          "[unit][application][config][king_of_the_hill][validation]") {
  // The section is required whatever `[match] mode` names, exactly as `[royale]` is, so switching
  // a deployment to the hill is one edit that cannot fail on a section nobody wrote. Removing the
  // whole section is refused as its eleven missing keys, which is how a fixed section's absence
  // has always been reported.
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration{test_fixture::kValidConfiguration};
  const std::size_t section_start = configuration.find("[king_of_the_hill]\n");
  const std::size_t section_end = configuration.find("[race]\n");
  REQUIRE(section_start != std::string::npos);
  REQUIRE(section_end != std::string::npos);
  configuration.erase(section_start, section_end - section_start);

  const std::filesystem::path config_path = workspace.write_file("no-hill.cfg", configuration);
  try {
    static_cast<void>(test_fixture::load_application_config(config_path));
    FAIL("a configuration without [king_of_the_hill] loaded");
  } catch (const ApplicationInputError& error) {
    CHECK(error.error_code() == ApplicationInputErrorCode::kConfigurationKeyMissing);
    CHECK(std::string_view{error.what()}.find("king_of_the_hill.points_to_win") !=
          std::string_view::npos);
  }
}

TEST_CASE("a configuration without the [race] section is refused naming its keys",
          "[unit][application][config][race][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration{test_fixture::kValidConfiguration};
  const std::size_t section_start = configuration.find("[race]\n");
  const std::size_t section_end = configuration.find("[lobbies]\n");
  REQUIRE(section_start != std::string::npos);
  REQUIRE(section_end != std::string::npos);
  configuration.erase(section_start, section_end - section_start);

  const std::filesystem::path config_path = workspace.write_file("no-race.cfg", configuration);
  try {
    static_cast<void>(test_fixture::load_application_config(config_path));
    FAIL("a configuration without [race] loaded");
  } catch (const ApplicationInputError& error) {
    CHECK(error.error_code() == ApplicationInputErrorCode::kConfigurationKeyMissing);
    CHECK(std::string_view{error.what()}.find("race.checkpoint_radius_world_units") !=
          std::string_view::npos);
  }
}

TEST_CASE("the application passes authored race values through its validated section",
          "[unit][application][config][race]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string configuration{test_fixture::kValidConfiguration};
  configuration = test_fixture::replace_once(configuration, "track_half_width_world_units=70\n",
                                             "track_half_width_world_units=80\n");
  configuration = test_fixture::replace_once(configuration, "checkpoint_radius_world_units=40\n",
                                             "checkpoint_radius_world_units=30\n");
  configuration = test_fixture::replace_once(configuration, "finish_window_seconds=20\n",
                                             "finish_window_seconds=0.5\n");
  const std::filesystem::path config_path = workspace.write_file("race-values.cfg", configuration);
  const ApplicationConfigLoader::Result loaded = test_fixture::load_application_config(config_path);
  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(loaded));
  const gameplay::RaceConfiguration& race = std::get<ApplicationConfigLoader::RunRequest>(loaded)
                                                .application_config()
                                                .game_mode_configuration()
                                                .race;
  CHECK(race.track_half_width() == 80.0);
  CHECK(race.checkpoint_radius() == 30.0);
  CHECK(race.finish_window_ticks() == 200);
}

TEST_CASE("contested_hill_scores is spelled exactly true or false",
          "[unit][application][config][king_of_the_hill][validation]") {
  constexpr std::array<std::string_view, 3> refused_spellings = {
      "contested_hill_scores=0\n", "contested_hill_scores=False\n", "contested_hill_scores=no\n"};

  TemporaryApplicationInputWorkspace workspace;
  for (const std::string_view refused : refused_spellings) {
    CAPTURE(refused);
    const std::string configuration = test_fixture::replace_once(
        std::string{test_fixture::kValidConfiguration}, "contested_hill_scores=false\n", refused);

    require_configuration_load_error(workspace, configuration,
                                     ApplicationInputErrorCode::kConfigurationValueInvalid);
  }
}

TEST_CASE("lethality is spelled exactly true or false",
          "[unit][application][config][hazard][validation]") {
  // One spelling, so two deployments cannot read differently while meaning the same thing.
  constexpr std::array<std::string_view, 3> refused_spellings = {
      "lethal_on_contact=1\n", "lethal_on_contact=True\n", "lethal_on_contact=yes\n"};

  TemporaryApplicationInputWorkspace workspace;
  for (const std::string_view refused : refused_spellings) {
    CAPTURE(refused);
    const std::string configuration = test_fixture::replace_once(
        configuration_with_hazards(), "lethal_on_contact=true\n", refused);

    require_configuration_load_error(workspace, configuration,
                                     ApplicationInputErrorCode::kConfigurationValueInvalid);
  }
}

TEST_CASE("a hazard value the mechanic refuses is a startup rejection naming the key",
          "[unit][application][config][hazard][validation]") {
  // The loader parses the number and the mechanic that owns the rule refuses it, exactly as
  // `[royale]` and `[match]` already delegate. The context is the configuration line itself.
  TemporaryApplicationInputWorkspace workspace;
  const std::string configuration =
      test_fixture::replace_once(configuration_with_hazards(), "\nmass=1\n", "\nmass=0\n");
  const std::filesystem::path config_path =
      workspace.write_file("refused-hazard.cfg", configuration);

  test_fixture::require_domain_validation_error_code<gameplay::GameplayValidationError>(
      [&] { static_cast<void>(test_fixture::load_application_config(config_path)); },
      gameplay::GameplayValidationCode::kHazardScalarOutOfRange);
}

TEST_CASE("a hazard kind name outside the published grammar is rejected",
          "[unit][application][config][hazard][validation]") {
  // The loader leaves the grammar to the value that publishes the name, so this is a
  // `GAMEPLAY.HAZARD_KIND_NAME_INVALID` rather than an unknown section: the section family resolved
  // fine, and it is the kind that is unencodable.
  TemporaryApplicationInputWorkspace workspace;
  const std::string configuration = test_fixture::replace_once(
      configuration_with_hazards(), "[hazard.plaid_meteorite]", "[hazard.Plaid_Meteorite]");
  const std::filesystem::path config_path =
      workspace.write_file("bad-hazard-name.cfg", configuration);

  test_fixture::require_domain_validation_error_code<gameplay::GameplayValidationError>(
      [&] { static_cast<void>(test_fixture::load_application_config(config_path)); },
      gameplay::GameplayValidationCode::kHazardKindNameInvalid);
}

} // namespace
} // namespace blob_royale::application
