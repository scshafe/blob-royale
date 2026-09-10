#include "application_config.hpp"
#include "application_config_loader.hpp"
#include "game_mode_registry.hpp"
#include "map_loader.hpp"
#include "match_startup_validation.hpp"
#include "server_config.hpp"
#include "shared/hazard_archetype.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using blob_royale::application::ApplicationConfigLoader;

// Loads the committed cole-ubuntu-pc deployment inputs through the exact production parsers, in
// the shape `scripts/deploy-tailnet` launches the container: the configuration alone. A scenario
// seeds exactly one world and the deployment runs four rooms, so it passes none; the loader refuses
// the pair, and a fixture that passed one would be proving a launch the script does not make.
[[nodiscard]] ApplicationConfigLoader::Result load_deployment_inputs() {
  const std::filesystem::path deployment_directory{BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY};
  const std::string configuration_path = (deployment_directory / "blob-royale.cfg").string();
  const std::array<const char*, 3> arguments = {"blob-royale", "--config",
                                                configuration_path.c_str()};
  return ApplicationConfigLoader::load(static_cast<int>(arguments.size()), arguments.data());
}

} // namespace

TEST_CASE("deployment configuration for cole-ubuntu-pc loads through the application boundary",
          "[fixtures][deployment]") {
  const ApplicationConfigLoader::Result result = load_deployment_inputs();
  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(result));
  const blob_royale::server::ServerConfig& server_config =
      std::get<ApplicationConfigLoader::RunRequest>(result).application_config().server_config();

  CHECK(server_config.bind_address() == "127.0.0.1");
  CHECK(server_config.port() == 8000);
  CHECK(server_config.allows_host("127.0.0.1:8000"));
  CHECK(server_config.allows_host("cole-ubuntu-pc.colobus-stargazer.ts.net:8444"));
  CHECK(server_config.allows_origin(blob_royale::server::normalize_serialized_origin(
      "https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444")));
  CHECK_FALSE(server_config.allows_origin(blob_royale::server::normalize_serialized_origin(
      "http://cole-ubuntu-pc.colobus-stargazer.ts.net:8444")));
  // `tailscale serve` is the one proxy, on loopback, so every tailnet client is its own principal.
  REQUIRE(server_config.trusted_proxy_addresses().size() == 1);
  CHECK(server_config.trusted_proxy_addresses()[0] == "127.0.0.1");
  CHECK(server_config.snapshots_per_second() == 20);
}

TEST_CASE("the deployment runs four hill rooms and seeds no scenario", "[fixtures][deployment]") {
  // Four rooms is the count ADR 0006 measured a budget for
  // (`docs/architecture/0006-lobbies-as-rooms.md` § "The tick-loop decision"), and a scenario seeds
  // exactly one world, so a deployment with more than one room passes none. The pair is refused at
  // load with `APPLICATION.LOBBIES.SCENARIO_REQUIRES_ONE_LOBBY`, which is what the deploy script
  // used to trip over at readiness after the count went to four; this pins the launch shape the
  // script makes now.
  const ApplicationConfigLoader::Result result = load_deployment_inputs();
  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(result));
  const ApplicationConfigLoader::RunRequest& run_request =
      std::get<ApplicationConfigLoader::RunRequest>(result);

  CHECK_FALSE(run_request.scenario_path().has_value());
  CHECK(run_request.application_config().lobbies_configuration().count() == 4);
  CHECK(run_request.application_config().match_configuration().mode_name() == "king_of_the_hill");
  CHECK(run_request.application_config().match_configuration().map_name() == "hills-960x640");
}

