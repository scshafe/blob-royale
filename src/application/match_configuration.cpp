#include "match_configuration.hpp"

#include "seat_roster.hpp"

#include "application_input_error.hpp"
#include "controller_registry.hpp"
#include "game_mode_registry.hpp"
#include "snake_case_identity.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

// The grammar every published mode and controller kind must already satisfy is
// `simulation::is_wire_kind_name`, checked here so an unencodable name is a startup rejection.
// It is not restated: it was copied into three libraries once and is now owned by the one library
// all three depend on (`src/simulation/snake_case_identity.hpp`).
using simulation::is_wire_kind_name;
using simulation::kMaximumKindNameLength;

// `common.schema.json#/$defs/map_name`. It admits the dash and dot a directory name carries and no
// path separator at all, which is what keeps `map=` a name rather than a traversal.
[[nodiscard]] bool is_wire_map_name(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumKindNameLength) {
    return false;
  }
  const char first = value.front();
  const bool first_is_alphanumeric =
      (first >= 'a' && first <= 'z') || (first >= '0' && first <= '9');
  if (!first_is_alphanumeric) {
    return false;
  }
  return std::all_of(value.cbegin(), value.cend(), [](const char character) {
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
           character == '.' || character == '_' || character == '-';
  });
}

[[nodiscard]] std::string_view trim_horizontal_whitespace(std::string_view value) noexcept {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

[[noreturn]] void throw_roster_invalid(const std::string_view detail) {
  throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotRosterInvalid, "match.bots",
                              std::string{detail}};
}

[[nodiscard]] std::uint64_t parse_bot_count(const std::string_view text) {
  std::uint64_t count = 0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), count);
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw_roster_invalid("a roster term's count must be one unsigned base-10 integer");
  }
  return count;
}

// Shared by text parsing and direct validated-value construction: neither entry point may publish
// a zero count, a repeated declaration, or an oversized roster. Registry policy stays in create.
void require_roster_entry(const std::span<const MatchConfiguration::BotRosterEntry> preceding,
                          const MatchConfiguration::BotRosterEntry& entry) {
  if (preceding.size() >= MatchConfiguration::kMaximumBotRosterEntryCount) {
    throw_roster_invalid("a roster may name at most " +
                         std::to_string(MatchConfiguration::kMaximumBotRosterEntryCount) +
                         " distinct declarations");
  }
  if (entry.controller_kind.empty()) {
    throw_roster_invalid("a roster term's controller kind must not be empty");
  }
  if (entry.count == 0) {
    throw_roster_invalid("a roster term's count must be greater than zero; omit the term instead");
  }
  const bool already_named = std::any_of(preceding.begin(), preceding.end(),
                                         [&entry](const MatchConfiguration::BotRosterEntry& seen) {
                                           return seen.controller_kind == entry.controller_kind &&
                                                  seen.profile_name == entry.profile_name;
                                         });
  if (already_named) {
    std::string declaration = entry.controller_kind;
    if (entry.profile_name.has_value()) {
      declaration.append("@");
      declaration.append(entry.profile_name->value());
    }
    throw_roster_invalid("controller declaration " + declaration +
                         " is named twice; one term carries the whole count");
  }
}

} // namespace

std::vector<MatchConfiguration::BotRosterEntry>
MatchConfiguration::parse_bot_roster(const std::string_view value) {
  const std::string_view roster = trim_horizontal_whitespace(value);
  if (roster.empty()) {
    return {};
  }

  std::vector<BotRosterEntry> entries;
  std::size_t term_start = 0;
  while (term_start <= roster.size()) {
    const std::size_t delimiter = roster.find(',', term_start);
    const std::size_t term_end = delimiter == std::string_view::npos ? roster.size() : delimiter;
    const std::string_view term =
        trim_horizontal_whitespace(roster.substr(term_start, term_end - term_start));
    if (term.empty()) {
      throw_roster_invalid("a roster term must not be empty; separate terms with one comma");
    }

    const std::size_t colon = term.find(':');
    if (colon == std::string_view::npos || term.find(':', colon + 1) != std::string_view::npos) {
      throw_roster_invalid("a roster term must be <kind>:<count> or <kind>@<profile>:<count>");
    }
    const std::string_view declaration = trim_horizontal_whitespace(term.substr(0, colon));
    const std::size_t at = declaration.find('@');
    if (at != std::string_view::npos && declaration.find('@', at + 1) != std::string_view::npos) {
      throw_roster_invalid("a profiled roster term must contain exactly one @ separator");
    }
    const std::string_view controller_kind = trim_horizontal_whitespace(declaration.substr(0, at));
    std::optional<simulation::BotProfileName> profile_name;
    if (at != std::string_view::npos) {
      const std::string_view token = trim_horizontal_whitespace(declaration.substr(at + 1));
      if (token.empty()) {
        throw_roster_invalid("a profiled roster term must name a profile");
      }
      profile_name = simulation::BotProfileName::create(token);
    }
    const std::string_view count_text = trim_horizontal_whitespace(term.substr(colon + 1));
    if (controller_kind.empty()) {
      throw_roster_invalid("a roster term's controller kind must not be empty");
    }
    if (count_text.empty()) {
      throw_roster_invalid("a roster term's count must not be empty");
    }
    BotRosterEntry entry{std::string{controller_kind}, parse_bot_count(count_text),
                         std::move(profile_name)};
    require_roster_entry(entries, entry);
    entries.push_back(std::move(entry));

    if (delimiter == std::string_view::npos) {
      break;
    }
    term_start = delimiter + 1;
  }
  return entries;
}

