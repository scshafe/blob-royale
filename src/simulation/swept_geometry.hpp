#ifndef BLOB_ROYALE_SIMULATION_SWEPT_GEOMETRY_HPP
#define BLOB_ROYALE_SIMULATION_SWEPT_GEOMETRY_HPP

#include "motion_event_order.hpp"
#include "vector2.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::simulation {

// canonical: swept_boundary_roots -- the complete boundary times of one convex primitive.
//
// Sorted, distinct roots in [0, 1]. A tangent contributes one root. A motion continuously on
// the boundary contributes the endpoints of that interval (including stationary boundary motion:
// [0, 1]). Interior starts do not manufacture a zero root: the first boundary is the later exit.
// Callers deciding initial overlap or support must evaluate that separately. No root is an impulse.
class SweptBoundaryRoots final {
public:
  [[nodiscard]] std::span<const MotionTime> times() const& noexcept {
    return {times_.data(), count_};
  }
  [[nodiscard]] std::span<const MotionTime> times() const&& = delete;
  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] std::optional<MotionTime> first() const noexcept {
    return empty() ? std::nullopt : std::optional{times_[0]};
  }

  friend bool operator==(const SweptBoundaryRoots&, const SweptBoundaryRoots&) = default;

private:
  friend class SweptRootAccumulator;
  std::array<MotionTime, 2> times_{MotionTime::start(), MotionTime::start()};
  std::size_t count_{};
};

// canonical: swept_geometry -- the only analytic event-root arithmetic, for contacts and triggers.
//
// Position is start + displacement * t. A boundary_radius is the radius of the locus traced by
// the moving centre, finite in [0, 2 * kMaximumPhysicalComponentMagnitude]. Radius zero is a point
// or a segment. Tolerances are geometric policy: the caller supplies an expanded/eroded radius
// explicitly, never an epsilon for comparing times. MotionTime documents the single rounding rule.
// Every invalid or unrepresentable calculation throws SimulationValidationError.
// Exact discriminant signs use a bounded expansion: after common power-of-two scaling, each
// nonzero input length must be at least 2^-200. A wider scale ratio is rejected visibly; it
// cannot silently collapse a thin crossing or change an exact tangent into two roots.
[[nodiscard]] SweptBoundaryRoots swept_circle_boundary_roots(const Vector2& start,
                                                             const Vector2& displacement,
                                                             const Vector2& center,
                                                             double boundary_radius);

[[nodiscard]] SweptBoundaryRoots swept_capsule_boundary_roots(const Vector2& start,
                                                              const Vector2& displacement,
                                                              const Vector2& segment_start,
                                                              const Vector2& segment_end,
                                                              double boundary_radius);

// The axis-coordinate form is also the shared linear root equation for capsule side planes.
// A stationary coordinate on the line returns [0, 1]; parallel motion elsewhere returns no root.
// Finite scalar inputs are accepted; finite roots outside [0, 1] are omitted, never snapped.
[[nodiscard]] SweptBoundaryRoots swept_line_boundary_roots(double start_coordinate,
                                                           double displacement_coordinate,
                                                           double boundary_coordinate);

// Maps a root from one remaining linear segment to its enclosing tick interval. Interior roots
// evaluate begin + ((end - begin) * local) in that order; exact local endpoints retain the supplied
// endpoints directly. Reversed intervals fail. This is the sole event-time remapping arithmetic.
[[nodiscard]] MotionTime map_motion_time(MotionTime local, MotionTime begin, MotionTime end);

// External disc contact is a point query with obstacle_radius + disc_radius, in that order.
// Each radius is finite in [0, kMaximumPhysicalComponentMagnitude]. Interior disc containment
// instead calls the point query with the explicitly eroded boundary radius; it is a different
// geometric predicate, not another root equation.
[[nodiscard]] SweptBoundaryRoots
swept_disc_circle_boundary_roots(const Vector2& start, const Vector2& displacement,
                                 double disc_radius, const Vector2& center, double obstacle_radius);

[[nodiscard]] SweptBoundaryRoots
swept_disc_capsule_boundary_roots(const Vector2& start, const Vector2& displacement,
                                  double disc_radius, const Vector2& segment_start,
                                  const Vector2& segment_end, double obstacle_radius);

} // namespace blob_royale::simulation

#endif
