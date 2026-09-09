#include "application_config_loader.hpp"

#include "application_input_error.hpp"
#include "application_text_file_reader.hpp"
#include "game_mode_configuration.hpp"
#include "lobbies_configuration.hpp"
#include "match_configuration.hpp"
#include "royale/royale_configuration.hpp"
#include "shared/hazard_archetype.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

enum class ConfigField : std::size_t {
  kServerBindAddress,
  kServerPort,
  kServerAllowedHosts,
  kServerAllowedOrigins,
  kServerTrustedProxyAddresses,
  kPresentationSnapshotsPerSecond,
  kSimulationTicksPerSecond,
  kSimulationDragPerSecond,
  kWorldWidth,
  kWorldHeight,
  kWorldPlayerRadius,
  kSpatialGridColumns,
  kSpatialGridRows,
  kMatchMode,
  kMatchMap,
  kMatchMapsDirectory,
  kMatchSeed,
  kMatchLobbySeatCount,
  kMatchBots,
  kRoyaleThrustMaximum,
  kRoyaleZoneMinimumRadius,
  kRoyaleZoneShrinkSeconds,
  kRoyaleEliminationGraceSeconds,
  kRoyaleCountdownSeconds,
  kRoyaleRestartDelaySeconds,
  kLobbiesCount,
  kCount,
};

enum class ConfigValueSyntax {
  kSingleValue,
  kCommaDelimitedList,
};

struct ConfigFieldSpec final {
  std::string_view section;
  std::string_view key;
  ConfigValueSyntax value_syntax = ConfigValueSyntax::kSingleValue;
};

constexpr std::array<std::string_view, 8> kConfigSections = {
    "server", "presentation", "simulation", "world", "spatial_grid", "match", "royale", "lobbies"};

// **`[royale]` is required whatever `[match] mode` names.** A mode's balance section is part of
// this deployment's accepted schema rather than of the game it happens to be running today, so
// switching `mode=` is a one-line edit that cannot fail at startup for a section that was never
// written. The values are read only by the mode that owns them
// (`src/gameplay/game_mode_configuration.hpp`). `[lobbies]` is required for the same reason: `1`
// is the single-match server, and a deployment that wants more rooms changes one number.
constexpr std::array<ConfigFieldSpec, static_cast<std::size_t>(ConfigField::kCount)>
    kConfigFieldSpecs = {
        {{"server", "bind_address"},
         {"server", "port"},
         {"server", "allowed_hosts", ConfigValueSyntax::kCommaDelimitedList},
         {"server", "allowed_origins", ConfigValueSyntax::kCommaDelimitedList},
         {"server", "trusted_proxy_addresses", ConfigValueSyntax::kCommaDelimitedList},
         {"presentation", "snapshots_per_second"},
         {"simulation", "ticks_per_second"},
         {"simulation", "drag_per_second"},
         {"world", "width_world_units"},
         {"world", "height_world_units"},
         {"world", "player_radius_world_units"},
         {"spatial_grid", "columns"},
         {"spatial_grid", "rows"},
         {"match", "mode"},
         {"match", "map"},
         {"match", "maps_directory"},
         {"match", "seed"},
         {"match", "lobby_seat_count"},
         {"match", "bots", ConfigValueSyntax::kCommaDelimitedList},
         {"royale", "thrust_max_world_units_per_second_squared"},
         {"royale", "zone_minimum_radius_world_units"},
         {"royale", "zone_shrink_seconds"},
         {"royale", "elimination_grace_seconds"},
         {"royale", "countdown_seconds"},
         {"royale", "restart_delay_seconds"},
         {"lobbies", "count"}}};

