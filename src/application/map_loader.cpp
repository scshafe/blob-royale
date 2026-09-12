#include "map_loader.hpp"

#include "application_input_error.hpp"
#include "application_text_file_reader.hpp"
#include "contact_effect_admission.hpp"
#include "map_definition.hpp"
#include "physics_body.hpp"
#include "team_id.hpp"
#include "terrain_definition.hpp"
#include "vector2.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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

constexpr std::string_view kMapConfigurationFileName = "map.cfg";
constexpr std::string_view kStaticBodiesFileName = "static_bodies.csv";
constexpr std::string_view kMarkersFileName = "markers.csv";

constexpr std::string_view kExpectedStaticBodiesHeader =
    "position_x_world_units,position_y_world_units,collision_layer,collision_mask,contact_effect_"
    "policy";
constexpr std::string_view kExpectedMarkersHeader =
    "marker_kind,position_x_world_units,position_y_world_units,team_id";

enum class MapConfigField : std::size_t {
  kMapName,
  kMapDisplayName,
  kBoundsWidth,
  kBoundsHeight,
  kTerrainGround,
  kCount,
};

struct MapConfigFieldSpec final {
  std::string_view section;
  std::string_view key;
};

constexpr std::array<std::string_view, 3> kMapConfigSections = {"map", "bounds", "terrain"};

constexpr std::array<MapConfigFieldSpec, static_cast<std::size_t>(MapConfigField::kCount)>
    kMapConfigFieldSpecs = {{{"map", "name"},
                             {"map", "display_name"},
                             {"bounds", "width_world_units"},
                             {"bounds", "height_world_units"},
                             {"terrain", "ground"}}};

// The server loader's section-family convention, kept private to this map grammar: the family
// prefixes and each family's keys are closed, but the instance name belongs to the terrain value
// factory. There is no second parser-owned spelling of the domain's identity grammar.
enum class MapSectionFamily : std::size_t { kCorridor, kHole, kCount };
enum class MapFamilyField : std::size_t {
  kCorridorHalfWidth,
  kCorridorPoints,
  kHoleCenterX,
  kHoleCenterY,
  kHoleRadius,
  kCount,
};
struct MapFamilyFieldSpec final {
  MapSectionFamily family;
  std::string_view key;
};
constexpr std::array<std::string_view, static_cast<std::size_t>(MapSectionFamily::kCount)>
    kMapSectionFamilyPrefixes = {"terrain.corridor.", "terrain.hole."};
constexpr std::array<MapFamilyFieldSpec, static_cast<std::size_t>(MapFamilyField::kCount)>
    kMapFamilyFieldSpecs = {{{MapSectionFamily::kCorridor, "half_width_world_units"},
                             {MapSectionFamily::kCorridor, "points_world_units"},
                             {MapSectionFamily::kHole, "center_x_world_units"},
                             {MapSectionFamily::kHole, "center_y_world_units"},
                             {MapSectionFamily::kHole, "radius_world_units"}}};
struct MapSectionFamilyInstance final {
  MapSectionFamily family;
  std::string name;
  std::array<std::optional<std::string>, static_cast<std::size_t>(MapFamilyField::kCount)> values;

  [[nodiscard]] std::string_view value(const MapFamilyField field) const noexcept {
    return *values[static_cast<std::size_t>(field)];
  }
};
struct MapSectionCursor final {
  bool is_family_instance = false;
  std::size_t index = 0;
};

