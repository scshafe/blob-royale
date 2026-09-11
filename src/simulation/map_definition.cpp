#include "map_definition.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "terrain_queries.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace blob_royale::simulation {
namespace {

[[nodiscard]] bool is_snake_case_identity(const std::string_view value) noexcept {
  if (value.empty() || (value.front() < 'a') || (value.front() > 'z')) {
    return false;
  }
  return std::all_of(value.cbegin(), value.cend(), [](const char character) {
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
           character == '_';
  });
}

// A map name is a file-system-shaped identity -- `arena-960x640` is the ADR's own example -- so it
// admits the dash and dot a directory name carries, unlike the snake_case vocabulary of marker
// kinds and metadata keys.
[[nodiscard]] bool is_map_name(const std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }
  return std::all_of(value.cbegin(), value.cend(), [](const char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || character == '-' ||
           character == '.';
  });
}

[[nodiscard]] std::string static_body_context(const std::size_t index) {
  return "map_definition.static_bodies[" + std::to_string(index) + "]";
}

[[nodiscard]] std::string marker_context(const std::size_t index) {
  return "map_definition.markers[" + std::to_string(index) + "]";
}

} // namespace

MapMetadata MapMetadata::create(std::vector<Entry> entries) {
  if (entries.size() > kMaximumMapMetadataEntryCount) {
    throw SimulationValidationError{
        SimulationValidationCode::kMapMetadataLimitExceeded, "map_definition.metadata",
        "metadata entry count " + std::to_string(entries.size()) + " exceeds the accepted limit"};
  }

  for (const Entry& entry : entries) {
    if (entry.key.size() > kMaximumMapMetadataKeyLength || !is_snake_case_identity(entry.key)) {
      throw SimulationValidationError{
          SimulationValidationCode::kMapMetadataKeyInvalid, "map_definition.metadata.key",
          "metadata key " + entry.key +
              " must be a non-empty snake_case identity within the accepted length"};
    }
    if (entry.value.empty() || entry.value.size() > kMaximumMapMetadataValueLength) {
      throw SimulationValidationError{SimulationValidationCode::kMapMetadataValueInvalid,
                                      "map_definition.metadata.value",
                                      "metadata value for key " + entry.key +
                                          " must be non-empty and within the accepted length"};
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const Entry& left, const Entry& right) { return left.key < right.key; });

  const auto duplicate = std::adjacent_find(
      entries.cbegin(), entries.cend(),
      [](const Entry& left, const Entry& right) { return left.key == right.key; });
  if (duplicate != entries.cend()) {
    throw SimulationValidationError{SimulationValidationCode::kMapMetadataDuplicateKey,
                                    "map_definition.metadata.key",
                                    "duplicate metadata key " + duplicate->key};
  }

  return MapMetadata{std::move(entries)};
}

MapMetadata MapMetadata::none() { return MapMetadata{}; }

const std::string* MapMetadata::find(const std::string_view key) const& noexcept {
  const auto match = std::lower_bound(entries_.cbegin(), entries_.cend(), key,
                                      [](const Entry& entry, const std::string_view searched) {
                                        return std::string_view{entry.key} < searched;
                                      });
  if (match == entries_.cend() || match->key != key) {
    return nullptr;
  }
  return &match->value;
}

MapMetadata::MapMetadata(std::vector<Entry> entries) noexcept : entries_(std::move(entries)) {}

MapDefinition::Marker MapDefinition::Marker::create(std::string kind, Vector2 position,
                                                    std::optional<TeamId> team,
                                                    MapMetadata metadata) {
  if (kind.size() > kMaximumMapMarkerKindLength || !is_snake_case_identity(kind)) {
    throw SimulationValidationError{
        SimulationValidationCode::kMapMarkerKindInvalid, "map_definition.markers.kind",
        "marker kind " + kind +
            " must be a non-empty snake_case identity within the accepted length"};
  }
  return Marker{std::move(kind), position, team, std::move(metadata)};
}

MapDefinition::Marker MapDefinition::Marker::spawn(Vector2 position) {
  return create(std::string{kSpawnMarkerKind}, position, std::nullopt, MapMetadata::none());
}

MapDefinition MapDefinition::create(std::string name, const ArenaBounds bounds,
                                    std::vector<PhysicsBody> static_bodies,
                                    std::vector<Marker> markers, MapMetadata metadata) {
  return create(std::move(name), TerrainDefinition::solid(bounds), std::move(static_bodies),
                std::move(markers), std::move(metadata));
}