// canonical: config_section_family -- the one open name in the configuration schema.
//
// **A section family is a declared section-name prefix whose instance names are open, whose key
// schema is closed and shared by every instance, and whose instances collect into a list instead
// of into a fixed field.** `[hazard.comet]` and `[hazard.boulder]` are two instances of the
// `hazard` family. `comet` and `boulder` appear in no C++ file, which is what makes adding a hazard
// kind a configuration section and nothing else.
//
// **Exactly one thing became open, and everything else stayed shut.** A section matching neither a
// name in `kConfigSections` nor a prefix here is still `SECTION_UNKNOWN`. An unknown key inside
// `[hazard.comet]` is still `KEY_UNKNOWN`, by the same lookup `[royale]` uses. A repeated instance
// name is still `SECTION_DUPLICATE` and a repeated key still `KEY_DUPLICATE`. An instance that
// omits one of its family's keys is still `KEY_MISSING`, because the schema is closed *within* an
// instance even though the set of instances is not. Zero instances is legal and is what every
// configuration in this tree looks like today.
//
// **The instance name's grammar is the domain's rule, not the parser's**, exactly as `[match] mode`
// works: this file rejects only an empty instance name, which is a header it cannot parse, and
// `gameplay::HazardArchetype::create` rejects a name outside `common.schema.json#/$defs/kind_name`
// naming the section it came from. Two copies of that grammar in one library would be the second
// source of truth this concept exists to avoid.
//
// The second customer this seam has to survive is a per-bot roster: `[bot.wanderer]` carrying that
// bot's own tuning is one more prefix in `kConfigSectionFamilies`, one more `ConfigSectionFamily`
// enumerator, and its keys appended to `kConfigFamilyFieldSpecs` -- no new structure, because an
// instance already stores its family alongside its values and every lookup already filters on it.
enum class ConfigSectionFamily : std::size_t {
  kHazard,
  kCount,
};

// The closed key schema every instance of every family answers to, indexed exactly as
// `ConfigField` indexes `kConfigFieldSpecs`. One flat list rather than one list per family, so a
// family's keys are declared the same way a section's are.
enum class ConfigFamilyField : std::size_t {
  kHazardRadius,
  kHazardMass,
  kHazardRestitution,
  kHazardSpeed,
  kHazardSpawnInterval,
  kHazardLethalOnContact,
  kCount,
};

struct ConfigFamilyFieldSpec final {
  ConfigSectionFamily family;
  std::string_view key;
  ConfigValueSyntax value_syntax = ConfigValueSyntax::kSingleValue;
};

// The declared prefixes, in the same closed shape `kConfigSections` has. `hazards` is deliberately
// not a section: there is no family-wide hazard settings block, because the only value it would
// have carried is a spawn interval, and an interval is better per kind
// (`gameplay/shared/hazard_archetype.hpp` states that decision and why `kinds=` is absent too).
constexpr std::array<std::string_view, static_cast<std::size_t>(ConfigSectionFamily::kCount)>
    kConfigSectionFamilies = {"hazard"};

// `[hazard.comet]` is one prefix, one separator, one instance name. The dot is the separator
// because it is already how this loader spells `<section>.<key>` in every diagnostic it writes, so
// `hazard.comet.mass` reads the same way `royale.countdown_seconds` does. No name in
// `kConfigSections` contains one, and a fixed section is resolved first regardless, so a family
// prefix cannot shadow a fixed section however the two lists grow.
constexpr char kConfigSectionFamilySeparator = '.';

// Declared instances a configuration may carry, across every family. The file is already bounded
// at `kMaximumConfigurationFileBytes`, so this is not what stops a large file; it is what stops an
// unbounded list growing from parsed input, which is the same discipline
// `kMaximumBotRosterEntryCount` applies to the roster line.
constexpr std::size_t kMaximumConfigSectionFamilyInstanceCount = 64;