[[nodiscard]] std::string family_field_context(const MapSectionFamilyInstance& instance,
                                               const MapFamilyField field) {
  std::string context{kMapSectionFamilyPrefixes[static_cast<std::size_t>(instance.family)]};
  context.append(instance.name);
  context.push_back('.');
  context.append(kMapFamilyFieldSpecs[static_cast<std::size_t>(field)].key);
  return context;
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

[[nodiscard]] std::string source_line_context(const std::filesystem::path& source_path,
                                              const std::size_t line_number) {
  std::string context = source_path.string();
  context.push_back(':');
  context.append(std::to_string(line_number));
  return context;
}

[[nodiscard]] std::optional<std::size_t> find_map_config_section(const std::string_view section) {
  for (std::size_t index = 0; index < kMapConfigSections.size(); ++index) {
    if (kMapConfigSections[index] == section) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<MapConfigField> find_map_config_field(const std::string_view section,
                                                                  const std::string_view key) {
  for (std::size_t index = 0; index < kMapConfigFieldSpecs.size(); ++index) {
    if (kMapConfigFieldSpecs[index].section == section && kMapConfigFieldSpecs[index].key == key) {
      return static_cast<MapConfigField>(index);
    }
  }
  return std::nullopt;
}

// The strict INI reader for one map's `map.cfg`. It is the same grammar
// `application_config_loader.cpp` applies to the server configuration -- one section header per
// line, exactly one equals sign, no duplicate section or key, no unknown section or key, no empty
// value, every declared key required -- over this file's own fixed and family-field tables. The
// 64 KiB acquisition bound also bounds temporary family and point lists; the terrain factories own
// their tighter count limits, so parsing does not duplicate domain validation.
class StrictMapIniDocument final {
public:
  [[nodiscard]] static StrictMapIniDocument parse(const std::string_view contents,
                                                  const std::filesystem::path& source_path) {
    StrictMapIniDocument document;
    document.parse_lines(contents, source_path);
    document.require_all_fields(source_path);
    return document;
  }

  [[nodiscard]] std::string_view value(const MapConfigField field) const noexcept {
    return *values_[static_cast<std::size_t>(field)];
  }

  [[nodiscard]] std::span<const MapSectionFamilyInstance> family_instances() const noexcept {
    return family_instances_;
  }

private:
  void parse_lines(const std::string_view contents, const std::filesystem::path& source_path) {
    std::optional<MapSectionCursor> current_section;
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
                  const std::size_t line_number, std::optional<MapSectionCursor>& current_section) {
    line = trim_horizontal_whitespace(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      return;
    }

    if (line.front() == '[') {
      if (line.size() < 3 || line.back() != ']' || line.find(']') != line.size() - 1) {
        throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                    source_line_context(source_path, line_number),
                                    "section header must have the exact form [lower_case_name]"};
      }
      current_section = open_section(line.substr(1, line.size() - 2), source_path, line_number);
      return;
    }

    if (!current_section.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "a key-value pair must appear inside a known section"};
    }

    const std::size_t delimiter = line.find('=');
    if (delimiter == std::string_view::npos ||
        line.find('=', delimiter + 1) != std::string_view::npos) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "a key-value pair must contain exactly one equals sign"};
    }

    const std::string_view key = trim_horizontal_whitespace(line.substr(0, delimiter));
    const std::string_view parsed_value = trim_horizontal_whitespace(line.substr(delimiter + 1));
    if (current_section->is_family_instance) {
      MapSectionFamilyInstance& instance = family_instances_[current_section->index];
      for (std::size_t index = 0; index < kMapFamilyFieldSpecs.size(); ++index) {
        if (kMapFamilyFieldSpecs[index].family == instance.family &&
            kMapFamilyFieldSpecs[index].key == key) {
          store_value(instance.values[index], parsed_value, source_path, line_number);
          return;
        }
      }
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "key is not valid in its section"};
    }
    const std::optional<MapConfigField> field =
        find_map_config_field(kMapConfigSections[current_section->index], key);
    if (!field.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "key is not valid in its section"};
    }

    store_value(values_[static_cast<std::size_t>(*field)], parsed_value, source_path, line_number);
  }

  static void store_value(std::optional<std::string>& stored_value,
                          const std::string_view parsed_value,
                          const std::filesystem::path& source_path, const std::size_t line_number) {
    if (stored_value.has_value()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "each map key may occur exactly once"};
    }

    if (parsed_value.empty()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                  source_line_context(source_path, line_number),
                                  "map value must not be empty"};
    }
    stored_value.emplace(parsed_value);
  }

  [[nodiscard]] MapSectionCursor open_section(const std::string_view section,
                                              const std::filesystem::path& source_path,
                                              const std::size_t line_number) {
    if (const std::optional<std::size_t> index = find_map_config_section(section);
        index.has_value()) {
      if (section_seen_[*index]) {
        throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                    source_line_context(source_path, line_number),
                                    "each section may occur exactly once"};
      }
      section_seen_[*index] = true;
      return MapSectionCursor{false, *index};
    }
    for (std::size_t index = 0; index < kMapSectionFamilyPrefixes.size(); ++index) {
      const std::string_view prefix = kMapSectionFamilyPrefixes[index];
      if (!section.starts_with(prefix) || section.size() == prefix.size()) {
        continue;
      }
      const auto family = static_cast<MapSectionFamily>(index);
      const std::string_view name = section.substr(prefix.size());
      for (const MapSectionFamilyInstance& instance : family_instances_) {
        if (instance.family == family && instance.name == name) {
          throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                      source_line_context(source_path, line_number),
                                      "each section may occur exactly once"};
        }
      }
      family_instances_.push_back(MapSectionFamilyInstance{family, std::string{name}, {}});
      return MapSectionCursor{true, family_instances_.size() - 1};
    }
    throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                source_line_context(source_path, line_number),
                                "section is not part of the accepted map schema"};
  }

  void require_all_fields(const std::filesystem::path& source_path) const {
    std::string missing_fields;
    for (std::size_t index = 0; index < values_.size(); ++index) {
      if (values_[index].has_value()) {
        continue;
      }
      if (!missing_fields.empty()) {
        missing_fields.append(", ");
      }
      missing_fields.append(kMapConfigFieldSpecs[index].section);
      missing_fields.push_back('.');
      missing_fields.append(kMapConfigFieldSpecs[index].key);
    }
    for (const MapSectionFamilyInstance& instance : family_instances_) {
      for (std::size_t index = 0; index < kMapFamilyFieldSpecs.size(); ++index) {
        if (kMapFamilyFieldSpecs[index].family != instance.family ||
            instance.values[index].has_value()) {
          continue;
        }
        if (!missing_fields.empty()) {
          missing_fields.append(", ");
        }
        missing_fields.append(family_field_context(instance, static_cast<MapFamilyField>(index)));
      }
    }
    if (!missing_fields.empty()) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid, source_path.string(),
                                  "required map keys are missing: " + missing_fields};
    }
  }

  std::array<bool, kMapConfigSections.size()> section_seen_{};
  std::array<std::optional<std::string>, static_cast<std::size_t>(MapConfigField::kCount)>
      values_{};
  std::vector<MapSectionFamilyInstance> family_instances_{};
};