MatchConfiguration MatchConfiguration::create(std::string mode_name, std::string map_name,
                                              std::filesystem::path maps_directory,
                                              const std::uint64_t seed,
                                              const std::uint64_t lobby_seat_count,
                                              std::vector<BotRosterEntry> bot_roster) {
  if (!is_wire_kind_name(mode_name)) {
    throw ApplicationInputError{
        ApplicationInputErrorCode::kMatchModeNameInvalid, "match.mode",
        "mode name " + mode_name +
            " must match the published kind grammar: lower snake case, first character a letter, "
            "at most 64 characters"};
  }
  if (!gameplay::GameModeRegistry::contains(mode_name)) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMatchModeUnknown, "match.mode",
                                "mode " + mode_name +
                                    " is registered by no row; the registered modes are " +
                                    gameplay::GameModeRegistry::registered_names()};
  }
  if (!is_wire_map_name(map_name)) {
    throw ApplicationInputError{
        ApplicationInputErrorCode::kMatchMapNameInvalid, "match.map",
        "map name " + map_name +
            " must match the published map-name grammar: lower case letters, digits, dot, "
            "underscore, and dash, starting with a letter or digit, at most 64 characters"};
  }
  if (maps_directory.empty()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMatchMapNameInvalid,
                                "match.maps_directory", "the maps directory must not be empty"};
  }
  // The lobby's own bounds, which are properties of the number rather than of the map: a lobby of
  // no seats cannot be sat in, and the engine's ceiling is the roster it copies into the working
  // world every tick (`src/simulation/seat_roster.hpp`). The tighter bound -- a spawn marker per
  // seat -- needs the map and the mode and is `require_lobby_fits_map`'s.
  if (lobby_seat_count < simulation::SeatRoster::kMinimumSeatCount ||
      lobby_seat_count > simulation::SeatRoster::kMaximumSeatCount) {
    throw ApplicationInputError{
        ApplicationInputErrorCode::kMatchLobbySeatCountOutOfRange, "match.lobby_seat_count",
        "a lobby has between " + std::to_string(simulation::SeatRoster::kMinimumSeatCount) +
            " and " + std::to_string(simulation::SeatRoster::kMaximumSeatCount) +
            " seats, and this configuration names " + std::to_string(lobby_seat_count)};
  }

  std::uint64_t total_bots = 0;
  const std::span<const BotRosterEntry> entries{bot_roster};
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const BotRosterEntry& entry = entries[index];
    require_roster_entry(entries.first(index), entry);
    const controllers::ControllerRegistry::Registration* registration =
        controllers::ControllerRegistry::find(entry.controller_kind);
    if (registration == nullptr) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotKindUnknown, "match.bots",
                                  "controller kind " + entry.controller_kind +
                                      " is registered by no row; the registered kinds are " +
                                      controllers::ControllerRegistry::registered_names()};
    }
    if (registration->requires_profile && !entry.profile_name.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotProfileRequired, "match.bots",
                                  "controller kind " + entry.controller_kind +
                                      " requires an authored profile selection"};
    }
    if (!registration->requires_profile && entry.profile_name.has_value()) {
      throw ApplicationInputError{
          ApplicationInputErrorCode::kMatchBotProfileUnexpected, "match.bots",
          "controller kind " + entry.controller_kind + " does not accept a profile selection"};
    }
    if (entry.count > kMaximumBotCount || total_bots > kMaximumBotCount - entry.count) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotRosterTooLarge, "match.bots",
                                  "a roster may seat at most " + std::to_string(kMaximumBotCount) +
                                      " bots"};
    }
    total_bots += entry.count;
  }

  return MatchConfiguration{std::move(mode_name),      std::move(map_name),
                            std::move(maps_directory), seed,
                            lobby_seat_count,          std::move(bot_roster)};
}

std::uint64_t MatchConfiguration::total_bot_count() const noexcept {
  std::uint64_t total = 0;
  for (const BotRosterEntry& entry : bot_roster_) {
    total += entry.count;
  }
  return total;
}

MatchConfiguration::MatchConfiguration(std::string mode_name, std::string map_name,
                                       std::filesystem::path maps_directory,
                                       const std::uint64_t seed,
                                       const std::uint64_t lobby_seat_count,
                                       std::vector<BotRosterEntry> bot_roster) noexcept
    : mode_name_(std::move(mode_name)), map_name_(std::move(map_name)),
      maps_directory_(std::move(maps_directory)), seed_(seed), lobby_seat_count_(lobby_seat_count),
      bot_roster_(std::move(bot_roster)) {}

} // namespace blob_royale::application
