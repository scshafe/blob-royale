#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_COURSE_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_COURSE_HPP

#include "map_definition.hpp"
#include "race/race_configuration.hpp"
#include "vector2.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace blob_royale::gameplay {

// canonical: race_course -- the validated, ordered centreline and gates of one race.
//
// Built once from a map and its `[race]` configuration, then handed by value to the systems that
// read it. Track and checkpoint marker order is authored order; every other marker kind belongs
// to another rule. The corridor is the closed set whose distance to the centreline is at most
// track_half_width(). Its last checkpoint is the finish.
// related: race_configuration.hpp -- the independently validated width and gate radius.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md -- the distance arithmetic.
class RaceCourse final {
public:
  static constexpr std::string_view kTrackMarkerKind = "track";
  static constexpr std::string_view kCheckpointMarkerKind = "checkpoint";

  // Projects the map's markers and validates the six map requirements. Throws
  // GameplayValidationError with a GAMEPLAY.RACE_MAP_* code and the map name for fewer than two
  // track nodes, coincident consecutive nodes, no checkpoint, an outside checkpoint centre, an
  // outside spawn point, or no spawn point. Gate radius is already validated by configuration.
  [[nodiscard]] static RaceCourse create(const simulation::MapDefinition& map,
                                         const RaceConfiguration& configuration);

  RaceCourse(const RaceCourse&) = default;
  RaceCourse(RaceCourse&&) noexcept = default;
  RaceCourse& operator=(const RaceCourse&) = default;
  RaceCourse& operator=(RaceCourse&&) noexcept = default;
  ~RaceCourse() = default;

  // At least two nodes, with a nonzero segment between every consecutive pair.
  [[nodiscard]] std::span<const simulation::Vector2> track() const& noexcept { return track_; }
  [[nodiscard]] std::span<const simulation::Vector2> track() const&& = delete;
  // At least one gate, in progress order; the last is the finish.
  [[nodiscard]] std::span<const simulation::Vector2> checkpoints() const& noexcept {
    return checkpoints_;
  }
  [[nodiscard]] std::span<const simulation::Vector2> checkpoints() const&& = delete;
  // World units; both are strictly positive and checkpoint_radius() <= track_half_width().
  [[nodiscard]] double track_half_width() const noexcept { return track_half_width_; }
  [[nodiscard]] double checkpoint_radius() const noexcept { return checkpoint_radius_; }

  // Minimum point-to-segment distance in world units, including the endpoint caps. Evaluates
  // ADR 0007's written-out projection and sqrt of the sum in its declared arithmetic order.
  [[nodiscard]] double distance_to_centreline(const simulation::Vector2& point) const noexcept;

  friend bool operator==(const RaceCourse&, const RaceCourse&) = default;

private:
  RaceCourse(std::vector<simulation::Vector2> track, std::vector<simulation::Vector2> checkpoints,
             double track_half_width, double checkpoint_radius) noexcept;

  std::vector<simulation::Vector2> track_;
  std::vector<simulation::Vector2> checkpoints_;
  double track_half_width_;
  double checkpoint_radius_;
};

} // namespace blob_royale::gameplay

#endif