constexpr std::array<ConfigFamilyFieldSpec, static_cast<std::size_t>(ConfigFamilyField::kCount)>
    kConfigFamilyFieldSpecs = {{{ConfigSectionFamily::kHazard, "radius_world_units"},
                                {ConfigSectionFamily::kHazard, "mass"},
                                {ConfigSectionFamily::kHazard, "restitution"},
                                {ConfigSectionFamily::kHazard, "speed_world_units_per_second"},
                                {ConfigSectionFamily::kHazard, "spawn_interval_seconds"},
                                {ConfigSectionFamily::kHazard, "lethal_on_contact"}}};

// One declared `[<family>.<instance>]` section: its family, its open name, and one slot per key of
// the closed schema. The slots a sibling family owns stay empty, which costs a startup-only parse
// a handful of empty optionals and keeps the value shape identical to `StrictIniDocument::values_`
// one level down.
struct ConfigSectionFamilyInstance final {
  ConfigSectionFamily family;
  std::string name;
  std::array<std::optional<std::string>, static_cast<std::size_t>(ConfigFamilyField::kCount)>
      values;
};

// Where the key-value pairs that follow a section header are stored: a fixed section's index into
// `kConfigSections`, or one instance's index into the document's instance list.
struct ConfigSectionCursor final {
  bool is_family_instance = false;
  std::size_t index = 0;
};

[[nodiscard]] std::string_view trim_horizontal_whitespace(std::string_view value) noexcept {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] std::string source_line_context(const std::filesystem::path& source_path,
                                              const std::size_t line_number) {
  std::string context = source_path.string();
  context.append(":");
  context.append(std::to_string(line_number));
  return context;
}

[[nodiscard]] std::string config_field_context(const ConfigField field) {
  const ConfigFieldSpec& field_spec = kConfigFieldSpecs[static_cast<std::size_t>(field)];
  std::string context{field_spec.section};
  context.push_back('.');
  context.append(field_spec.key);
  return context;
}

// The `<family>.<instance>.<key>` form the section is authored in, which is also the context
// `gameplay::HazardArchetype` reports its own rejections under, so one configuration line has one
// spelling wherever it is refused.
[[nodiscard]] std::string config_family_field_context(const ConfigSectionFamilyInstance& instance,
                                                      const ConfigFamilyField field) {
  std::string context{kConfigSectionFamilies[static_cast<std::size_t>(instance.family)]};
  context.push_back(kConfigSectionFamilySeparator);
  context.append(instance.name);
  context.push_back('.');
  context.append(kConfigFamilyFieldSpecs[static_cast<std::size_t>(field)].key);
  return context;
}

[[nodiscard]] constexpr bool permits_explicit_empty_value(const ConfigField field) noexcept {
  return kConfigFieldSpecs[static_cast<std::size_t>(field)].value_syntax ==
         ConfigValueSyntax::kCommaDelimitedList;
}

[[nodiscard]] constexpr bool permits_explicit_empty_value(const ConfigFamilyField field) noexcept {
  return kConfigFamilyFieldSpecs[static_cast<std::size_t>(field)].value_syntax ==
         ConfigValueSyntax::kCommaDelimitedList;
}

// The two rules that hold for every stored value, whichever kind of section holds it: a key may
// occur once, and only a comma-delimited key may be explicitly empty. Written once over both field
// enumerations so a family instance cannot drift into a laxer rule than a fixed section's.
template <typename ConfigFieldEnum>
void store_config_value(std::optional<std::string>& stored_value, const ConfigFieldEnum field,
                        const std::string_view parsed_value,
                        const std::filesystem::path& source_path, const std::size_t line_number) {
  if (stored_value.has_value()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationKeyDuplicate,
                                source_line_context(source_path, line_number),
                                "each configuration key may occur exactly once"};
  }
  if (parsed_value.empty() && !permits_explicit_empty_value(field)) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid,
                                source_line_context(source_path, line_number),
                                "configuration value must not be empty"};
  }
  stored_value.emplace(parsed_value);
}