MapDefinition MapDefinition::create(std::string name, TerrainDefinition terrain,
                                    std::vector<PhysicsBody> static_bodies,
                                    std::vector<Marker> markers, MapMetadata metadata) {
  const ArenaBounds& bounds = terrain.bounds();
  if (name.size() > kMaximumMapNameLength || !is_map_name(name)) {
    throw SimulationValidationError{
        SimulationValidationCode::kMapNameInvalid, "map_definition.name",
        "map name " + name +
            " must be non-empty, within the accepted length, and composed of "
            "letters, digits, underscore, dash, and dot"};
  }

  if (static_bodies.size() > kMaximumMapStaticBodyCount) {
    throw SimulationValidationError{SimulationValidationCode::kMapStaticBodyLimitExceeded,
                                    "map_definition.static_bodies",
                                    "static body count " + std::to_string(static_bodies.size()) +
                                        " exceeds the accepted limit"};
  }
  if (markers.size() > kMaximumMapMarkerCount) {
    throw SimulationValidationError{
        SimulationValidationCode::kMapMarkerLimitExceeded, "map_definition.markers",
        "marker count " + std::to_string(markers.size()) + " exceeds the accepted limit"};
  }

  for (std::size_t index = 0; index < static_bodies.size(); ++index) {
    const PhysicsBody& body = static_bodies[index];
    // A dynamic body in the static list would be integrated by the kernel from a position the map
    // chose and never re-chose, so it is rejected rather than silently promoted.
    if (!body.is_static()) {
      throw SimulationValidationError{
          SimulationValidationCode::kMapStaticBodyNotStatic,
          static_body_context(index) + ".is_static",
          "a map's static body must carry is_static; a dynamic body is an entity a mode spawns"};
    }
    // The centre obeys the closed rectangle, not the disc-centre interval: a wall legitimately
    // sits on the arena edge. A body whose centre is outside the rectangle can never be reached by
    // a disc phase 4 keeps inside it, so it is authoring error rather than content.
    if (!bounds.contains(body.position())) {
      throw SimulationValidationError{
          SimulationValidationCode::kMapStaticBodyOutOfBounds,
          static_body_context(index) + ".position",
          "a static body centre must lie inside the closed arena rectangle"};
    }
    if (!terrain_supports_point(terrain, body.position())) {
      throw SimulationValidationError{SimulationValidationCode::kTerrainGeometryOutOfBounds,
                                      static_body_context(index) + ".position",
                                      "a static body centre must lie on supported terrain"};
    }
  }

  std::vector<Marker> spawn_points;
  for (std::size_t index = 0; index < markers.size(); ++index) {
    const Marker& marker = markers[index];
    if (marker.kind.size() > kMaximumMapMarkerKindLength || !is_snake_case_identity(marker.kind)) {
      throw SimulationValidationError{
          SimulationValidationCode::kMapMarkerKindInvalid, marker_context(index) + ".kind",
          "marker kind " + marker.kind +
              " must be a non-empty snake_case identity within the accepted length"};
    }
    if (!bounds.contains(marker.position)) {
      throw SimulationValidationError{SimulationValidationCode::kMapMarkerOutOfBounds,
                                      marker_context(index) + ".position",
                                      "a marker must lie inside the closed arena rectangle"};
    }
    if (marker.kind == kSpawnMarkerKind) {
      spawn_points.push_back(marker);
    }
  }

  return MapDefinition{std::move(name),    std::move(terrain),      std::move(static_bodies),
                       std::move(markers), std::move(spawn_points), std::move(metadata)};
}

MapDefinition MapDefinition::bare_arena(const ArenaBounds bounds) {
  return create("bare_arena", bounds, {}, {}, MapMetadata::none());
}

MapDefinition::MapDefinition(std::string name, TerrainDefinition terrain,
                             std::vector<PhysicsBody> static_bodies, std::vector<Marker> markers,
                             std::vector<Marker> spawn_points, MapMetadata metadata) noexcept
    : name_(std::move(name)), terrain_(std::move(terrain)),
      static_bodies_(std::move(static_bodies)), markers_(std::move(markers)),
      spawn_points_(std::move(spawn_points)), metadata_(std::move(metadata)) {}

} // namespace blob_royale::simulation
