#include "replay_fixture.hpp"

#include "command_kind_mask.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/despawn_command.hpp"
#include "commands/join_command.hpp"
#include "commands/leave_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/start_match_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_mode_registry.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "king_of_the_hill/king_of_the_hill_mode.hpp"
#include "race/race_mode.hpp"
#include "royale/royale_mode.hpp"
#include "seat_roster.hpp"
#include "vector2.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace blob_royale::testing {
namespace {

constexpr std::size_t kMaximumReplayFileBytes = 1'048'576;

[[nodiscard]] std::string read_replay_file(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary};
  if (!file.is_open()) {
    throw ReplayFixtureError("replay fixture file is missing: " + path.string());
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  std::string text = contents.str();
  if (text.size() > kMaximumReplayFileBytes) {
    throw ReplayFixtureError("replay fixture file exceeds the accepted size: " + path.string());
  }
  return text;
}

[[nodiscard]] std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream{text};
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

// A full-line comment, and only a full-line one: `#` in the first column and nothing before it.
//
// **Added when the lobby commands landed, because a migrated fixture has to be able to say why it
// starts when it starts.** A `start_match` row at tick 2 rather than tick 1 is a derivation, not a
// datum, and a derivation with nowhere to live is a number the next reader has to re-derive or
// trust. The alternative -- explaining every fixture in the test that reads it -- puts the reason a
// screen away from the row and one file away from the person editing the row.
//
// It stays a *full-line* rule so nothing about the strictness below changes: a `#` anywhere else is
// still an ordinary character in an ordinary column, so no value can be silently truncated by a
// comment marker appearing inside it, and every other rejection this reader makes is untouched.
[[nodiscard]] bool is_comment(const std::string& line) noexcept {
  return !line.empty() && line.front() == '#';
}

[[nodiscard]] bool is_skipped(const std::string& line) noexcept {
  return line.empty() || is_comment(line);
}

// The index of the first line that is neither blank nor a comment, which is where a CSV's one
// accepted header row must be. It is `lines.size()` for a file that has no content at all, which
// the caller reports as a missing header rather than reading past the end.
[[nodiscard]] std::size_t first_content_line(const std::vector<std::string>& lines) noexcept {
  std::size_t index = 0;
  while (index < lines.size() && is_skipped(lines[index])) {
    ++index;
  }
  return index;
}

[[nodiscard]] std::vector<std::string> split_columns(const std::string& row) {
  std::vector<std::string> columns;
  std::size_t column_start = 0;
  while (true) {
    const std::size_t delimiter = row.find(',', column_start);
    if (delimiter == std::string::npos) {
      columns.push_back(row.substr(column_start));
      return columns;
    }
    columns.push_back(row.substr(column_start, delimiter - column_start));
    column_start = delimiter + 1;
  }
}

[[nodiscard]] double parse_double(const std::string& value, const std::string& where) {
  double parsed = 0.0;
  const char* const begin = value.data();
  const char* const end = begin + value.size();
  const auto [stop, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || stop != end || value.empty()) {
    throw ReplayFixtureError(where + ": '" + value + "' is not a decimal number");
  }
  return parsed;
}

[[nodiscard]] std::uint64_t parse_unsigned(const std::string& value, const std::string& where) {
  std::uint64_t parsed = 0;
  const char* const begin = value.data();
  const char* const end = begin + value.size();
  const auto [stop, error] = std::from_chars(begin, end, parsed);
  if (error != std::errc{} || stop != end || value.empty()) {
    throw ReplayFixtureError(where + ": '" + value + "' is not an unsigned integer");
  }
  return parsed;
}

// One strict INI document: `[section] key=value`, full-line `#` comments, no blank-line tolerance
// beyond fully empty lines, no duplicate keys, and no repeated sections. A comment is skipped
// before anything else looks at the line, so it can appear anywhere including above the first
// section.
class StrictIni final {
public:
  [[nodiscard]] static StrictIni parse(const std::string& text, const std::string& path) {
    StrictIni document;
    std::string section;
    std::size_t line_number = 0;
    for (const std::string& line : split_lines(text)) {
      ++line_number;
      const std::string where = path + ":" + std::to_string(line_number);
      if (is_skipped(line)) {
        continue;
      }
      if (line.front() == '[') {
        if (line.back() != ']' || line.size() < 3) {
          throw ReplayFixtureError(where + ": malformed section header '" + line + "'");
        }
        section = line.substr(1, line.size() - 2);
        if (!document.sections_.emplace(section, Entries{}).second) {
          throw ReplayFixtureError(where + ": section [" + section + "] is declared twice");
        }
        continue;
      }
      if (section.empty()) {
        throw ReplayFixtureError(where + ": '" + line + "' appears before any section header");
      }
      const std::size_t separator = line.find('=');
      if (separator == std::string::npos) {
        throw ReplayFixtureError(where + ": '" + line + "' is not a key=value pair");
      }
      const std::string key = line.substr(0, separator);
      const std::string value = line.substr(separator + 1);
      if (key.empty()) {
        throw ReplayFixtureError(where + ": empty key");
      }
      if (!document.sections_[section].emplace(key, value).second) {
        throw ReplayFixtureError(where + ": key '" + key + "' is declared twice in [" + section +
                                 "]");
      }
    }
    document.path_ = path;
    return document;
  }