[[nodiscard]] std::optional<std::size_t> find_section_index(const std::string_view section) {
  for (std::size_t index = 0; index < kConfigSections.size(); ++index) {
    if (kConfigSections[index] == section) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ConfigField> find_config_field(const std::string_view section,
                                                           const std::string_view key) {
  for (std::size_t index = 0; index < kConfigFieldSpecs.size(); ++index) {
    if (kConfigFieldSpecs[index].section == section && kConfigFieldSpecs[index].key == key) {
      return static_cast<ConfigField>(index);
    }
  }
  return std::nullopt;
}

// One resolved `[<family>.<instance>]` header.
struct ConfigSectionFamilyHeader final {
  ConfigSectionFamily family;
  std::string_view instance_name;
};

// Splits a section name at its first separator and resolves the prefix against the declared
// families. Nothing is returned for a name with no separator, an empty instance name, or a prefix
// no family declares, and every one of those stays an unknown section.
[[nodiscard]] std::optional<ConfigSectionFamilyHeader>
find_section_family(const std::string_view section) {
  const std::size_t separator = section.find(kConfigSectionFamilySeparator);
  if (separator == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view prefix = section.substr(0, separator);
  const std::string_view instance_name = section.substr(separator + 1);
  if (instance_name.empty()) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < kConfigSectionFamilies.size(); ++index) {
    if (kConfigSectionFamilies[index] == prefix) {
      return ConfigSectionFamilyHeader{static_cast<ConfigSectionFamily>(index), instance_name};
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ConfigFamilyField>
find_config_family_field(const ConfigSectionFamily family, const std::string_view key) {
  for (std::size_t index = 0; index < kConfigFamilyFieldSpecs.size(); ++index) {
    if (kConfigFamilyFieldSpecs[index].family == family &&
        kConfigFamilyFieldSpecs[index].key == key) {
      return static_cast<ConfigFamilyField>(index);
    }
  }
  return std::nullopt;
}

class StrictIniDocument final {
public:
  [[nodiscard]] static StrictIniDocument parse(const std::string_view contents,
                                               const std::filesystem::path& source_path) {
    StrictIniDocument document;
    document.parse_lines(contents, source_path);
    document.require_all_fields();
    return document;
  }

  [[nodiscard]] std::string_view value(const ConfigField field) const noexcept {
    return *values_[static_cast<std::size_t>(field)];
  }

  // Every declared family instance, in the order the file declares them. A caller reads the ones
  // whose family it owns and ignores the rest, which is what keeps one parse serving every family.
  [[nodiscard]] std::span<const ConfigSectionFamilyInstance> family_instances() const noexcept {
    return family_instances_;
  }

private:
  void parse_lines(const std::string_view contents, const std::filesystem::path& source_path) {
    std::optional<ConfigSectionCursor> current_section;
    std::size_t line_start = 0;
    std::size_t line_number = 1;

    while (line_start < contents.size()) {
      const std::size_t line_end = contents.find('\n', line_start);
      const std::size_t line_length =
          line_end == std::string_view::npos ? contents.size() - line_start : line_end - line_start;
      std::string_view line = contents.substr(line_start, line_length);
      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }
      parse_line(line, source_path, line_number, current_section);

      if (line_end == std::string_view::npos) {
        break;
      }
      line_start = line_end + 1;
      ++line_number;
    }
  }

  void parse_line(std::string_view line, const std::filesystem::path& source_path,
                  const std::size_t line_number,
                  std::optional<ConfigSectionCursor>& current_section) {
    line = trim_horizontal_whitespace(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      return;
    }

    if (line.front() == '[') {
      if (line.size() < 3 || line.back() != ']' || line.find(']') != line.size() - 1) {
        throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSyntaxInvalid,
                                    source_line_context(source_path, line_number),
                                    "section header must have the exact form [lower_case_name]"};
      }
      current_section = open_section(line.substr(1, line.size() - 2), source_path, line_number);
      return;
    }

    if (!current_section.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSyntaxInvalid,
                                  source_line_context(source_path, line_number),
                                  "a key-value pair must appear inside a known section"};
    }

    const std::size_t delimiter = line.find('=');
    if (delimiter == std::string_view::npos ||
        line.find('=', delimiter + 1) != std::string_view::npos) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSyntaxInvalid,
                                  source_line_context(source_path, line_number),
                                  "a key-value pair must contain exactly one equals sign"};
    }

    const std::string_view key = trim_horizontal_whitespace(line.substr(0, delimiter));
    if (key.empty()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "configuration keys must not be empty"};
    }
    const std::string_view parsed_value = trim_horizontal_whitespace(line.substr(delimiter + 1));

    if (current_section->is_family_instance) {
      ConfigSectionFamilyInstance& instance = family_instances_[current_section->index];
      const std::optional<ConfigFamilyField> field = find_config_family_field(instance.family, key);
      if (!field.has_value()) {
        throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationKeyUnknown,
                                    source_line_context(source_path, line_number),
                                    "key is not valid in its section"};
      }
      store_config_value(instance.values[static_cast<std::size_t>(*field)], *field, parsed_value,
                         source_path, line_number);
      return;
    }

    const std::string_view section = kConfigSections[current_section->index];
    const std::optional<ConfigField> field = find_config_field(section, key);
    if (!field.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationKeyUnknown,
                                  source_line_context(source_path, line_number),
                                  "key is not valid in its section"};
    }
    store_config_value(values_[static_cast<std::size_t>(*field)], *field, parsed_value, source_path,
                       line_number);
  }

  // Resolves one section header against the two closed lists in turn: the fixed section names, then
  // the declared family prefixes. A header matching neither is a rejection, so the only name this
  // schema leaves open is a family instance's.
  [[nodiscard]] ConfigSectionCursor open_section(const std::string_view section,
                                                 const std::filesystem::path& source_path,
                                                 const std::size_t line_number) {
    if (const std::optional<std::size_t> section_index = find_section_index(section);
        section_index.has_value()) {
      if (section_seen_[*section_index]) {
        throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSectionDuplicate,
                                    source_line_context(source_path, line_number),
                                    "each section may occur exactly once"};
      }
      section_seen_[*section_index] = true;
      return ConfigSectionCursor{false, *section_index};
    }

    const std::optional<ConfigSectionFamilyHeader> family_header = find_section_family(section);
    if (!family_header.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSectionUnknown,
                                  source_line_context(source_path, line_number),
                                  "section is not part of the accepted configuration schema"};
    }
    for (const ConfigSectionFamilyInstance& declared : family_instances_) {
      if (declared.family == family_header->family &&
          declared.name == family_header->instance_name) {
        throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSectionDuplicate,
                                    source_line_context(source_path, line_number),
                                    "each section may occur exactly once"};
      }
    }
    if (family_instances_.size() >= kMaximumConfigSectionFamilyInstanceCount) {
      std::string detail{"a configuration may declare at most "};
      detail.append(std::to_string(kMaximumConfigSectionFamilyInstanceCount));
      detail.append(" family instance sections in total");
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSectionLimitExceeded,
                                  source_line_context(source_path, line_number), std::move(detail)};
    }
    family_instances_.push_back(
        ConfigSectionFamilyInstance{.family = family_header->family,
                                    .name = std::string{family_header->instance_name},
                                    .values = {}});
    return ConfigSectionCursor{true, family_instances_.size() - 1};
  }

  void require_all_fields() const {
    std::string missing_fields;
    const auto note_missing_field = [&missing_fields](const std::string& context) {
      if (!missing_fields.empty()) {
        missing_fields.append(", ");
      }
      missing_fields.append(context);
    };
    for (std::size_t index = 0; index < values_.size(); ++index) {
      if (!values_[index].has_value()) {
        note_missing_field(config_field_context(static_cast<ConfigField>(index)));
      }
    }
    // The key schema is closed *within* an instance even though the set of instances is not, so a
    // section that declares a kind and forgets one of its numbers is the same rejection a fixed
    // section gets for the same mistake.
    for (const ConfigSectionFamilyInstance& instance : family_instances_) {
      for (std::size_t index = 0; index < kConfigFamilyFieldSpecs.size(); ++index) {
        if (kConfigFamilyFieldSpecs[index].family != instance.family ||
            instance.values[index].has_value()) {
          continue;
        }
        note_missing_field(
            config_family_field_context(instance, static_cast<ConfigFamilyField>(index)));
      }
    }
    if (!missing_fields.empty()) {
      std::string detail{"required keys are missing: "};
      detail.append(missing_fields);
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationKeyMissing,
                                  "configuration", std::move(detail)};
    }
  }

  std::array<bool, kConfigSections.size()> section_seen_{};
  std::array<std::optional<std::string>, static_cast<std::size_t>(ConfigField::kCount)> values_{};
  std::vector<ConfigSectionFamilyInstance> family_instances_{};
};

