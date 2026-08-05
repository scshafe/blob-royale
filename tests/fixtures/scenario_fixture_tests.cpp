#include <catch2/catch_test_macros.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view expected_header =
    "entity_id,position_x_world_units,position_y_world_units,"
    "velocity_x_world_units_per_second,velocity_y_world_units_per_second,"
    "acceleration_x_world_units_per_second_squared,"
    "acceleration_y_world_units_per_second_squared";
constexpr std::size_t expected_column_count = 7;

std::vector<std::string> split_csv_row(const std::string& row) {
  std::vector<std::string> columns;
  std::size_t column_start = 0;
  while (column_start <= row.size()) {
    const std::size_t delimiter = row.find(',', column_start);
    if (delimiter == std::string::npos) {
      columns.emplace_back(row.substr(column_start));
      break;
    }
    columns.emplace_back(row.substr(column_start, delimiter - column_start));
    column_start = delimiter + 1;
  }
  return columns;
}

bool is_unsigned_integer(const std::string& value) {
  std::uint64_t parsed_value = 0;
  const char* const value_begin = value.data();
  const char* const value_end = value_begin + value.size();
  const auto [parse_end, parse_error] = std::from_chars(value_begin, value_end, parsed_value);
  return parse_error == std::errc{} && parse_end == value_end;
}

bool is_double(const std::string& value) {
  double parsed_value = 0.0;
  const char* const value_begin = value.data();
  const char* const value_end = value_begin + value.size();
  const auto [parse_end, parse_error] = std::from_chars(value_begin, value_end, parsed_value);
  return parse_error == std::errc{} && parse_end == value_end;
}

void require_readable_scenario_shape(const std::string& fixture_filename) {
  const std::string fixture_path =
      std::string{BLOB_ROYALE_SCENARIO_FIXTURE_DIRECTORY} + "/" + fixture_filename;
  std::ifstream fixture{fixture_path};
  REQUIRE(fixture.is_open());

  std::string row;
  REQUIRE(std::getline(fixture, row));
  REQUIRE(row == expected_header);

  std::size_t player_row_count = 0;
  while (std::getline(fixture, row)) {
    REQUIRE_FALSE(row.empty());
    const std::vector<std::string> columns = split_csv_row(row);
    REQUIRE(columns.size() == expected_column_count);
    REQUIRE(is_unsigned_integer(columns.front()));
    for (std::size_t column_index = 1; column_index < columns.size(); ++column_index) {
      REQUIRE(is_double(columns[column_index]));
    }
    ++player_row_count;
  }

  REQUIRE(player_row_count > 0);
  REQUIRE(fixture.eof());
}
} // namespace

TEST_CASE("player-on-wall scenario fixture is readable and rectangular", "[fixtures][csv]") {
  require_readable_scenario_shape("player-on-wall-collision-test.csv");
}

TEST_CASE("player-on-player scenario fixture is readable and rectangular", "[fixtures][csv]") {
  require_readable_scenario_shape("player-on-player-collision-test.csv");
}

TEST_CASE("partition trace scenario fixture is readable and rectangular", "[fixtures][csv]") {
  require_readable_scenario_shape("partition-trace-test.csv");
}
