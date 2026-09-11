#ifndef BLOB_ROYALE_SIMULATION_TERRAIN_QUERIES_HPP
#define BLOB_ROYALE_SIMULATION_TERRAIN_QUERIES_HPP

#include "motion_event_order.hpp"
#include "terrain_definition.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace blob_royale::simulation {

// Stable authored boundary identity; zero identifies an already-supported query point.
struct BoundaryFeatureId final {
  std::uint64_t value{};
  friend auto operator<=>(const BoundaryFeatureId&, const BoundaryFeatureId&) = default;
};

// A closed supported interval, including a singleton tangent/rim. Ordering is MotionTime's exact
// order, never a tolerance comparator. The result is sorted, disjoint, and maximally merged.
struct TerrainSupportInterval final {
  MotionTime begin;
  MotionTime end;
  friend bool operator==(const TerrainSupportInterval&, const TerrainSupportInterval&) = default;
};

// A supported binary64 witness to the analytically selected nearest boundary. Selection precedes
// representability correction, by computed distance, x, y, then feature identity. Translation can
// round a boundary coordinate into void. At most kMaximumTerrainWitnessRoundingStepCount directed
// nextafter steps per coordinate repair it, retaining the selected feature. distance is measured
// to the final supported coordinate; no farther boundary replaces an unrepresentable nearer one.
struct TerrainPointWitness final {
  Vector2 point;
  double distance;
  BoundaryFeatureId feature;
  friend bool operator==(const TerrainPointWitness&, const TerrainPointWitness&) = default;
};

// Nearest point on the authored centreline, not on the Boolean terrain boundary. Equal computed
// distances retain the first declared segment. This result owns its point, never a corridor view.
struct CorridorCentrelineProjection final {
  Vector2 point;
  double distance;
  friend bool operator==(const CorridorCentrelineProjection&,
                         const CorridorCentrelineProjection&) = default;
};

// canonical: corridor_centreline_projection -- the racer's written projection/clamp/sqrt order.
// TerrainCorridor guarantees nonzero finite segment squared lengths. No support tolerance,
// envelope clipping, hole subtraction, or boundary-witness correction participates. On standalone
// signed/extreme corridors, the raw projected coordinate may round beyond Vector2's scalar bound;
// materializing this point then throws the existing Vector2 validation error. The distance-only
// query has no such point-materialization failure and retains its noexcept contract.
[[nodiscard]] CorridorCentrelineProjection
corridor_project_to_centreline(const TerrainCorridor& corridor, const Vector2& point);

// Distance-only view of the same canonical projection, without materializing its point.
// The validated corridor has at least two distinct consecutive points; returns finite distance.
[[nodiscard]] double corridor_distance_to_centreline(const TerrainCorridor& corridor,
                                                     const Vector2& point) noexcept;

// canonical: terrain_queries -- envelope AND positive union MINUS open holes, shared by all
// readers. Roads include their edge plus kPositionTolerance; exact hole rims and the tolerance band
// are supported. The envelope remains exact. No consumer owns another support predicate.
[[nodiscard]] bool terrain_supports_point(const TerrainDefinition& terrain, const Vector2& point);

// Parameterizes start + displacement * t, t in [0,1]. Primitive intervals use only swept_geometry
// roots and endpoint predicates, not sampling. Throws SIMULATION.TERRAIN_GEOMETRY_PRECISION_LOST
// when a required crossing/arrangement cannot be represented; never returns partial geometry.
[[nodiscard]] std::vector<TerrainSupportInterval>
swept_support_intervals(const TerrainDefinition& terrain, const Vector2& start,
                        const Vector2& displacement);

// Zero when initially unsupported; otherwise the supported component's end only when void follows
// before t=1. Ending exactly on a supported rim does not fall. Starting on a rim into void exits
// at0.
[[nodiscard]] std::optional<MotionTime> first_support_exit(const TerrainDefinition& terrain,
                                                           const Vector2& start,
                                                           const Vector2& displacement);

// Radius-zero nearest supported point, or absence for empty terrain. Already-supported p returns p.
// Analytic line/arc projections use the compiled exposed Boolean boundary, not capsule erosion.
// An unrepresentable projection is rounded toward its cached supported side, never toward the
// unsupported query. At a multi-feature endpoint, the compiler derives actual Boolean supported
// sectors from all incident tangent rays; recovery follows the first sector in canonical angular
// order, not one span's normal. Four directed targets on that sector's interior ray stay inside
// the coordinatewise ULP budget and must pass exact sector-sign and canonical support checks.
// This is not a complete search of every representable point/sector inside that budget. Rim-only
// geometry has no open recovery sector. Exhaustion throws
// SIMULATION.TERRAIN_GEOMETRY_PRECISION_LOST.
[[nodiscard]] std::optional<TerrainPointWitness>
nearest_supported_point(const TerrainDefinition& terrain, const Vector2& point);

// Computed clearance from a supported point, with a nearest exposed-boundary witness; absent iff
// the center is unsupported. A projected witness rounded into void is corrected coordinatewise
// toward the already-supported query point, so correction cannot increase computed clearance.
// This binary64 policy does not claim interval-certified exact-real bounds for preceding root or
// sqrt arithmetic. A closed disc fits under this policy iff clearance is at least its radius.
[[nodiscard]] std::optional<TerrainPointWitness> disc_clearance(const TerrainDefinition& terrain,
                                                                const Vector2& point);

// Radius must be finite, nonnegative and <= kMaximumPhysicalComponentMagnitude; invalid input
// throws SIMULATION.TERRAIN_QUERY_RADIUS_INVALID. Internal union seams never reduce clearance.
[[nodiscard]] bool terrain_supports_disc(const TerrainDefinition& terrain, const Vector2& center,
                                         double radius);

} // namespace blob_royale::simulation

#endif
