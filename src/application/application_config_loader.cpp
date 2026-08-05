#include "application_config_loader.hpp"

#include "application_input_error.hpp"
#include "application_text_file_reader.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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
  kWorldWidth,
  kWorldHeight,
  kWorldPlayerRadius,
  kSpatialGridColumns,
  kSpatialGridRows,
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

constexpr std::array<std::string_view, 5> kConfigSections = {"server", "presentation", "simulation",
                                                             "world", "spatial_grid"};

constexpr std::array<ConfigFieldSpec, static_cast<std::size_t>(ConfigField::kCount)>
    kConfigFieldSpecs = {
        {{"server", "bind_address"},
         {"server", "port"},
         {"server", "allowed_hosts", ConfigValueSyntax::kCommaDelimitedList},
         {"server", "allowed_origins", ConfigValueSyntax::kCommaDelimitedList},
         {"server", "trusted_proxy_addresses", ConfigValueSyntax::kCommaDelimitedList},
         {"presentation", "snapshots_per_second"},
         {"simulation", "ticks_per_second"},
         {"world", "width_world_units"},
         {"world", "height_world_units"},
         {"world", "player_radius_world_units"},
         {"spatial_grid", "columns"},
         {"spatial_grid", "rows"}}};

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

[[nodiscard]] constexpr bool permits_explicit_empty_value(const ConfigField field) noexcept {
  return kConfigFieldSpecs[static_cast<std::size_t>(field)].value_syntax ==
         ConfigValueSyntax::kCommaDelimitedList;
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

private:
  void parse_lines(const std::string_view contents, const std::filesystem::path& source_path) {
    std::optional<std::size_t> current_section;
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
                  const std::size_t line_number, std::optional<std::size_t>& current_section) {
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
      const std::string_view section = line.substr(1, line.size() - 2);
      const std::optional<std::size_t> section_index = find_section_index(section);
      if (!section_index.has_value()) {
        throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSectionUnknown,
                                    source_line_context(source_path, line_number),
                                    "section is not part of the accepted configuration schema"};
      }
      if (section_seen_[*section_index]) {
        throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationSectionDuplicate,
                                    source_line_context(source_path, line_number),
                                    "each section may occur exactly once"};
      }
      section_seen_[*section_index] = true;
      current_section = section_index;
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

    const std::string_view section = kConfigSections[*current_section];
    const std::optional<ConfigField> field = find_config_field(section, key);
    if (!field.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationKeyUnknown,
                                  source_line_context(source_path, line_number),
                                  "key is not valid in its section"};
    }

    std::optional<std::string>& stored_value = values_[static_cast<std::size_t>(*field)];
    if (stored_value.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationKeyDuplicate,
                                  source_line_context(source_path, line_number),
                                  "each configuration key may occur exactly once"};
    }

    const std::string_view parsed_value = trim_horizontal_whitespace(line.substr(delimiter + 1));
    if (parsed_value.empty() && !permits_explicit_empty_value(*field)) {
      throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "configuration value must not be empty"};
    }
    stored_value.emplace(parsed_value);
  }

  void require_all_fields() const {
    std::string missing_fields;
    for (std::size_t index = 0; index < values_.size(); ++index) {
      if (!values_[index].has_value()) {
        const auto field = static_cast<ConfigField>(index);
        if (!missing_fields.empty()) {
          missing_fields.append(", ");
        }
        missing_fields.append(config_field_context(field));
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
};

[[nodiscard]] std::uint64_t parse_unsigned_config_value(const StrictIniDocument& document,
                                                        const ConfigField field) {
  const std::string_view text = document.value(field);
  std::uint64_t value = 0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parse_error == std::errc::result_out_of_range) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueOutOfRange,
                                config_field_context(field),
                                "integer value is outside the uint64 range"};
  }
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid,
                                config_field_context(field),
                                "value must be an unsigned base-10 integer"};
  }
  return value;
}

[[nodiscard]] double parse_double_config_value(const StrictIniDocument& document,
                                               const ConfigField field) {
  const std::string_view text = document.value(field);
  double value = 0.0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
  if (parse_error == std::errc::result_out_of_range) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueOutOfRange,
                                config_field_context(field),
                                "floating-point value is outside the binary64 range"};
  }
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kConfigurationValueInvalid,
                                config_field_context(field),
                                "value must be one complete decimal number"};
  }
  return value;
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

[[noreturn]] void throw_invalid_command_line(const std::string_view detail) {
  throw ApplicationInputError{ApplicationInputErrorCode::kCommandLineInvalid, "command_line",
                              std::string{detail}};
}

} // namespace

ApplicationConfigLoader::RunRequest::RunRequest(ApplicationConfig application_config,
                                                std::filesystem::path scenario_path)
    : application_config_(std::move(application_config)), scenario_path_(std::move(scenario_path)) {
  if (scenario_path_.empty()) {
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

  if (argument_count != 5 || std::string_view{arguments[1]} != "--config" ||
      std::string_view{arguments[3]} != "--scenario") {
    throw_invalid_command_line("expected only --help or exactly --config <path> --scenario <path>");
  }
  if (std::string_view{arguments[2]}.empty() || std::string_view{arguments[4]}.empty()) {
    throw_invalid_command_line("configuration and scenario paths must not be empty");
  }

  const std::filesystem::path configuration_path{arguments[2]};
  const std::filesystem::path scenario_path{arguments[4]};
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
      parse_unsigned_config_value(document, ConfigField::kSpatialGridRows));

  server::ServerConfig server_config = server::ServerConfig::create(
      std::string{document.value(ConfigField::kServerBindAddress)},
      parse_unsigned_config_value(document, ConfigField::kServerPort),
      parse_unsigned_config_value(document, ConfigField::kPresentationSnapshotsPerSecond),
      world_width, world_height, player_radius,
      parse_comma_delimited_config_value(document, ConfigField::kServerAllowedHosts),
      parse_comma_delimited_config_value(document, ConfigField::kServerAllowedOrigins),
      parse_comma_delimited_config_value(document, ConfigField::kServerTrustedProxyAddresses));

  return RunRequest{ApplicationConfig::create(std::move(server_config), simulation_config),
                    scenario_path};
}

} // namespace blob_royale::application