[[nodiscard]] double parse_map_double(const std::string_view text, const std::string& context) {
  double value = 0.0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid, context,
                                "value must be one complete decimal number"};
  }
  if (!std::isfinite(value)) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid, context,
                                "value must be finite"};
  }
  return value;
}

[[nodiscard]] std::uint64_t parse_map_unsigned(const std::string_view text,
                                               const std::string& context) {
  std::uint64_t value = 0;
  const auto [parse_end, parse_error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parse_error != std::errc{} || parse_end != text.data() + text.size()) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid, context,
                                "value must be an unsigned base-10 integer"};
  }
  return value;
}

[[nodiscard]] std::vector<simulation::Vector2> parse_corridor_points(const std::string_view text,
                                                                     const std::string& context) {
  std::vector<simulation::Vector2> points;
  std::size_t point_start = 0;
  while (point_start <= text.size()) {
    const std::size_t delimiter = text.find(';', point_start);
    const std::size_t point_end = delimiter == std::string_view::npos ? text.size() : delimiter;
    const std::string_view point =
        trim_horizontal_whitespace(text.substr(point_start, point_end - point_start));
    const std::size_t comma = point.find(',');
    if (comma == std::string_view::npos || point.find(',', comma + 1) != std::string_view::npos) {
      throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid, context,
                                  "points must be semicolon-separated x,y pairs"};
    }
    points.push_back(simulation::Vector2::create(
        parse_map_double(trim_horizontal_whitespace(point.substr(0, comma)), context),
        parse_map_double(trim_horizontal_whitespace(point.substr(comma + 1)), context)));
    if (delimiter == std::string_view::npos) {
      break;
    }
    point_start = delimiter + 1;
  }
  return points;
}