  // Reads one required key and marks it consumed, so `require_every_key_was_read` can reject a key
  // no reader asked for. An unknown key is a rejection, never a silently ignored line.
  [[nodiscard]] const std::string& value(const std::string& section, const std::string& key) {
    const auto section_entry = sections_.find(section);
    if (section_entry == sections_.end()) {
      throw ReplayFixtureError(path_ + ": section [" + section + "] is missing");
    }
    const auto key_entry = section_entry->second.find(key);
    if (key_entry == section_entry->second.end()) {
      throw ReplayFixtureError(path_ + ": [" + section + "] " + key + " is missing");
    }
    read_keys_.emplace(section + "." + key);
    return key_entry->second;
  }

  [[nodiscard]] double number(const std::string& section, const std::string& key) {
    return parse_double(value(section, key), path_ + ": [" + section + "] " + key);
  }

  [[nodiscard]] std::uint64_t count(const std::string& section, const std::string& key) {
    return parse_unsigned(value(section, key), path_ + ": [" + section + "] " + key);
  }

  // The one boolean spelling the production loader accepts, for the same reason: four spellings
  // of one value are four ways for two fixtures to read differently while meaning the same thing.
  [[nodiscard]] bool flag(const std::string& section, const std::string& key) {
    const std::string& text = value(section, key);
    if (text == "true") {
      return true;
    }
    if (text == "false") {
      return false;
    }
    throw ReplayFixtureError(path_ + ": [" + section + "] " + key +
                             " must be exactly true or false");
  }

  void require_every_key_was_read() const {
    for (const auto& [section, entries] : sections_) {
      for (const auto& [key, unused_value] : entries) {
        if (!read_keys_.contains(section + "." + key)) {
          throw ReplayFixtureError(path_ + ": [" + section + "] " + key +
                                   " is not a key this format declares");
        }
      }
    }
  }

private:
  using Entries = std::map<std::string, std::string>;

