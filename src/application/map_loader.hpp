#ifndef BLOB_ROYALE_APPLICATION_MAP_LOADER_HPP
#define BLOB_ROYALE_APPLICATION_MAP_LOADER_HPP

#include "map_definition.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace blob_royale::application {

// canonical: map_loader -- the only map-directory boundary.
//
// A map is content, not code (`src/simulation/map_definition.hpp`), and this is what turns one
// authored directory into the validated `simulation::MapDefinition` a mode is handed. Adding a map
// is adding a directory and naming it in `[match] map=`; no C++ changes at all.
//
// One map directory is exactly three files, each strictly parsed with no silent default and no
// partially accepted document:
//
//   map.cfg            `[map] name`, `[map] display_name`, `[bounds] width_world_units`,
//                      `[bounds] height_world_units`, `[terrain] ground=solid|corridors`.
//                      Every fixed key and section required; no implicit solid fallback.
//                      Optional `[terrain.corridor.<name>]` instances require
//                      `half_width_world_units` and `points_world_units=x,y;x,y;...`.
//                      Optional `[terrain.hole.<name>]` instances require
//                      `center_x_world_units`, `center_y_world_units`, `radius_world_units`.
//                      Unknown sections/keys and duplicate sections/keys are rejected. A point
//                      list has complete comma-separated pairs, semicolons only between pairs,
//                      and permits horizontal whitespace around each coordinate.
//   static_bodies.csv  `position_x_world_units,position_y_world_units,collision_layer,
//                      collision_mask`. Header always present; zero rows is a map with no
//                      obstacles.
//   markers.csv        `marker_kind,position_x_world_units,position_y_world_units,team_id`.
//                      `team_id` is empty for an unaligned marker.
//
// **`static_bodies.csv` deliberately carries no radius column.** No accepted phase reads
// `PhysicsBody::radius()`; the pair predicate, the wall fold, the spatial index, and the spawn
// occupancy test all measure with `SimulationConfig::player_radius()`
// (`src/simulation/physics_body.hpp`). A per-body authored radius would therefore publish a size
// the collision kernel does not use -- an obstacle drawn at 40 wu and collided at 10 wu -- which is
// the two-sources-of-truth defect this tree refuses. Differently sized bodies are the growing-blob
// change, a versioned physics amendment on ADR 0003's path, and the column arrives with it.
// `GameWorld::create(configuration, map, seed)` fills every seated body's radius in from the
// configuration, so what a snapshot publishes is what the kernel measured.
//
// **`map.cfg`'s `name` must equal the directory name.** Two names for one map is two ways to refer
// to it, and the one the wire publishes (`MapDefinition::name()`) would then be able to disagree
// with the one `[match] map=` selected.
//
// Throws ApplicationInputError with an `APPLICATION.MAP.*` code for every filesystem, grammar, and
// cross-field failure, and lets `simulation::SimulationValidationError` from
// the terrain and map factories propagate for content rules the simulation owns -- an invalid
// terrain name or geometry, an out-of-bounds marker, a dynamic body in the static list, a count
// past a limit. related: match_configuration.hpp
// -- where the directory to load comes from. related: ../simulation/map_definition.hpp -- the value
// this produces.
class MapLoader final {
public:
  static constexpr std::size_t kMaximumMapConfigurationFileBytes = 65'536;
  static constexpr std::size_t kMaximumMapCsvFileBytes = 1'048'576;
  static constexpr std::size_t kMaximumMapRowBytes = 4'096;
  static constexpr std::size_t kStaticBodyColumnCount = 5;
  static constexpr std::size_t kMarkerColumnCount = 4;

  // Loads `<map_directory>/map.cfg`, `static_bodies.csv`, and `markers.csv`.
  [[nodiscard]] static simulation::MapDefinition load(const std::filesystem::path& map_directory);

  // The metadata key `map.cfg`'s `display_name` is published under.
  static constexpr std::string_view kDisplayNameMetadataKey = "display_name";

  [[nodiscard]] static std::string_view expected_static_bodies_header() noexcept;
  [[nodiscard]] static std::string_view expected_markers_header() noexcept;
};

} // namespace blob_royale::application

#endif