TEST_CASE("both tailnet scripts identify their localhost readiness client to the trusted proxy",
          "[fixtures][deployment][script-contract]") {
  // Router tests prove this request is admitted and the headerless one is refused. This source
  // guard ties that behavior to BOTH callers: parser/configuration fixtures alone cannot catch a
  // deployment script reporting NOT_READY after it has already replaced a healthy container.
  const std::filesystem::path repository_directory =
      std::filesystem::path{BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY}.parent_path().parent_path();
  constexpr std::string_view expected_probe =
      "if curl --fail --silent --show-error --max-time 1 \\\n"
      "    --header 'X-Forwarded-For: 127.0.0.1' \\\n"
      "    \"http://${APPLICATION_ADDRESS}:${APPLICATION_PORT}/api/v1/health/ready\" 2>/dev/null |";
  for (const std::string_view script_name : {"deploy-tailnet", "reconfigure-tailnet"}) {
    INFO(script_name);
    std::ifstream source{repository_directory / "scripts" / script_name};
    REQUIRE(source.is_open());
    const std::string script{std::istreambuf_iterator<char>{source},
                             std::istreambuf_iterator<char>{}};
    CHECK(script.find(expected_probe) != std::string::npos);
  }
}

TEST_CASE("the deployed hazard table is the one intended and fits the snapshot entity bound",
          "[fixtures][deployment][hazard]") {
  // The point of this test is that the *shipped* configuration is provably playable, not that some
  // configuration is. `require_match_fits_snapshot_bound` runs at application startup and would
  // refuse a table whose standing population could push the published entity count past
  // `kSnapshotEntityLimit`; running it here means a hazard table that could not be served is a red
  // build rather than a match that degrades once people are in it.
  //
  // The map comes from the repository rather than from `[match] maps_directory`, which names the
  // container's read-only mount at `/run/blob-royale` and does not exist on a build machine.
  const ApplicationConfigLoader::Result result = load_deployment_inputs();
  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(result));
  const blob_royale::application::ApplicationConfig& application_config =
      std::get<ApplicationConfigLoader::RunRequest>(result).application_config();
  const std::vector<blob_royale::gameplay::HazardArchetype>& hazards =
      application_config.game_mode_configuration().hazards;

  // Two kinds, and the names are configuration rather than code: no C++ file contains either,
  // which is the whole acceptance bar for hazards being data. Adding a third is one section in
  // `deploy/ubuntu-pc/blob-royale.cfg` and one number here.
  REQUIRE(hazards.size() == 2);

  // Looked up by name rather than by index, because the order the loader returns instances in is
  // not something a configuration file should have to promise. A test that pins it would fail the
  // day someone reorders two sections that are, by construction, independent.
  const auto archetype_named =
      [&hazards](
          const std::string_view kind_name) -> const blob_royale::gameplay::HazardArchetype& {
    const auto found =
        std::find_if(hazards.cbegin(), hazards.cend(),
                     [kind_name](const blob_royale::gameplay::HazardArchetype& candidate) {
                       return candidate.kind_name() == kind_name;
                     });
    REQUIRE(found != hazards.cend());
    return *found;
  };
  const blob_royale::gameplay::HazardArchetype& comet = archetype_named("comet");
  const blob_royale::gameplay::HazardArchetype& boulder = archetype_named("boulder");

  // The lethal one is the small fast one and the heavy one is survivable. Pinning both means a
  // configuration edit that accidentally makes the 40-mass boulder lethal fails the build rather
  // than the playtest.
  CHECK(comet.lethal_on_contact());
  CHECK_FALSE(boulder.lethal_on_contact());
  CHECK(comet.mass() == 1.0);
  CHECK(boulder.mass() == 40.0);

  const blob_royale::simulation::MapDefinition map = blob_royale::application::MapLoader::load(
      std::filesystem::path{BLOB_ROYALE_MAPS_DIRECTORY} /
      application_config.match_configuration().map_name());
  const auto mode = blob_royale::gameplay::GameModeRegistry::create(
      application_config.match_configuration().mode_name(),
      application_config.game_mode_configuration());
  CHECK_NOTHROW(mode->validate_map(map));
  CHECK_NOTHROW(blob_royale::application::require_match_fits_snapshot_bound(
      application_config.match_configuration(), map, hazards));
  CHECK_NOTHROW(blob_royale::application::require_map_matches_published_world(
      application_config.simulation_config(), map));
  // Four seats on the eight-spawn hill map, and four rooms of it: the intended compact playtest.
  CHECK(application_config.match_configuration().lobby_seat_count() == 4);
  CHECK(application_config.lobbies_configuration().count() == 4);
  CHECK_NOTHROW(blob_royale::application::require_lobby_fits_map(
      *mode, application_config.match_configuration().lobby_seat_count(), map));
}