  std::string path_;
  std::map<std::string, Entries> sections_;
  std::set<std::string> read_keys_;
};

constexpr std::string_view kMarkerHeader =
    "marker_kind,position_x_world_units,position_y_world_units";
// Nine columns since the lobby commands landed. The three new ones are empty for every kind that
// does not use them, which is the same rule the six original columns already obeyed: a row cannot
// carry a value the reader silently drops.
constexpr std::string_view kCommandHeader = "tick_sequence,entity_id,command_kind,controller_id,"
                                            "direction_x,direction_y,seat_index,seat_count,"
                                            "npc_kind";
constexpr std::size_t kCommandColumnCount = 9;

[[nodiscard]] std::vector<simulation::MapDefinition::Marker>
read_markers(const std::filesystem::path& path) {
  const std::vector<std::string> lines = split_lines(read_replay_file(path));
  const std::size_t header_index = first_content_line(lines);
  if (header_index >= lines.size() || lines[header_index] != kMarkerHeader) {
    throw ReplayFixtureError(path.string() + ": the first row must be exactly '" +
                             std::string(kMarkerHeader) + "'");
  }
  std::vector<simulation::MapDefinition::Marker> markers;
  for (std::size_t index = header_index + 1; index < lines.size(); ++index) {
    if (is_skipped(lines[index])) {
      continue;
    }
    const std::string where = path.string() + ":" + std::to_string(index + 1);
    const std::vector<std::string> columns = split_columns(lines[index]);
    if (columns.size() != 3) {
      throw ReplayFixtureError(where + ": expected 3 columns, found " +
                               std::to_string(columns.size()));
    }
    const simulation::Vector2 position =
        simulation::Vector2::create(parse_double(columns[1], where + " position_x_world_units"),
                                    parse_double(columns[2], where + " position_y_world_units"));
    markers.push_back(simulation::MapDefinition::Marker::create(columns[0], position, std::nullopt,
                                                                simulation::MapMetadata::none()));
  }
  return markers;
}

// One command row. The payload columns a kind does not use must be empty, so a row cannot carry a
// value that is silently dropped.
void require_empty(const std::vector<std::string>& columns, const std::size_t index,
                   const std::string_view column_name, const std::string& where) {
  if (!columns[index].empty()) {
    throw ReplayFixtureError(where + ": " + std::string(column_name) +
                             " must be empty for this command kind");
  }
}

[[nodiscard]] std::vector<std::vector<simulation::Command>>
read_commands(const std::filesystem::path& path, const std::uint64_t tick_count) {
  const std::vector<std::string> lines = split_lines(read_replay_file(path));
  const std::size_t header_index = first_content_line(lines);
  if (header_index >= lines.size() || lines[header_index] != kCommandHeader) {
    throw ReplayFixtureError(path.string() + ": the first row must be exactly '" +
                             std::string(kCommandHeader) + "'");
  }
  std::vector<std::vector<simulation::Command>> by_tick(static_cast<std::size_t>(tick_count));
  std::uint64_t previous_tick = 0;
  for (std::size_t index = header_index + 1; index < lines.size(); ++index) {
    if (is_skipped(lines[index])) {
      continue;
    }
    const std::string where = path.string() + ":" + std::to_string(index + 1);
    const std::vector<std::string> columns = split_columns(lines[index]);
    if (columns.size() != kCommandColumnCount) {
      throw ReplayFixtureError(where + ": expected " + std::to_string(kCommandColumnCount) +
                               " columns, found " + std::to_string(columns.size()));
    }
    const std::uint64_t tick = parse_unsigned(columns[0], where + " tick_sequence");
    if (tick == 0 || tick > tick_count) {
      throw ReplayFixtureError(where + ": tick_sequence " + std::to_string(tick) +
                               " is outside [1, " + std::to_string(tick_count) + "]");
    }
    if (tick < previous_tick) {
      throw ReplayFixtureError(where + ": rows must be ascending by tick_sequence");
    }
    previous_tick = tick;

    const std::string& kind = columns[2];
    std::vector<simulation::Command>& tick_commands = by_tick[static_cast<std::size_t>(tick) - 1];
    if (kind == "spawn") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 6, "seat_index", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::SpawnCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id"))}});
      continue;
    }
    if (kind == "despawn") {
      require_empty(columns, 3, "controller_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 6, "seat_index", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::DespawnCommand{
          simulation::EntityId::create(parse_unsigned(columns[1], where + " entity_id"))}});
      continue;
    }
    if (kind == "thrust") {
      require_empty(columns, 3, "controller_id", where);
      require_empty(columns, 6, "seat_index", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::ThrustCommand{
          simulation::EntityId::create(parse_unsigned(columns[1], where + " entity_id")),
          simulation::Vector2::create(parse_double(columns[4], where + " direction_x"),
                                      parse_double(columns[5], where + " direction_y"))}});
      continue;
    }
    // The four lobby kinds. Each names its sender in `controller_id`, exactly as the wire does: the
    // identity is what orders them and what de-duplicates them, so a row that omitted it would be a
    // command no batch could place (`src/simulation/command_registry.hpp`).
    if (kind == "set_seat_count") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 6, "seat_index", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::SetSeatCountCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id")),
          parse_unsigned(columns[7], where + " seat_count")}});
      continue;
    }
    if (kind == "clear_seat") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::ClearSeatCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id")),
          parse_unsigned(columns[6], where + " seat_index")}});
      continue;
    }
    if (kind == "seat_npc") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 7, "seat_count", where);
      // `SeatKindName::create` enforces the published `kind_name` grammar and throws a
      // SimulationValidationError for a name that fails it. That is deliberately **not** translated
      // into a ReplayFixtureError: a fixture naming an ungrammatical kind is exercising the same
      // rejection a client would get, and reporting it under the simulation's own code is what
      // makes the two comparable.
      tick_commands.push_back(simulation::Command{simulation::SeatNpcCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id")),
          parse_unsigned(columns[6], where + " seat_index"),
          simulation::SeatKindName::create(columns[8])}});
      continue;
    }
    if (kind == "start_match") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 6, "seat_index", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::StartMatchCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id"))}});
      continue;
    }
    // A controller asking for a seat: a person's names no seat, a bot's names the seat that
    // declared it. In production a session or the bot reconciliation submits it; in a replay it is
    // the row that says who sat down, and where.
    if (kind == "join") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      std::optional<std::uint64_t> seat_index;
      if (!columns[6].empty()) {
        seat_index = parse_unsigned(columns[6], where + " seat_index");
      }
      tick_commands.push_back(simulation::Command{simulation::JoinCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id")),
          seat_index}});
      continue;
    }
    // A departed controller. In production the sink enqueues it on session close; in a replay it is
    // the row that says a session ended, and the tick treats both identically.
    if (kind == "leave") {
      require_empty(columns, 1, "entity_id", where);
      require_empty(columns, 4, "direction_x", where);
      require_empty(columns, 5, "direction_y", where);
      require_empty(columns, 6, "seat_index", where);
      require_empty(columns, 7, "seat_count", where);
      require_empty(columns, 8, "npc_kind", where);
      tick_commands.push_back(simulation::Command{simulation::LeaveCommand{
          simulation::ControllerId::create(parse_unsigned(columns[3], where + " controller_id"))}});
      continue;
    }
    throw ReplayFixtureError(where + ": '" + kind + "' is not a registered command kind");
  }
  return by_tick;
}

