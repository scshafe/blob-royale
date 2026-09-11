#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_COURSE_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_COURSE_HPP

#include "map_definition.hpp"
#include "race/race_configuration.hpp"
#include "terrain_definition.hpp"
#include "vector2.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace blob_royale::gameplay {

// canonical: race_course -- the validated, ordered centreline and gates of one race.
//
// Built once by binding `[race] road` to a named terrain corridor, then handed by value to its
// readers. Owning immutable terrain keeps the selected corridor alive after the map is destroyed.
// Track nodes and width are views of that corridor; checkpoint markers retain authored order.
// Race admission keeps the exact distance <= width convention, independent of full terrain
// support/holes. Its last checkpoint is the finish.
// related: terrain_queries.hpp -- the canonical corridor distance arithmetic.
// related: race_configuration.hpp -- the independently validated road name and gate radius.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md -- the distance arithmetic.
class RaceCourse final {
public:
  static constexpr std::string_view kCheckpointMarkerKind = "checkpoint";

  // Binds the configured road and projects checkpoints. Throws GameplayValidationError with a
  // GAMEPLAY.RACE_MAP_* code and map name for an absent road/checkpoint/spawn or a checkpoint/spawn
  // centre outside that road. Radius greater than the bound width raises RACE_SCALAR_OUT_OF_RANGE.
  // TerrainDefinition already owns corridor segment and shape-limit validation.
  [[nodiscard]] static RaceCourse create(const simulation::MapDefinition& map,
                                         const RaceConfiguration& configuration);

  RaceCourse(const RaceCourse&) = default;
  RaceCourse(RaceCourse&&) noexcept = default;
  RaceCourse& operator=(const RaceCourse&) = default;
  RaceCourse& operator=(RaceCourse&&) noexcept = default;
  ~RaceCourse() = default;

  // At least two nodes, with a nonzero segment between every consecutive pair.
  [[nodiscard]] std::span<const simulation::Vector2> track() const& noexcept {
    return bound_road().points();
  }
  [[nodiscard]] std::span<const simulation::Vector2> track() const&& = delete;
  // At least one gate, in progress order; the last is the finish.
  [[nodiscard]] std::span<const simulation::Vector2> checkpoints() const& noexcept {
    return checkpoints_;
  }
  [[nodiscard]] std::span<const simulation::Vector2> checkpoints() const&& = delete;
  // World units; both are strictly positive and checkpoint_radius() <= track_half_width().
  [[nodiscard]] double track_half_width() const noexcept { return bound_road().half_width(); }
  [[nodiscard]] double checkpoint_radius() const noexcept { return checkpoint_radius_; }

  // Minimum point-to-segment distance in world units, including the endpoint caps. Evaluates
  // ADR 0007's written-out projection and sqrt of the sum in its declared arithmetic order.
  [[nodiscard]] double distance_to_centreline(const simulation::Vector2& point) const noexcept;

  friend bool operator==(const RaceCourse&, const RaceCourse&) = default;

private:
  RaceCourse(simulation::TerrainDefinition terrain, std::size_t road_index,
             std::vector<simulation::Vector2> checkpoints, double checkpoint_radius) noexcept;

  [[nodiscard]] const simulation::TerrainCorridor& bound_road() const& noexcept {
    return terrain_.corridors()[road_index_];
  }

  simulation::TerrainDefinition terrain_;
  std::size_t road_index_;
  std::vector<simulation::Vector2> checkpoints_;
  double checkpoint_radius_;
};

} // namespace blob_royale::gameplay

#endif
