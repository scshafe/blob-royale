#include "scenario_loader.hpp"

#include "application_input_error.hpp"
#include "application_text_file_reader.hpp"
#include "entity_id.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

constexpr std::array<std::string_view, ScenarioLoader::kScenarioColumnCount> kScenarioColumnNames =
    {
        "entity_id",
        "position_x_world_units",
        "position_y_world_units",
        "velocity_x_world_units_per_second",
        "velocity_y_world_units_per_second",
        "acceleration_x_world_units_per_second_squared",
        "acceleration_y_world_units_per_second_squared",
};

constexpr std::string_view kExpectedScenarioHeader =
    "entity_id,position_x_world_units,position_y_world_units,"
    "velocity_x_world_units_per_second,velocity_y_world_units_per_second,"
    "acceleration_x_world_units_per_second_squared,"
    "acceleration_y_world_units_per_second_squared";

[[nodiscard]] constexpr bool scenario_header_matches_column_names() noexcept {
  std::size_t header_position = 0;
  for (std::size_t column_index = 0; column_index < kScenarioColumnNames.size(); ++column_index) {
    const std::string_view column_name = kScenarioColumnNames[column_index];
    if (kExpectedScenarioHeader.substr(header_position, column_name.size()) != column_name) {
      return false;
    }
    header_position += column_name.size();
    if (column_index + 1 < kScenarioColumnNames.size()) {
      if (header_position >= kExpectedScenarioHeader.size() ||
          kExpectedScenarioHeader[header_position] != ',') {
        return false;
      }
      ++header_position;
    }
  }
  return header_position == kExpectedScenarioHeader.size();
}

static_assert(scenario_header_matches_column_names());

[[nodiscard]] std::string row_context(const std::filesystem::path& scenario_path,
                                      const std::size_t line_number) {
  std::string context = scenario_path.string();
  context.push_back(':');
  context.append(std::to_string(line_number));
  return context;
}

[[nodiscard]] std::string column_context(const std::filesystem::path& scenario_path,
                                         const std::size_t line_number,
                                         const std::size_t column_index) {
  std::string context = row_context(scenario_path, line_number);
  context.push_back(':');
  context.append(kScenarioColumnNames[column_index]);
  return context;
}

[[nodiscard]] std::array<std::string_view, ScenarioLoader::kScenarioColumnCount>
split_scenario_row(const std::string_view row, const std::filesystem::path& scenario_path,
                   const std::size_t line_number) {
  std::size_t column_count = 1;
  for (const char character : row) {
    if (character == ',') {
      ++column_count;
    }
  }
  if (column_count != ScenarioLoader::kScenarioColumnCount) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioColumnCountInvalid,
                                row_context(scenario_path, line_number),
                                "each data row must contain exactly seven comma-separated fields"};
  }

  std::array<std::string_view, ScenarioLoader::kScenarioColumnCount> columns{};
  std::size_t column_start = 0;
  for (std::size_t column_index = 0; column_index < columns.size(); ++column_index) {
    const std::size_t delimiter = row.find(',', column_start);
    const std::size_t column_end = delimiter == std::string_view::npos ? row.size() : delimiter;
    columns[column_index] = row.substr(column_start, column_end - column_start);
    column_start = column_end + 1;
  }
  return columns;
}

[[nodiscard]] std::uint64_t parse_entity_id(const std::string_view text,
                                            const std::filesystem::path& scenario_path,
                                            const std::size_t line_number) {
  std::uint64_t value = 0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parse_error == std::errc::result_out_of_range ||
      (parse_error == std::errc{} &&
       (value < simulation::kMinimumEntityId || value > simulation::kMaximumEntityId))) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioEntityIdOutOfRange,
                                column_context(scenario_path, line_number, 0),
                                "entity_id must be within the inclusive exact-integer range"};
  }
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioValueInvalid,
                                column_context(scenario_path, line_number, 0),
                                "entity_id must be one unsigned base-10 integer"};
  }
  return value;
}

[[nodiscard]] double parse_physical_component(const std::string_view text,
                                              const std::filesystem::path& scenario_path,
                                              const std::size_t line_number,
                                              const std::size_t column_index) {
  double value = 0.0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
  if (parse_error == std::errc::result_out_of_range) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioValueOutOfRange,
                                column_context(scenario_path, line_number, column_index),
                                "value is outside the binary64 range"};
  }
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioValueInvalid,
                                column_context(scenario_path, line_number, column_index),
                                "value must be one complete decimal number"};
  }
  if (!std::isfinite(value)) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioValueNotFinite,
                                column_context(scenario_path, line_number, column_index),
                                "physical components must be finite"};
  }
  if (std::abs(value) > simulation::kMaximumPhysicalComponentMagnitude) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioValueOutOfRange,
                                column_context(scenario_path, line_number, column_index),
                                "physical component exceeds the accepted simulation limit"};
  }
  return value;
}

