#include "race/race_course.hpp"

#include "gameplay_validation_error.hpp"
#include "terrain_queries.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

RaceCourse RaceCourse::create(const simulation::MapDefinition& map,
                              const RaceConfiguration& configuration) {
  const auto* road = map.terrain().find_corridor(configuration.road());
  const std::string map_context = "map " + std::string(map.name());
  if (road == nullptr) {
    throw GameplayValidationError(
        GameplayValidationCode::kRaceMapRoadMissing, "race_course.create.road",
        map_context + " declares no terrain corridor named " + std::string(configuration.road()));
  }
  if (configuration.checkpoint_radius() > road->half_width()) {
    throw GameplayValidationError(GameplayValidationCode::kRaceScalarOutOfRange,
                                  "race_course.create.checkpoint_radius",
                                  map_context + " checkpoint radius exceeds terrain corridor " +
                                      std::string(configuration.road()) + " half-width");
  }
  std::vector<simulation::Vector2> checkpoints;
  for (const simulation::MapDefinition::Marker& marker : map.markers()) {
    if (marker.kind == kCheckpointMarkerKind) {
      checkpoints.push_back(marker.position);
    }
  }

  if (checkpoints.empty()) {
    throw GameplayValidationError(GameplayValidationCode::kRaceMapWithoutCheckpoint,
                                  "race_course.create.checkpoints",
                                  map_context + " declares no checkpoint marker");
  }

  const auto road_index = static_cast<std::size_t>(road - map.terrain().corridors().data());
  RaceCourse course{map.terrain(), road_index, std::move(checkpoints),
                    configuration.checkpoint_radius()};
  for (std::size_t index = 0; index < course.checkpoints().size(); ++index) {
    if (course.distance_to_centreline(course.checkpoints()[index]) > course.track_half_width()) {
      throw GameplayValidationError(GameplayValidationCode::kRaceMapCheckpointOutsideCorridor,
                                    "race_course.create.checkpoints",
                                    map_context + " declares checkpoint " + std::to_string(index) +
                                        " with its centre outside the track corridor");
    }
  }
  for (std::size_t index = 0; index < map.spawn_points().size(); ++index) {
    if (course.distance_to_centreline(map.spawn_points()[index].position) >
        course.track_half_width()) {
      throw GameplayValidationError(GameplayValidationCode::kRaceMapSpawnPointOutsideCorridor,
                                    "race_course.create.spawn_points",
                                    map_context + " declares spawn marker " +
                                        std::to_string(index) + " outside the track corridor");
    }
  }
  if (map.spawn_points().empty()) {
    throw GameplayValidationError(GameplayValidationCode::kRaceMapWithoutSpawnPoint,
                                  "race_course.create.spawn_points",
                                  map_context + " declares no spawn marker");
  }
  return course;
}

RaceCourse::RaceCourse(simulation::TerrainDefinition terrain, const std::size_t road_index,
                       std::vector<simulation::Vector2> checkpoints,
                       const double checkpoint_radius) noexcept
    : terrain_(std::move(terrain)), road_index_(road_index), checkpoints_(std::move(checkpoints)),
      checkpoint_radius_(checkpoint_radius) {}

double RaceCourse::distance_to_centreline(const simulation::Vector2& point) const noexcept {
  return simulation::corridor_distance_to_centreline(bound_road(), point);
}

} // namespace blob_royale::gameplay