[[nodiscard]] std::uint64_t
spawn_count_of(const std::vector<simulation::Command>& tick_commands) noexcept {
  std::uint64_t spawns = 0;
  for (const simulation::Command& command : tick_commands) {
    if (std::holds_alternative<simulation::SpawnCommand>(command)) {
      ++spawns;
    }
  }
  return spawns;
}

} // namespace

ReplayFixture ReplayFixture::load(const std::filesystem::path& replay_directory) {
  const std::filesystem::path match_path = replay_directory / "match.ini";
  StrictIni match = StrictIni::parse(read_replay_file(match_path), match_path.string());

  std::string mode_name = match.value("match", "mode");
  const std::uint64_t seed = match.count("match", "seed");
  const std::uint64_t tick_count = match.count("match", "tick_count");
  const std::uint64_t lobby_seat_count = match.count("match", "lobby_seat_count");

  simulation::MapDefinition map = simulation::MapDefinition::create(
      match.value("map", "name"),
      simulation::ArenaBounds::create(match.number("map", "width_world_units"),
                                      match.number("map", "height_world_units")),
      {}, read_markers(replay_directory / "markers.csv"), simulation::MapMetadata::none());

  // Read into named locals in declared order rather than as arguments, because the order in which
  // function arguments are evaluated is unspecified in C++: a fixture with two malformed values
  // would otherwise be rejected naming whichever key the compiler happened to reach first.
  const double player_radius = match.number("simulation", "player_radius_world_units");
  const std::uint64_t ticks_per_second = match.count("simulation", "ticks_per_second");
  const std::uint64_t grid_columns = match.count("simulation", "spatial_grid_columns");
  const std::uint64_t grid_rows = match.count("simulation", "spatial_grid_rows");
  const double drag_per_second = match.number("simulation", "drag_per_second");
  const simulation::SimulationConfig configuration = simulation::SimulationConfig::create(
      map.bounds().width(), map.bounds().height(), player_radius, ticks_per_second, grid_columns,
      grid_rows, drag_per_second);

  // The section of the mode the fixture names, and no other: a fixture whose balance numbers were
  // silently replaced by a mode's defaults would assert against a game it is not running, and a
  // section for a mode the fixture does not run is a key no reader asked for. Aggregate
  // initialization of a `Section` sequences its initializers left to right, unlike a function
  // call's arguments, so each one is already ordered.
  gameplay::GameModeConfiguration mode_configuration = gameplay::GameModeConfiguration::defaults();
  if (mode_name == gameplay::RoyaleMode::kModeName) {
    const gameplay::RoyaleConfiguration::Section royale_section{
        match.number("royale", "thrust_max_world_units_per_second_squared"),
        match.number("royale", "zone_minimum_radius_world_units"),
        match.number("royale", "zone_shrink_seconds"),
        match.number("royale", "elimination_grace_seconds"),
        match.number("royale", "countdown_seconds"),
        match.number("royale", "restart_delay_seconds")};
    mode_configuration.royale = gameplay::RoyaleConfiguration::create(royale_section);
  } else if (mode_name == gameplay::KingOfTheHillMode::kModeName) {
    const gameplay::KingOfTheHillConfiguration::Section hill_section{
        match.number("king_of_the_hill", "thrust_max_world_units_per_second_squared"),
        match.number("king_of_the_hill", "hill_radius_world_units"),
        match.number("king_of_the_hill", "hill_dwell_seconds"),
        match.number("king_of_the_hill", "hill_travel_seconds"),
        match.number("king_of_the_hill", "point_interval_seconds"),
        match.count("king_of_the_hill", "points_to_win"),
        match.flag("king_of_the_hill", "contested_hill_scores"),
        match.number("king_of_the_hill", "time_limit_seconds"),
        match.number("king_of_the_hill", "respawn_delay_seconds"),
        match.number("king_of_the_hill", "countdown_seconds"),
        match.number("king_of_the_hill", "restart_delay_seconds")};
    mode_configuration.king_of_the_hill =
        gameplay::KingOfTheHillConfiguration::create(hill_section);
  } else if (mode_name == gameplay::RaceMode::kModeName) {
    const gameplay::RaceConfiguration::Section race_section{
        match.number("race", "thrust_max_world_units_per_second_squared"),
        match.number("race", "track_half_width_world_units"),
        match.number("race", "checkpoint_radius_world_units"),
        match.number("race", "respawn_delay_seconds"),
        match.number("race", "finish_window_seconds"),
        match.number("race", "time_limit_seconds"),
        match.number("race", "countdown_seconds"),
        match.number("race", "restart_delay_seconds")};
    mode_configuration.race = gameplay::RaceConfiguration::create(race_section);
  } else {
    throw ReplayFixtureError(replay_directory.filename().string() + ": [match] mode=" + mode_name +
                             " has no configuration section this format knows how to read; " +
                             std::string(gameplay::RoyaleMode::kModeName) + ", " +
                             std::string(gameplay::KingOfTheHillMode::kModeName) + " and " +
                             std::string(gameplay::RaceMode::kModeName) + " do");
  }

  match.require_every_key_was_read();

  std::vector<std::vector<simulation::Command>> commands_by_tick =
      read_commands(replay_directory / "commands.csv", tick_count);
  std::vector<std::uint64_t> spawn_count_by_tick;
  spawn_count_by_tick.reserve(commands_by_tick.size());
  for (const std::vector<simulation::Command>& tick_commands : commands_by_tick) {
    spawn_count_by_tick.push_back(spawn_count_of(tick_commands));
  }

  return ReplayFixture(replay_directory.filename().string(), std::move(mode_name), seed, tick_count,
                       lobby_seat_count, configuration, std::move(map),
                       std::move(mode_configuration), std::move(commands_by_tick),
                       std::move(spawn_count_by_tick));
}