// The three scalar syntaxes, over the parsed text and the context that names where it came from.
// They take a context rather than a `ConfigField` because a family instance's value has no
// `ConfigField` to name it, and one number parser serving both kinds of section is what keeps a
// `[hazard.comet]` number as strictly parsed as a `[royale]` one.
[[nodiscard]] std::uint64_t parse_unsigned_value(const std::string_view text,
                                                 const std::string& context) {
  std::uint64_t value = 0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parse_error == std::errc::result_out_of_range) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueOutOfRange, context,
                                "integer value is outside the uint64 range"};
  }
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid, context,
                                "value must be an unsigned base-10 integer"};
  }
  return value;
}

[[nodiscard]] double parse_double_value(const std::string_view text, const std::string& context) {
  double value = 0.0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
  if (parse_error == std::errc::result_out_of_range) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueOutOfRange, context,
                                "floating-point value is outside the binary64 range"};
  }
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid, context,
                                "value must be one complete decimal number"};
  }
  return value;
}

// The one boolean spelling this schema accepts. `1`, `yes`, `on`, and `True` are all rejected,
// because a schema that accepts four spellings of one value is four ways for two deployments to
// read differently while meaning the same thing.
[[nodiscard]] bool parse_boolean_value(const std::string_view text, const std::string& context) {
  if (text == "true") {
    return true;
  }
  if (text == "false") {
    return false;
  }
  throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid, context,
                              "value must be exactly true or false"};
}

