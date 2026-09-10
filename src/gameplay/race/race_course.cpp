#include "race/race_course.hpp"

#include "gameplay_validation_error.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::gameplay {

RaceCourse RaceCourse::create(const simulation::MapDefinition& map,
                              const RaceConfiguration& configuration) {
  std::vector<simulation::Vector2> track;
  std::vector<simulation::Vector2> checkpoints;
  for (const simulation::MapDefinition::Marker& marker : map.markers()) {
    if (marker.kind == kTrackMarkerKind) {
      track.push_back(marker.position);
    } else if (marker.kind == kCheckpointMarkerKind) {
      checkpoints.push_back(marker.position);
    }
  }

  const std::string map_context = "map " + std::string(map.name());
  if (track.size() < 2) {
    throw GameplayValidationError(GameplayValidationCode::kRaceMapTooFewTrackMarkers,
                                  "race_course.create.track",
                                  map_context + " declares fewer than two track markers");
  }
  for (std::size_t index = 1; index < track.size(); ++index) {
    if (track[index - 1] == track[index]) {
      throw GameplayValidationError(
          GameplayValidationCode::kRaceMapCoincidentTrackMarkers, "race_course.create.track",
          map_context + " declares coincident consecutive track markers " +
              std::to_string(index - 1) + " and " + std::to_string(index));
    }
  }
  if (checkpoints.empty()) {
    throw GameplayValidationError(GameplayValidationCode::kRaceMapWithoutCheckpoint,
                                  "race_course.create.checkpoints",
                                  map_context + " declares no checkpoint marker");
  }

  RaceCourse course{std::move(track), std::move(checkpoints), configuration.track_half_width(),
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

RaceCourse::RaceCourse(std::vector<simulation::Vector2> track,
                       std::vector<simulation::Vector2> checkpoints, const double track_half_width,
                       const double checkpoint_radius) noexcept
    : track_(std::move(track)), checkpoints_(std::move(checkpoints)),
      track_half_width_(track_half_width), checkpoint_radius_(checkpoint_radius) {}

double RaceCourse::distance_to_centreline(const simulation::Vector2& point) const noexcept {
  double nearest_distance = std::numeric_limits<double>::infinity();
  for (std::size_t index = 1; index < track_.size(); ++index) {
    const simulation::Vector2& a = track_[index - 1];
    const simulation::Vector2& b = track_[index];
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double wx = point.x() - a.x();
    const double wy = point.y() - a.y();
    double t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
    if (t < 0.0) {
      t = 0.0;
    } else if (t > 1.0) {
      t = 1.0;
    }
    const double cx = a.x() + (dx * t);
    const double cy = a.y() + (dy * t);
    const double ex = point.x() - cx;
    const double ey = point.y() - cy;
    const double distance = std::sqrt(ex * ex + ey * ey);
    if (distance < nearest_distance) {
      nearest_distance = distance;
    }
  }
  return nearest_distance;
}

} // namespace blob_royale::gameplay
