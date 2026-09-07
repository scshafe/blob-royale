#include "application_config_loader.hpp"
#include "game_world.hpp"
#include "scenario_loader.hpp"
#include "server_config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <variant>

namespace {

using blob_royale::application::ApplicationConfigLoader;

// Loads the committed cole-ubuntu-pc deployment inputs through the exact production parsers.
[[nodiscard]] ApplicationConfigLoader::Result load_deployment_inputs() {
  const std::filesystem::path deployment_directory{BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY};
  const std::string configuration_path = (deployment_directory / "blob-royale.cfg").string();
  const std::string scenario_path = (deployment_directory / "scenario.csv").string();
  const std::array<const char*, 5> arguments = {
      "blob-royale", "--config", configuration_path.c_str(), "--scenario", scenario_path.c_str()};
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
  CHECK(server_config.trusted_proxy_addresses().empty());
  CHECK(server_config.snapshots_per_second() == 20);
}

TEST_CASE("deployment scenario for cole-ubuntu-pc loads a populated world",
          "[fixtures][deployment]") {
  const ApplicationConfigLoader::Result result = load_deployment_inputs();
  REQUIRE(std::holds_alternative<ApplicationConfigLoader::RunRequest>(result));
  const ApplicationConfigLoader::RunRequest& run_request =
      std::get<ApplicationConfigLoader::RunRequest>(result);

  REQUIRE(run_request.scenario_path().has_value());
  const blob_royale::simulation::GameWorld world = blob_royale::application::ScenarioLoader::load(
      *run_request.scenario_path(), run_request.application_config().simulation_config());

  CHECK(world.entities().size() == 4);
}