ReplayFixture ReplayFixture::named(const std::string& fixture_name) {
  return load(std::filesystem::path{BLOB_ROYALE_REPLAY_FIXTURE_DIRECTORY} / fixture_name);
}

ReplayFixture::ReplayFixture(std::string name, std::string mode_name, const std::uint64_t seed,
                             const std::uint64_t tick_count, const std::uint64_t lobby_seat_count,
                             simulation::SimulationConfig configuration,
                             simulation::MapDefinition map,
                             gameplay::GameModeConfiguration mode_configuration,
                             std::vector<std::vector<simulation::Command>> commands_by_tick,
                             std::vector<std::uint64_t> spawn_count_by_tick)
    : name_(std::move(name)), mode_name_(std::move(mode_name)), seed_(seed),
      tick_count_(tick_count), lobby_seat_count_(lobby_seat_count),
      configuration_(std::move(configuration)), map_(std::move(map)),
      mode_configuration_(std::move(mode_configuration)),
      commands_by_tick_(std::move(commands_by_tick)),
      spawn_count_by_tick_(std::move(spawn_count_by_tick)) {}

simulation::EntityId
ReplayFixture::first_reserved_entity_id(const std::uint64_t tick_sequence) const {
  if (tick_sequence == 0 || tick_sequence > tick_count_) {
    throw ReplayFixtureError(name_ + ": tick " + std::to_string(tick_sequence) +
                             " is outside [1, " + std::to_string(tick_count_) + "]");
  }
  // The map's static bodies occupy `[kMinimumEntityId, kMinimumEntityId + static_body_count)`, so
  // the first reservation opens above that block exactly as plan Step 22's allocator must.
  std::uint64_t cursor =
      simulation::kMinimumEntityId + static_cast<std::uint64_t>(map_.static_bodies().size());
  for (std::uint64_t tick = 1; tick < tick_sequence; ++tick) {
    cursor +=
        spawn_count_by_tick_[static_cast<std::size_t>(tick) - 1] + kSystemCreatedEntityHeadroom;
  }
  return simulation::EntityId::create(cursor);
}