[[nodiscard]] std::uint64_t parse_unsigned_config_value(const StrictIniDocument& document,
                                                        const ConfigField field) {
  return parse_unsigned_value(document.value(field), config_field_context(field));
}

[[nodiscard]] double parse_double_config_value(const StrictIniDocument& document,
                                               const ConfigField field) {
  return parse_double_value(document.value(field), config_field_context(field));
}

[[nodiscard]] double parse_double_family_value(const ConfigSectionFamilyInstance& instance,
                                               const ConfigFamilyField field) {
  return parse_double_value(*instance.values[static_cast<std::size_t>(field)],
                            config_family_field_context(instance, field));
}

[[nodiscard]] bool parse_boolean_family_value(const ConfigSectionFamilyInstance& instance,
                                              const ConfigFamilyField field) {
  return parse_boolean_value(*instance.values[static_cast<std::size_t>(field)],
                             config_family_field_context(instance, field));
}

[[nodiscard]] std::vector<std::string>
parse_comma_delimited_config_value(const StrictIniDocument& document, const ConfigField field) {
  const std::string_view text = document.value(field);
  if (text.empty()) {
    return {};
  }

  std::vector<std::string> entries;
  std::size_t entry_start = 0;
  while (entry_start <= text.size()) {
    const std::size_t delimiter = text.find(',', entry_start);
    const std::size_t entry_end = delimiter == std::string_view::npos ? text.size() : delimiter;
    entries.emplace_back(
        trim_horizontal_whitespace(text.substr(entry_start, entry_end - entry_start)));
    if (delimiter == std::string_view::npos) {
      break;
    }
    entry_start = delimiter + 1;
  }
  return entries;
}