[[nodiscard]] simulation::TerrainDefinition load_terrain(const StrictMapIniDocument& document,
                                                         const simulation::ArenaBounds bounds,
                                                         const std::filesystem::path& source_path) {
  const std::string_view ground_text = document.value(MapConfigField::kTerrainGround);
  simulation::TerrainGround ground;
  if (ground_text == "solid") {
    ground = simulation::TerrainGround::kSolid;
  } else if (ground_text == "corridors") {
    ground = simulation::TerrainGround::kCorridors;
  } else {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid,
                                source_path.string() + ":terrain.ground",
                                "terrain ground must be solid or corridors"};
  }

  std::vector<simulation::TerrainCorridor> corridors;
  std::vector<simulation::TerrainHole> holes;
  for (const MapSectionFamilyInstance& instance : document.family_instances()) {
    const auto context = [&instance, &source_path](const MapFamilyField field) {
      return source_path.string() + ":" + family_field_context(instance, field);
    };
    if (instance.family == MapSectionFamily::kCorridor) {
      corridors.push_back(simulation::TerrainCorridor::create(
          instance.name,
          parse_map_double(instance.value(MapFamilyField::kCorridorHalfWidth),
                           context(MapFamilyField::kCorridorHalfWidth)),
          parse_corridor_points(instance.value(MapFamilyField::kCorridorPoints),
                                context(MapFamilyField::kCorridorPoints))));
    } else {
      holes.push_back(simulation::TerrainHole::create(
          instance.name,
          simulation::Vector2::create(parse_map_double(instance.value(MapFamilyField::kHoleCenterX),
                                                       context(MapFamilyField::kHoleCenterX)),
                                      parse_map_double(instance.value(MapFamilyField::kHoleCenterY),
                                                       context(MapFamilyField::kHoleCenterY))),
          parse_map_double(instance.value(MapFamilyField::kHoleRadius),
                           context(MapFamilyField::kHoleRadius))));
    }
  }
  return simulation::TerrainDefinition::create(bounds, ground, std::move(corridors),
                                               std::move(holes));
}

[[nodiscard]] simulation::PhysicsBody::CollisionLayer
parse_collision_mask(const std::string_view text, const std::string& context) {
  const std::uint64_t value = parse_map_unsigned(text, context);
  if (value > 0xFFFF'FFFFULL) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapValueInvalid, context,
                                "a collision layer or mask must fit 32 unsigned bits"};
  }
  return static_cast<simulation::PhysicsBody::CollisionLayer>(value);
}

// One CSV row split into exactly the declared number of columns, or a named rejection.
template <std::size_t ColumnCount>
[[nodiscard]] std::array<std::string_view, ColumnCount>
split_row(const std::string_view row, const std::filesystem::path& source_path,
          const std::size_t line_number) {
  std::size_t column_count = 1;
  for (const char character : row) {
    if (character == ',') {
      ++column_count;
    }
  }
  if (column_count != ColumnCount) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapColumnCountInvalid,
                                source_line_context(source_path, line_number),
                                "each data row must contain exactly " +
                                    std::to_string(ColumnCount) + " comma-separated fields"};
  }

  std::array<std::string_view, ColumnCount> columns{};
  std::size_t column_start = 0;
  for (std::size_t index = 0; index < ColumnCount; ++index) {
    const std::size_t delimiter = row.find(',', column_start);
    const std::size_t column_end = delimiter == std::string_view::npos ? row.size() : delimiter;
    columns[index] =
        trim_horizontal_whitespace(row.substr(column_start, column_end - column_start));
    column_start = column_end + 1;
  }
  return columns;
}

// The shared shape of both CSV files: an exact header line, then zero or more bounded, non-blank
// data rows. `visit_row` is called once per data row with the row and its line number.
template <typename RowVisitor>
void read_csv(const std::filesystem::path& source_path, const std::string_view expected_header,
              RowVisitor&& visit_row) {
  const std::string contents = read_application_text_file(
      source_path, ApplicationTextFileKind::kMap, MapLoader::kMaximumMapCsvFileBytes);

  const std::size_t header_end = contents.find('\n');
  const std::size_t header_length = header_end == std::string::npos ? contents.size() : header_end;
  std::string_view header{contents.data(), header_length};
  if (!header.empty() && header.back() == '\r') {
    header.remove_suffix(1);
  }
  if (header != expected_header) {
    throw ApplicationInputError{ApplicationInputErrorCode::kMapHeaderInvalid,
                                source_line_context(source_path, 1),
                                "header must exactly match " + std::string{expected_header}};
  }

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

    // A trailing newline after the last row is ordinary; a blank line between rows is not.
    const bool is_trailing_newline = row.empty() && line_end == std::string::npos;
    if (!is_trailing_newline) {
      if (row.empty()) {
        throw ApplicationInputError{ApplicationInputErrorCode::kMapRowEmpty,
                                    source_line_context(source_path, line_number),
                                    "blank data rows are not permitted"};
      }
      if (row.size() > MapLoader::kMaximumMapRowBytes) {
        throw ApplicationInputError{ApplicationInputErrorCode::kMapRowTooLong,
                                    source_line_context(source_path, line_number),
                                    "data row exceeds the 4096-byte limit"};
      }
      visit_row(row, line_number);
    }

    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
    ++line_number;
  }
}

