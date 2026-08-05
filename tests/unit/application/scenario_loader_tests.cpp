#include "application_input_error.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "scenario_loader.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"

#include "application_input_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace blob_royale::application {
namespace {

using test_fixture::TemporaryApplicationInputWorkspace;

[[nodiscard]] simulation::SimulationConfig valid_simulation_config() {
  return simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16);
}

[[nodiscard]] std::string scenario_with_rows(const std::string_view rows) {
  std::string scenario{ScenarioLoader::expected_header()};
  scenario.push_back('\n');
  scenario.append(rows);
  return scenario;
}

TEST_CASE("scenario loader publishes one exact semantic CSV header",
          "[unit][application][scenario]") {
  CHECK(ScenarioLoader::expected_header() ==
        "entity_id,position_x_world_units,position_y_world_units,"
        "velocity_x_world_units_per_second,velocity_y_world_units_per_second,"
        "acceleration_x_world_units_per_second_squared,"
        "acceleration_y_world_units_per_second_squared");
}

TEST_CASE("scenario loader creates a canonical ID-ordered world", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("valid.csv", test_fixture::valid_scenario());

  const simulation::GameWorld world =
      ScenarioLoader::load(scenario_path, valid_simulation_config());

  REQUIRE(world.players().size() == 2);
  CHECK(world.players()[0].id().value() == 3);
  CHECK(world.players()[0].body().position().x() == 15.0);
  CHECK(world.players()[0].body().position().y() == 70.0);
  CHECK(world.players()[0].body().velocity().x() == -1.0);
  CHECK(world.players()[0].body().velocity().y() == 3.2);
  CHECK(world.players()[1].id().value() == 20);
}

TEST_CASE("scenario loader canonicalizes signed zero components", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("signed-zero.csv", test_fixture::valid_scenario());

  const simulation::GameWorld world =
      ScenarioLoader::load(scenario_path, valid_simulation_config());

  REQUIRE(world.players().size() == 2);
  CHECK_FALSE(std::signbit(world.players()[0].body().acceleration().x()));
  CHECK_FALSE(std::signbit(world.players()[0].body().acceleration().y()));
}

TEST_CASE("scenario loader accepts deterministic CRLF line endings",
          "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string crlf_scenario = scenario_with_rows("1,10,10,0,0,0,0\n");
  std::size_t newline_position = 0;
  while ((newline_position = crlf_scenario.find('\n', newline_position)) != std::string::npos) {
    crlf_scenario.replace(newline_position, 1, "\r\n");
    newline_position += 2;
  }
  const std::filesystem::path scenario_path = workspace.write_file("crlf.csv", crlf_scenario);

  const simulation::GameWorld world =
      ScenarioLoader::load(scenario_path, valid_simulation_config());

  REQUIRE(world.players().size() == 1);
  CHECK(world.players().front().id().value() == 1);
}

TEST_CASE("scenario loader accepts an explicitly empty world", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("empty.csv", ScenarioLoader::expected_header());

  const simulation::GameWorld world =
      ScenarioLoader::load(scenario_path, valid_simulation_config());

  CHECK(world.players().empty());
}

TEST_CASE("scenario loader rejects a missing scenario file", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(
            ScenarioLoader::load(workspace.absent_path("missing.csv"), valid_simulation_config()));
      },
      ApplicationInputErrorCode::kScenarioFileMissing);
}

TEST_CASE("scenario loader rejects a non-file scenario path", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;

  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(ScenarioLoader::load(workspace.create_directory("scenario-directory"),
                                               valid_simulation_config()));
      },
      ApplicationInputErrorCode::kScenarioPathNotRegularFile);
}

TEST_CASE("scenario loader rejects an oversized scenario file", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string oversized_scenario(ScenarioLoader::kMaximumScenarioFileBytes + 1, 'x');
  const std::filesystem::path scenario_path =
      workspace.write_file("oversized.csv", oversized_scenario);

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioFileTooLarge);
}

TEST_CASE("scenario loader rejects the legacy type-based header", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path = workspace.write_file(
      "legacy.csv", "type,x_pos,y_pos,x_vel,y_vel,x_acc,y_acc\nplayer,15,70,-1,3.2,0,0\n");

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioHeaderInvalid);
}

TEST_CASE("scenario loader rejects blank data rows", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("blank-row.csv", scenario_with_rows("\n1,10,10,0,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioRowEmpty);
}

TEST_CASE("scenario loader rejects short rows", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("short-row.csv", scenario_with_rows("1,10,10,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioColumnCountInvalid);
}

TEST_CASE("scenario loader rejects long rows", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("long-row.csv", scenario_with_rows("1,10,10,0,0,0,0,unexpected\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioColumnCountInvalid);
}

TEST_CASE("scenario loader rejects rows above the byte allocation limit",
          "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::string long_row(ScenarioLoader::kMaximumScenarioRowBytes + 1, '1');
  const std::filesystem::path scenario_path =
      workspace.write_file("byte-long-row.csv", scenario_with_rows(long_row));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioRowTooLong);
}

TEST_CASE("scenario loader rejects malformed numeric values", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("malformed-value.csv", scenario_with_rows("1,10,10,0trailing,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioValueInvalid);
}

TEST_CASE("scenario loader rejects quoted numeric fields", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("quoted-value.csv", scenario_with_rows("1,\"10\",10,0,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioValueInvalid);
}

TEST_CASE("scenario loader rejects non-finite physical values", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("non-finite.csv", scenario_with_rows("1,10,10,nan,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioValueNotFinite);
}

TEST_CASE("scenario loader rejects physical values above the domain limit",
          "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("out-of-range.csv", scenario_with_rows("1,10,10,1000000000001,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioValueOutOfRange);
}

TEST_CASE("scenario loader rejects entity identifiers below one", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("zero-id.csv", scenario_with_rows("0,10,10,0,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioEntityIdOutOfRange);
}

TEST_CASE("scenario loader rejects duplicate entity identifiers", "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path = workspace.write_file(
      "duplicate-id.csv", scenario_with_rows("7,10,10,0,0,0,0\n7,20,20,0,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioEntityIdDuplicate);
}

TEST_CASE("scenario loader accepts exact radius-inset world boundaries",
          "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path = workspace.write_file(
      "boundary.csv", scenario_with_rows("1,10,10,0,0,0,0\n2,950,630,0,0,0,0\n"));

  const simulation::GameWorld world =
      ScenarioLoader::load(scenario_path, valid_simulation_config());

  CHECK(world.players().size() == 2);
}

TEST_CASE("scenario loader rejects centers outside radius-inset world boundaries",
          "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  const std::filesystem::path scenario_path =
      workspace.write_file("outside.csv", scenario_with_rows("1,9.999,10,0,0,0,0\n"));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioPositionOutOfBounds);
}

TEST_CASE("scenario loader rejects player counts above the bounded world limit",
          "[unit][application][scenario]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string rows;
  for (std::size_t player_index = 0; player_index <= simulation::kMaximumPlayerCount;
       ++player_index) {
    rows.append(std::to_string(player_index + 1));
    rows.append(",10,10,0,0,0,0\n");
  }
  const std::filesystem::path scenario_path =
      workspace.write_file("too-many-players.csv", scenario_with_rows(rows));

  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(ScenarioLoader::load(scenario_path, valid_simulation_config())); },
      ApplicationInputErrorCode::kScenarioPlayerLimitExceeded);
}

} // namespace
} // namespace blob_royale::application