// Every declared `[hazard.<kind>]` section, validated by the gameplay value that owns the rules.
// The kind is the section's own instance name, so **no name in this file, or in any other C++ file,
// decides which hazard kinds exist**: this function would build a table of a hundred kinds without
// being edited. Zero instances yields an empty table, which is a configuration with no hazards
// rather than a rejection.
[[nodiscard]] std::vector<gameplay::HazardArchetype>
parse_hazard_archetypes(const StrictIniDocument& document) {
  std::vector<gameplay::HazardArchetype> archetypes;
  for (const ConfigSectionFamilyInstance& instance : document.family_instances()) {
    if (instance.family != ConfigSectionFamily::kHazard) {
      continue;
    }
    archetypes.push_back(gameplay::HazardArchetype::create(gameplay::HazardArchetype::Section{
        .kind_name = instance.name,
        .radius_world_units = parse_double_family_value(instance, ConfigFamilyField::kHazardRadius),
        .mass = parse_double_family_value(instance, ConfigFamilyField::kHazardMass),
        .restitution = parse_double_family_value(instance, ConfigFamilyField::kHazardRestitution),
        .speed_world_units_per_second =
            parse_double_family_value(instance, ConfigFamilyField::kHazardSpeed),
        .spawn_interval_seconds =
            parse_double_family_value(instance, ConfigFamilyField::kHazardSpawnInterval),
        .lethal_on_contact =
            parse_boolean_family_value(instance, ConfigFamilyField::kHazardLethalOnContact)}));
  }
  return archetypes;
}

[[noreturn]] void throw_invalid_command_line(const std::string_view detail) {
  throw ApplicationInputError{ApplicationInputErrorCode::kCommandLineInvalid, "command_line",
                              std::string{detail}};
}

} // namespace

ApplicationConfigLoader::RunRequest::RunRequest(ApplicationConfig application_config,
                                                std::optional<std::filesystem::path> scenario_path)
    : application_config_(std::move(application_config)), scenario_path_(std::move(scenario_path)) {
  if (scenario_path_.has_value() && scenario_path_->empty()) {
    throw_invalid_command_line("scenario path must not be empty");
  }
}