[[nodiscard]] std::vector<simulation::StaticBodyDeclaration>
load_static_bodies(const std::filesystem::path& source_path) {
  std::vector<simulation::StaticBodyDeclaration> static_bodies;
  read_csv(source_path, kExpectedStaticBodiesHeader,
           [&static_bodies, &source_path](const std::string_view row, const std::size_t line) {
             const auto columns =
                 split_row<MapLoader::kStaticBodyColumnCount>(row, source_path, line);
             const std::string context = source_line_context(source_path, line);
             static_bodies.push_back(simulation::StaticBodyDeclaration::create(
                 simulation::PhysicsBody::create_static(
                     simulation::Vector2::create(parse_map_double(columns[0], context),
                                                 parse_map_double(columns[1], context)),
                     parse_collision_mask(columns[2], context),
                     parse_collision_mask(columns[3], context)),
                 simulation::parse_contact_effect_policy(columns[4])));
           });
  return static_bodies;
}

[[nodiscard]] std::vector<simulation::MapDefinition::Marker>
load_markers(const std::filesystem::path& source_path) {
  std::vector<simulation::MapDefinition::Marker> markers;
  read_csv(source_path, kExpectedMarkersHeader,
           [&markers, &source_path](const std::string_view row, const std::size_t line) {
             const auto columns = split_row<MapLoader::kMarkerColumnCount>(row, source_path, line);
             const std::string context = source_line_context(source_path, line);
             // An empty `team_id` is the documented spelling of an unaligned marker, which is what
             // every `spawn` marker is; any other value must be a whole team id.
             std::optional<simulation::TeamId> team;
             if (!columns[3].empty()) {
               team = simulation::TeamId::create(parse_map_unsigned(columns[3], context));
             }
             markers.push_back(simulation::MapDefinition::Marker::create(
                 std::string{columns[0]},
                 simulation::Vector2::create(parse_map_double(columns[1], context),
                                             parse_map_double(columns[2], context)),
                 team, simulation::MapMetadata::none()));
           });
  return markers;
}

} // namespace

simulation::MapDefinition MapLoader::load(const std::filesystem::path& map_directory) {
  const std::filesystem::path configuration_path = map_directory / kMapConfigurationFileName;
  const std::string configuration_contents = read_application_text_file(
      configuration_path, ApplicationTextFileKind::kMap, kMaximumMapConfigurationFileBytes);
  const StrictMapIniDocument document =
      StrictMapIniDocument::parse(configuration_contents, configuration_path);

  const std::string declared_name{document.value(MapConfigField::kMapName)};
  const std::string directory_name = map_directory.filename().string();
  if (declared_name != directory_name) {
    throw ApplicationInputError{
        ApplicationInputErrorCode::kMapNameMismatch, configuration_path.string(),
        "map.name " + declared_name + " must equal the directory name " + directory_name +
            "; two names for one map is two ways to refer to it"};
  }

  const simulation::ArenaBounds bounds = simulation::ArenaBounds::create(
      parse_map_double(document.value(MapConfigField::kBoundsWidth),
                       configuration_path.string() + ":bounds.width_world_units"),
      parse_map_double(document.value(MapConfigField::kBoundsHeight),
                       configuration_path.string() + ":bounds.height_world_units"));

  std::vector<simulation::MapMetadata::Entry> metadata_entries;
  metadata_entries.push_back(
      simulation::MapMetadata::Entry{std::string{kDisplayNameMetadataKey},
                                     std::string{document.value(MapConfigField::kMapDisplayName)}});

  return simulation::MapDefinition::create(
      declared_name, load_terrain(document, bounds, configuration_path),
      load_static_bodies(map_directory / kStaticBodiesFileName),
      load_markers(map_directory / kMarkersFileName),
      simulation::MapMetadata::create(std::move(metadata_entries)));
}

std::string_view MapLoader::expected_static_bodies_header() noexcept {
  return kExpectedStaticBodiesHeader;
}

std::string_view MapLoader::expected_markers_header() noexcept { return kExpectedMarkersHeader; }

} // namespace blob_royale::application