void parse_player_row(const std::string_view row, const std::filesystem::path& scenario_path,
                      const std::size_t line_number,
                      const simulation::SimulationConfig& simulation_config,
                      std::set<std::uint64_t>& seen_entity_ids,
                      std::vector<simulation::GameWorld::EntitySeed>& seeds) {
  if (row.empty()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioRowEmpty,
                                row_context(scenario_path, line_number),
                                "blank data rows are not permitted"};
  }
  if (row.size() > ScenarioLoader::kMaximumScenarioRowBytes) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioRowTooLong,
                                row_context(scenario_path, line_number),
                                "data row exceeds the 4096-byte limit"};
  }
  if (seeds.size() >= simulation::kMaximumPlayerCount) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioPlayerLimitExceeded,
                                row_context(scenario_path, line_number),
                                "scenario exceeds the accepted 4096-player limit"};
  }

  const auto columns = split_scenario_row(row, scenario_path, line_number);
  const std::uint64_t entity_id_value = parse_entity_id(columns[0], scenario_path, line_number);
  const double position_x = parse_physical_component(columns[1], scenario_path, line_number, 1);
  const double position_y = parse_physical_component(columns[2], scenario_path, line_number, 2);
  const double velocity_x = parse_physical_component(columns[3], scenario_path, line_number, 3);
  const double velocity_y = parse_physical_component(columns[4], scenario_path, line_number, 4);
  const double acceleration_x = parse_physical_component(columns[5], scenario_path, line_number, 5);
  const double acceleration_y = parse_physical_component(columns[6], scenario_path, line_number, 6);

  const simulation::Vector2 position = simulation::Vector2::create(position_x, position_y);
  if (!simulation_config.contains_player_center(position)) {
    throw ApplicationInputError{
        ApplicationInputErrorCode::kScenarioPositionOutOfBounds,
        row_context(scenario_path, line_number),
        "player center must lie within the world bounds inset by the configured radius"};
  }
  if (!seen_entity_ids.emplace(entity_id_value).second) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioEntityIdDuplicate,
                                column_context(scenario_path, line_number, 0),
                                "entity_id must be unique within one scenario"};
  }

  const simulation::Vector2 velocity = simulation::Vector2::create(velocity_x, velocity_y);
  const simulation::Vector2 acceleration =
      simulation::Vector2::create(acceleration_x, acceleration_y);
  // Every scenario row is one baseline dynamic player disc: the configured common radius, unit
  // mass, and the single default collision layer and mask. The row's entity id is reused verbatim
  // as its ControllerId, so the entity-to-controller link is reproducible from the CSV alone and
  // needs no eighth column; a scenario entity therefore decides for itself until a session or a
  // bot claims it.
  seeds.push_back(simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(entity_id_value),
      simulation::PhysicsBody::create(
          position, velocity, acceleration, simulation_config.player_radius(),
          simulation::PhysicsBody::kDefaultMass, simulation::PhysicsBody::kDefaultCollisionLayer,
          simulation::PhysicsBody::kDefaultCollisionMask, false)));
}

} // namespace

simulation::GameWorld ScenarioLoader::load(const std::filesystem::path& scenario_path,
                                           const simulation::SimulationConfig& simulation_config) {
  const std::string contents = read_application_text_file(
      scenario_path, ApplicationTextFileKind::kScenario, kMaximumScenarioFileBytes);

  const std::size_t header_end = contents.find('\n');
  const std::size_t header_length = header_end == std::string::npos ? contents.size() : header_end;
  std::string_view header{contents.data(), header_length};
  if (!header.empty() && header.back() == '\r') {
    header.remove_suffix(1);
  }
  if (header != expected_header()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kScenarioHeaderInvalid,
                                row_context(scenario_path, 1),
                                "header must exactly match the accepted seven-column schema"};
  }

  std::set<std::uint64_t> seen_entity_ids;
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  std::size_t line_start = header_end == std::string::npos ? contents.size() : header_end + 1;
  std::size_t line_number = 2;
  while (line_start < contents.size()) {
    const std::size_t line_end = contents.find('\n', line_start);
    const std::size_t line_length =
        line_end == std::string::npos ? contents.size() - line_start : line_end - line_start;
    std::string_view row{contents.data() + line_start, line_length};
    if (!row.empty() && row.back() == '\r') {
      row.remove_suffix(1);
    }
    parse_player_row(row, scenario_path, line_number, simulation_config, seen_entity_ids, seeds);

    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
    ++line_number;
  }

  return simulation::GameWorld::create(std::move(seeds));
}

std::string_view ScenarioLoader::expected_header() noexcept { return kExpectedScenarioHeader; }

} // namespace blob_royale::application