ApplicationConfigLoader::Result ApplicationConfigLoader::load(const int argument_count,
                                                              const char* const arguments[]) {
  if (argument_count < 1 || arguments == nullptr) {
    throw_invalid_command_line("argument vector is missing");
  }
  for (int index = 0; index < argument_count; ++index) {
    if (arguments[index] == nullptr) {
      throw_invalid_command_line("argument vector contains a null entry");
    }
  }

  if (argument_count == 2 && std::string_view{arguments[1]} == "--help") {
    return HelpRequest{};
  }

  // `--scenario` is optional: a match is fully described by `[match]` and the map it names, and a
  // scenario only seeds extra entities on top of that, which is what fixtures need and a live
  // deployment does not.
  const bool has_scenario = argument_count == 5;
  if ((argument_count != 3 && argument_count != 5) ||
      std::string_view{arguments[1]} != "--config" ||
      (has_scenario && std::string_view{arguments[3]} != "--scenario")) {
    throw_invalid_command_line(
        "expected only --help, --config <path>, or --config <path> --scenario <path>");
  }
  if (std::string_view{arguments[2]}.empty()) {
    throw_invalid_command_line("configuration path must not be empty");
  }
  if (has_scenario && std::string_view{arguments[4]}.empty()) {
    throw_invalid_command_line("scenario path must not be empty");
  }

  const std::filesystem::path configuration_path{arguments[2]};
  std::optional<std::filesystem::path> scenario_path;
  if (has_scenario) {
    scenario_path.emplace(arguments[4]);
  }
  const std::string configuration_contents = read_application_text_file(
      configuration_path, ApplicationTextFileKind::kConfiguration, kMaximumConfigurationFileBytes);
  const StrictIniDocument document =
      StrictIniDocument::parse(configuration_contents, configuration_path);

  const double world_width = parse_double_config_value(document, ConfigField::kWorldWidth);
  const double world_height = parse_double_config_value(document, ConfigField::kWorldHeight);
  const double player_radius = parse_double_config_value(document, ConfigField::kWorldPlayerRadius);

  simulation::SimulationConfig simulation_config = simulation::SimulationConfig::create(
      world_width, world_height, player_radius,
      parse_unsigned_config_value(document, ConfigField::kSimulationTicksPerSecond),
      parse_unsigned_config_value(document, ConfigField::kSpatialGridColumns),
      parse_unsigned_config_value(document, ConfigField::kSpatialGridRows),
      parse_double_config_value(document, ConfigField::kSimulationDragPerSecond));

  server::ServerConfig server_config = server::ServerConfig::create(
      std::string{document.value(ConfigField::kServerBindAddress)},
      parse_unsigned_config_value(document, ConfigField::kServerPort),
      parse_unsigned_config_value(document, ConfigField::kPresentationSnapshotsPerSecond),
      world_width, world_height, player_radius,
      parse_comma_delimited_config_value(document, ConfigField::kServerAllowedHosts),
      parse_comma_delimited_config_value(document, ConfigField::kServerAllowedOrigins),
      parse_comma_delimited_config_value(document, ConfigField::kServerTrustedProxyAddresses));

  MatchConfiguration match_configuration = MatchConfiguration::create(
      std::string{document.value(ConfigField::kMatchMode)},
      std::string{document.value(ConfigField::kMatchMap)},
      std::filesystem::path{document.value(ConfigField::kMatchMapsDirectory)},
      parse_unsigned_config_value(document, ConfigField::kMatchSeed),
      parse_unsigned_config_value(document, ConfigField::kMatchLobbySeatCount),
      MatchConfiguration::parse_bot_roster(document.value(ConfigField::kMatchBots)));

  // Validated by the mode that owns the section, so the application never re-derives a balance
  // rule: the section is authored in seconds and world units and comes back in tick counts. The
  // hazard table is validated the same way by the mechanic that owns it, and the two are written as
  // one aggregate initialization so `[royale]` is always resolved before the hazards, whose
  // rejections would otherwise arrive in an order the standard does not fix.
  gameplay::GameModeConfiguration game_mode_configuration{
      gameplay::RoyaleConfiguration::create(gameplay::RoyaleConfiguration::Section{
          .thrust_max_world_units_per_second_squared =
              parse_double_config_value(document, ConfigField::kRoyaleThrustMaximum),
          .zone_minimum_radius_world_units =
              parse_double_config_value(document, ConfigField::kRoyaleZoneMinimumRadius),
          .zone_shrink_seconds =
              parse_double_config_value(document, ConfigField::kRoyaleZoneShrinkSeconds),
          .elimination_grace_seconds =
              parse_double_config_value(document, ConfigField::kRoyaleEliminationGraceSeconds),
          .countdown_seconds =
              parse_double_config_value(document, ConfigField::kRoyaleCountdownSeconds),
          .restart_delay_seconds =
              parse_double_config_value(document, ConfigField::kRoyaleRestartDelaySeconds)}),
      parse_hazard_archetypes(document)};

  const LobbiesConfiguration lobbies_configuration = LobbiesConfiguration::create(
      parse_unsigned_config_value(document, ConfigField::kLobbiesCount));

  return RunRequest{ApplicationConfig::create(
                        std::move(server_config), simulation_config, std::move(match_configuration),
                        std::move(game_mode_configuration), lobbies_configuration),
                    std::move(scenario_path)};
}

} // namespace blob_royale::application