simulation::EntityId ReplayFixture::spawned_entity_id(const std::uint64_t tick_sequence,
                                                      const std::uint64_t spawn_index) const {
  if (spawn_index >= spawn_count_by_tick_[static_cast<std::size_t>(tick_sequence) - 1]) {
    throw ReplayFixtureError(
        name_ + ": tick " + std::to_string(tick_sequence) + " carries " +
        std::to_string(spawn_count_by_tick_[static_cast<std::size_t>(tick_sequence) - 1]) +
        " spawn commands, so index " + std::to_string(spawn_index) + " names none");
  }
  return simulation::EntityId::create(first_reserved_entity_id(tick_sequence).value() +
                                      spawn_index);
}

std::vector<simulation::WorldSnapshot> ReplayFixture::run() const {
  // The mode name must resolve in the registry, because `[match] mode=` naming an unregistered game
  // is exactly the rejection the registry exists for. The mode is then built through the
  // registry's own factory from **this replay's** sections, which is how production builds it, so
  // the harness is generic over every registered game.
  if (!gameplay::GameModeRegistry::contains(mode_name_)) {
    throw ReplayFixtureError(name_ + ": [match] mode=" + mode_name_ +
                             " is registered by no row; the registered modes are " +
                             gameplay::GameModeRegistry::registered_names());
  }
  std::unique_ptr<const simulation::GameMode> mode =
      gameplay::GameModeRegistry::create(mode_name_, mode_configuration_);
  simulation::MapDefinition map = map_;
  simulation::GameWorld world = simulation::GameWorld::create(configuration_, map, seed_);
  // The lobby is part of the state a match begins in, so it is seeded onto the initial world here
  // exactly as `BlobRoyaleApplication::create` seeds it in production. **Every seat starts empty
  // and no start is requested**, so no recorded replay leaves `lobby` on its own: a replay that
  // needs a running match says so in its own `commands.csv`, with a `join` row for every player who
  // sits down and a `start_match` row that presses the button, on the tick its own comment derives.
  // A `seat_npc` row alone would not do it: a declared seat counts as filled only once a bot holds
  // it, and a replay has no runtime to build one.
  world.mutable_match().seats =
      simulation::SeatRoster::of_size(static_cast<std::size_t>(lobby_seat_count_));
  simulation::GameSimulation game = simulation::GameSimulation::create(
      configuration_, std::move(world),
      simulation::GameSimulationSetup::of_mode(std::move(map), std::move(mode)));

  std::vector<simulation::WorldSnapshot> snapshots;
  snapshots.reserve(static_cast<std::size_t>(tick_count_));
  for (std::uint64_t tick = 1; tick <= tick_count_; ++tick) {
    const std::vector<simulation::Command>& tick_commands =
        commands_by_tick_[static_cast<std::size_t>(tick) - 1];
    const simulation::InputBatch batch = simulation::InputBatch::create(
        tick_commands, game.accepted_command_kinds(),
        simulation::EntityIdReservation::create(
            first_reserved_entity_id(tick),
            spawn_count_by_tick_[static_cast<std::size_t>(tick) - 1] +
                kSystemCreatedEntityHeadroom));
    game.step(simulation::FixedDelta::canonical(), batch);
    snapshots.push_back(game.snapshot());
  }
  return snapshots;
}

std::size_t first_divergent_tick(const std::vector<simulation::WorldSnapshot>& expected,
                                 const std::vector<simulation::WorldSnapshot>& actual) {
  const std::size_t common = std::min(expected.size(), actual.size());
  for (std::size_t index = 0; index < common; ++index) {
    if (!(expected[index] == actual[index])) {
      return index + 1;
    }
  }
  if (expected.size() != actual.size()) {
    return common + 1;
  }
  return 0;
}

} // namespace blob_royale::testing
