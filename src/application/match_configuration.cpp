#include "match_configuration.hpp"

#include "application_input_error.hpp"
#include "controller_registry.hpp"
#include "game_mode_registry.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

// `common.schema.json#/$defs/kind_name`: the grammar every published mode and controller kind must
// already satisfy, checked here so an unencodable name is a startup rejection.
constexpr std::size_t kMaximumKindNameLength = 64;

[[nodiscard]] bool is_wire_kind_name(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumKindNameLength || value.front() < 'a' ||
      value.front() > 'z') {
    return false;
  }
  return std::all_of(value.cbegin(), value.cend(), [](const char character) {
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
           character == '_';
  });
}

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
  if (count == 0) {
    throw_roster_invalid("a roster term's count must be greater than zero; omit the term instead");
  }
  return count;
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
      throw_roster_invalid("a roster term must have the exact form <controller_kind>:<count>");
    }
    const std::string_view controller_kind = trim_horizontal_whitespace(term.substr(0, colon));
    const std::string_view count_text = trim_horizontal_whitespace(term.substr(colon + 1));
    if (controller_kind.empty()) {
      throw_roster_invalid("a roster term's controller kind must not be empty");
    }
    if (count_text.empty()) {
      throw_roster_invalid("a roster term's count must not be empty");
    }
    if (entries.size() >= kMaximumBotRosterEntryCount) {
      throw_roster_invalid("a roster may name at most " +
                           std::to_string(kMaximumBotRosterEntryCount) + " distinct kinds");
    }
    const bool already_named = std::any_of(entries.cbegin(), entries.cend(),
                                           [controller_kind](const BotRosterEntry& seen) {
                                             return seen.controller_kind == controller_kind;
                                           });
    if (already_named) {
      throw_roster_invalid("controller kind " + std::string{controller_kind} +
                           " is named twice; one term carries the whole count");
    }
    entries.push_back(BotRosterEntry{std::string{controller_kind}, parse_bot_count(count_text)});

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

  std::uint64_t total_bots = 0;
  for (const BotRosterEntry& entry : bot_roster) {
    if (!controllers::ControllerRegistry::contains(entry.controller_kind)) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotKindUnknown, "match.bots",
                                  "controller kind " + entry.controller_kind +
                                      " is registered by no row; the registered kinds are " +
                                      controllers::ControllerRegistry::registered_names()};
    }
    if (entry.count > kMaximumBotCount || total_bots > kMaximumBotCount - entry.count) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMatchBotRosterTooLarge, "match.bots",
                                  "a roster may seat at most " + std::to_string(kMaximumBotCount) +
                                      " bots"};
    }
    total_bots += entry.count;
  }

  return MatchConfiguration{std::move(mode_name), std::move(map_name), std::move(maps_directory),
                            seed, std::move(bot_roster)};
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
                                       std::vector<BotRosterEntry> bot_roster) noexcept
    : mode_name_(std::move(mode_name)), map_name_(std::move(map_name)),
      maps_directory_(std::move(maps_directory)), seed_(seed), bot_roster_(std::move(bot_roster)) {}

} // namespace blob_royale::application
