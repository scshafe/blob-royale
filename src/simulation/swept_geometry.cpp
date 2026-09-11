#include "swept_geometry.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// A line intersects a convex primitive's boundary in at most two isolated points or one closed
// interval. Capsule decomposition may rediscover its endpoints at side/cap seams; keeping its
// extrema merges only those duplicate descriptions, never disconnected intervals or other shapes.
class SweptRootAccumulator final {
public:
  void add(const double value) {
    if (!std::isfinite(value)) {
      throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarNotFinite,
                                      "swept_geometry.root", "calculated root must be finite");
    }
    if (value < 0.0 || value > 1.0) {
      return;
    }
    const MotionTime time = MotionTime::create(value);
    if (roots_.empty()) {
      roots_.times_[0] = time;
      roots_.count_ = 1;
    } else if (time < roots_.times_[0]) {
      roots_.times_[1] = roots_.times_[roots_.count_ - 1];
      roots_.times_[0] = time;
      roots_.count_ = 2;
    } else if (time > roots_.times_[roots_.count_ - 1]) {
      roots_.times_[1] = time;
      roots_.count_ = 2;
    }
  }

  [[nodiscard]] SweptBoundaryRoots finish() const noexcept { return roots_; }

private:
  SweptBoundaryRoots roots_;
};

namespace {

void require_finite(const double value, const std::string_view context) {
  if (!std::isfinite(value)) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarNotFinite,
                                    std::string(context), "scalar must be finite");
  }
}

void require_radius(const double radius, const double maximum, const std::string_view context) {
  require_finite(radius, context);
  if (radius < 0.0 || radius > maximum) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    std::string(context), "radius is outside the accepted range");
  }
}

void require_precision_component(const double value, const bool nonzero,
                                 const std::string_view context) {
  if (nonzero && std::abs(value) < 0x1p-200) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    std::string(context),
                                    "nonzero normalized lengths must be at least 2^-200");
  }
}

// Exact-sign arithmetic for a degree-four polynomial of normalized binary64 lengths. The
// 2^-200 input floor places the lowest possible product bit at -1008 (four factors with at most
// 53 significant bits), above the -1074 subnormal floor. Thus TwoProduct's explicit FMA residual
// cannot underflow. Each expansion is ordered low-to-high and nonoverlapping; TwoSum does not
// require a magnitude precondition. No platform-dependent extended precision participates.
class Expansion final {
public:
  void add(const double value) {
    double carry = value;
    std::size_t next_size = 0;
    for (std::size_t index = 0; index < size_; ++index) {
      const double term = terms_[index];
      const double sum = carry + term;
      const double term_virtual = sum - carry;
      const double carry_virtual = sum - term_virtual;
      const double term_error = term - term_virtual;
      const double carry_error = carry - carry_virtual;
      const double error = carry_error + term_error;
      if (error != 0.0) {
        terms_[next_size++] = error;
      }
      carry = sum;
    }
    if (carry != 0.0) {
      if (next_size == terms_.size()) {
        throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                        "swept_geometry.expansion",
                                        "exact root arithmetic exceeded its fixed capacity");
      }
      terms_[next_size++] = carry;
    }
    size_ = next_size;
  }

  void add_product(const double first, const double second) {
    const double product = first * second;
    const double error = std::fma(first, second, -product);
    add(error);
    add(product);
  }

  void add_expansion(const Expansion& other, const double sign = 1.0) {
    for (std::size_t index = 0; index < other.size_; ++index) {
      add(sign * other.terms_[index]);
    }
  }

  void add_product(const Expansion& first, const Expansion& second, const double sign = 1.0) {
    for (std::size_t left = 0; left < first.size_; ++left) {
      for (std::size_t right = 0; right < second.size_; ++right) {
        add_product(sign * first.terms_[left], second.terms_[right]);
      }
    }
  }

  [[nodiscard]] bool zero() const noexcept { return size_ == 0; }
  [[nodiscard]] bool negative() const noexcept { return !zero() && terms_[size_ - 1] < 0.0; }
  [[nodiscard]] double rounded() const noexcept {
    double result = 0.0;
    for (std::size_t index = 0; index < size_; ++index) {
      result = result + terms_[index];
    }
    return result;
  }

private:
  // Circle discriminants need at most (4*2*2)+(4*4*2)=48 terms. A capsule side's exact endpoint
  // cross has eight terms, so its squared boundary predicate needs at most (8*8*2)+(4*2*2)=144.
  std::array<double, 160> terms_{};
  std::size_t size_{};
};

[[nodiscard]] double disc_boundary_radius(const double disc_radius, const double obstacle_radius) {
  require_radius(disc_radius, kMaximumPhysicalComponentMagnitude, "swept_geometry.disc_radius");
  require_radius(obstacle_radius, kMaximumPhysicalComponentMagnitude,
                 "swept_geometry.obstacle_radius");
  return obstacle_radius + disc_radius;
}

[[noreturn]] void throw_collapsed_roots() {
  throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                  "swept_geometry.root_separation",
                                  "two distinct boundary roots collapse to one binary64 time");
}

// The side equation remains linear, cross(m+d*t,s)=+/-r*sqrt(dot(s,s)). Only endpoint and
// parallel membership require exact signs; no degree-eight quadratic discriminant is formed.
[[nodiscard]] SweptBoundaryRoots capsule_side_roots(const double mx, const double my,
                                                    const double dx, const double dy,
                                                    const double radius, const double sx,
                                                    const double sy) {
  const double scale = std::max({std::abs(mx), std::abs(my), std::abs(dx), std::abs(dy), radius});
  int exponent = 0;
  static_cast<void>(std::frexp(scale, &exponent));
  const double x = std::scalbn(mx, -exponent);
  const double y = std::scalbn(my, -exponent);
  const double vx = std::scalbn(dx, -exponent);
  const double vy = std::scalbn(dy, -exponent);
  const double r = std::scalbn(radius, -exponent);
  require_precision_component(x, mx != 0.0, "swept_geometry.side.offset_x");
  require_precision_component(y, my != 0.0, "swept_geometry.side.offset_y");
  require_precision_component(vx, dx != 0.0, "swept_geometry.side.displacement_x");
  require_precision_component(vy, dy != 0.0, "swept_geometry.side.displacement_y");
  require_precision_component(r, radius != 0.0, "swept_geometry.side.radius");
  Expansion cross_start;
  cross_start.add_product(x, sy);
  cross_start.add_product(-y, sx);
  Expansion cross_motion;
  cross_motion.add_product(vx, sy);
  cross_motion.add_product(-vy, sx);
  Expansion radius_squared;
  radius_squared.add_product(r, r);
  Expansion length_squared;
  length_squared.add_product(sx, sx);
  length_squared.add_product(sy, sy);
  Expansion side_squared;
  side_squared.add_product(radius_squared, length_squared);
  Expansion start_boundary;
  start_boundary.add_product(cross_start, cross_start);
  start_boundary.add_expansion(side_squared, -1.0);
  SweptRootAccumulator roots;
  if (cross_motion.zero()) {
    if (start_boundary.zero()) {
      roots.add(0.0);
      roots.add(1.0);
    }
    return roots.finish();
  }
  Expansion cross_end = cross_start;
  cross_end.add_expansion(cross_motion);
  Expansion end_boundary;
  end_boundary.add_product(cross_end, cross_end);
  end_boundary.add_expansion(side_squared, -1.0);
  const double side_offset = std::sqrt(side_squared.rounded());
  std::optional<MotionTime> prior_side;
  for (const double side : {-side_offset, side_offset}) {
    const bool negative_side = side < 0.0;
    if (start_boundary.zero() && negative_side == cross_start.negative()) {
      roots.add(0.0);
      continue;
    }
    if (end_boundary.zero() && negative_side == cross_end.negative()) {
      roots.add(1.0);
      continue;
    }
    const SweptBoundaryRoots side_roots =
        swept_line_boundary_roots(cross_start.rounded(), cross_motion.rounded(), side);
    for (const MotionTime time : side_roots.times()) {
      if (radius > 0.0 && prior_side == time) {
        throw_collapsed_roots();
      }
      roots.add(time.value());
      prior_side = time;
    }
  }
  return roots.finish();
}

} // namespace

SweptBoundaryRoots swept_line_boundary_roots(const double start_coordinate,
                                             const double displacement_coordinate,
                                             const double boundary_coordinate) {
  require_finite(start_coordinate, "swept_geometry.line.start");
  require_finite(displacement_coordinate, "swept_geometry.line.displacement");
  require_finite(boundary_coordinate, "swept_geometry.line.boundary");
  SweptRootAccumulator roots;
  const double offset = boundary_coordinate - start_coordinate;
  require_finite(offset, "swept_geometry.line.offset");
  if (displacement_coordinate == 0.0) {
    if (offset == 0.0) {
      roots.add(0.0);
      roots.add(1.0);
    }
    return roots.finish();
  }
  // Reject roots outside the segment before division, so tiny nonzero displacements which
  // cannot reach a distant line are ordinary misses rather than overflowing irrelevant roots.
  if ((displacement_coordinate > 0.0 && (offset < 0.0 || offset > displacement_coordinate)) ||
      (displacement_coordinate < 0.0 && (offset > 0.0 || offset < displacement_coordinate))) {
    return roots.finish();
  }
  roots.add(offset / displacement_coordinate);
  return roots.finish();
}

SweptBoundaryRoots swept_circle_boundary_roots(const Vector2& start, const Vector2& displacement,
                                               const Vector2& center,
                                               const double boundary_radius) {
  return swept_circle_boundary_query(start, displacement, center, boundary_radius).roots;
}

CircleSweepResult swept_circle_boundary_query(const Vector2& start, const Vector2& displacement,
                                              const Vector2& center, const double boundary_radius) {
  require_radius(boundary_radius, 2.0 * kMaximumPhysicalComponentMagnitude,
                 "swept_geometry.boundary_radius");
  const double offset_x = start.x() - center.x();
  const double offset_y = start.y() - center.y();
  const bool initial_centers_coincident = offset_x == 0.0 && offset_y == 0.0;
  const double scale = std::max({std::abs(offset_x), std::abs(offset_y), std::abs(displacement.x()),
                                 std::abs(displacement.y()), boundary_radius});
  SweptRootAccumulator roots;
  if (scale == 0.0) {
    roots.add(0.0);
    roots.add(1.0);
    return {roots.finish(), CircleInitialRelation::kOnBoundary, CircleLineTopology::kStationary,
            CircleRadialMotion::kOrthogonal, initial_centers_coincident};
  }

  // Power-of-two scaling retains every accepted input bit. Dividing by an arbitrary maximum
  // would change an exact rational tangent before the discriminant was even evaluated.
  int exponent = 0;
  static_cast<void>(std::frexp(scale, &exponent));
  const double mx = std::scalbn(offset_x, -exponent);
  const double my = std::scalbn(offset_y, -exponent);
  const double dx = std::scalbn(displacement.x(), -exponent);
  const double dy = std::scalbn(displacement.y(), -exponent);
  const double radius = std::scalbn(boundary_radius, -exponent);
  require_precision_component(mx, offset_x != 0.0, "swept_geometry.offset_x");
  require_precision_component(my, offset_y != 0.0, "swept_geometry.offset_y");
  require_precision_component(dx, displacement.x() != 0.0, "swept_geometry.displacement_x");
  require_precision_component(dy, displacement.y() != 0.0, "swept_geometry.displacement_y");
  require_precision_component(radius, boundary_radius != 0.0, "swept_geometry.radius");

  Expansion exact_a;
  exact_a.add_product(dx, dx);
  exact_a.add_product(dy, dy);
  Expansion exact_b;
  exact_b.add_product(mx, dx);
  exact_b.add_product(my, dy);
  Expansion exact_radius_squared;
  exact_radius_squared.add_product(radius, radius);
  Expansion exact_c;
  exact_c.add_product(mx, mx);
  exact_c.add_product(my, my);
  exact_c.add_expansion(exact_radius_squared, -1.0);
  const CircleInitialRelation initial_relation =
      exact_c.zero()
          ? CircleInitialRelation::kOnBoundary
          : (exact_c.negative() ? CircleInitialRelation::kInside : CircleInitialRelation::kOutside);
  const CircleRadialMotion initial_radial_motion =
      exact_b.zero()
          ? CircleRadialMotion::kOrthogonal
          : (exact_b.negative() ? CircleRadialMotion::kApproaching : CircleRadialMotion::kReceding);
  const double a = exact_a.rounded();
  const double b = exact_b.rounded();
  const double c = exact_c.rounded();
  if (exact_a.zero()) {
    if (exact_c.zero()) {
      roots.add(0.0);
      roots.add(1.0);
    }
    return {roots.finish(), initial_relation, CircleLineTopology::kStationary,
            initial_radial_motion, initial_centers_coincident};
  }
  if (exact_c.zero()) {
    roots.add(0.0);
    roots.add((-2.0 * b) / a);
    return {roots.finish(), initial_relation,
            exact_b.zero() ? CircleLineTopology::kTangent : CircleLineTopology::kSecant,
            initial_radial_motion, initial_centers_coincident};
  }

  // Endpoint membership is an exact polynomial fact, not a proximity snap. Factor out an
  // exact t=1 root before rounding divisions which might otherwise put that root one ULP outside.
  Expansion endpoint;
  endpoint.add_expansion(exact_a);
  endpoint.add_expansion(exact_b, 2.0);
  endpoint.add_expansion(exact_c);
  if (endpoint.zero()) {
    const double other = c / a;
    Expansion difference = exact_c;
    difference.add_expansion(exact_a, -1.0);
    if (other >= 1.0 && difference.negative()) {
      throw_collapsed_roots();
    }
    roots.add(1.0);
    if (!difference.negative() && !difference.zero()) {
      // The other exact root is outside, even if its division rounds to 1.
      return {roots.finish(), initial_relation, CircleLineTopology::kSecant, initial_radial_motion,
              initial_centers_coincident};
    }
    roots.add(other);
    return {roots.finish(), initial_relation,
            difference.zero() ? CircleLineTopology::kTangent : CircleLineTopology::kSecant,
            initial_radial_motion, initial_centers_coincident};
  }

  // The half-quadratic is a*t*t + 2*b*t + c. Its discriminant is evaluated by the
  // two-dimensional identity a*r*r - cross(m,d)^2, not b*b-a*c: the latter erases a small
  // circle on a long radial sweep through cancellation and misreports two crossings as tangent.
  Expansion cross;
  cross.add_product(mx, dy);
  cross.add_product(-my, dx);
  Expansion exact_discriminant;
  exact_discriminant.add_product(exact_a, exact_radius_squared);
  exact_discriminant.add_product(cross, cross, -1.0);
  if (exact_discriminant.negative()) {
    return {roots.finish(), initial_relation, CircleLineTopology::kMiss, initial_radial_motion,
            initial_centers_coincident};
  }
  if (exact_discriminant.zero()) {
    roots.add(-b / a);
    return {roots.finish(), initial_relation, CircleLineTopology::kTangent, initial_radial_motion,
            initial_centers_coincident};
  }
  const double square_root = std::sqrt(exact_discriminant.rounded());
  const double q = b >= 0.0 ? -b - square_root : -b + square_root;
  const double first = q / a;
  const double second = c / q;
  if (first == second && first >= 0.0 && first <= 1.0) {
    throw_collapsed_roots();
  }
  roots.add(first);
  roots.add(second);
  return {roots.finish(), initial_relation, CircleLineTopology::kSecant, initial_radial_motion,
          initial_centers_coincident};
}

MotionTime map_motion_time(const MotionTime local, const MotionTime begin, const MotionTime end) {
  if (end < begin) {
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    "motion_time.interval", "motion interval must be ordered");
  }
  if (local == MotionTime::start()) {
    return begin;
  }
  if (local == MotionTime::end()) {
    return end;
  }
  const double remaining = end.value() - begin.value();
  return MotionTime::create(begin.value() + (remaining * local.value()));
}

SweptBoundaryRoots swept_capsule_boundary_roots(const Vector2& start, const Vector2& displacement,
                                                const Vector2& segment_start,
                                                const Vector2& segment_end,
                                                const double boundary_radius) {
  require_radius(boundary_radius, 2.0 * kMaximumPhysicalComponentMagnitude,
                 "swept_geometry.boundary_radius");
  const double segment_x = segment_end.x() - segment_start.x();
  const double segment_y = segment_end.y() - segment_start.y();
  const double segment_scale = std::max(std::abs(segment_x), std::abs(segment_y));
  if (segment_scale == 0.0) {
    return swept_circle_boundary_roots(start, displacement, segment_start, boundary_radius);
  }
  // Power-of-two direction scaling preserves exact rational side boundaries. Its two components
  // use the same degree-four precision floor as the circle and side predicates.
  int segment_exponent = 0;
  static_cast<void>(std::frexp(segment_scale, &segment_exponent));
  const double sx = std::scalbn(segment_x, -segment_exponent);
  const double sy = std::scalbn(segment_y, -segment_exponent);
  require_precision_component(sx, segment_x != 0.0, "swept_geometry.segment_x");
  require_precision_component(sy, segment_y != 0.0, "swept_geometry.segment_y");
  const double end_projection = (segment_x * sx) + (segment_y * sy);
  const double mx = start.x() - segment_start.x();
  const double my = start.y() - segment_start.y();
  const double projection_start = (mx * sx) + (my * sy);
  const double projection_motion = (displacement.x() * sx) + (displacement.y() * sy);
  SweptRootAccumulator roots;

  const SweptBoundaryRoots side_roots =
      capsule_side_roots(mx, my, displacement.x(), displacement.y(), boundary_radius, sx, sy);
  for (const MotionTime time : side_roots.times()) {
    const double projection = projection_start + (projection_motion * time.value());
    if (projection >= 0.0 && projection <= end_projection) {
      roots.add(time.value());
    }
  }

  const SweptBoundaryRoots start_roots =
      swept_circle_boundary_roots(start, displacement, segment_start, boundary_radius);
  for (const MotionTime time : start_roots.times()) {
    const double projection = projection_start + (projection_motion * time.value());
    if (projection <= 0.0) {
      roots.add(time.value());
    }
  }
  const SweptBoundaryRoots end_roots =
      swept_circle_boundary_roots(start, displacement, segment_end, boundary_radius);
  for (const MotionTime time : end_roots.times()) {
    const double projection = projection_start + (projection_motion * time.value());
    if (projection >= end_projection) {
      roots.add(time.value());
    }
  }
  return roots.finish();
}

SweptBoundaryRoots swept_disc_circle_boundary_roots(const Vector2& start,
                                                    const Vector2& displacement,
                                                    const double disc_radius, const Vector2& center,
                                                    const double obstacle_radius) {
  return swept_circle_boundary_roots(start, displacement, center,
                                     disc_boundary_radius(disc_radius, obstacle_radius));
}

SweptBoundaryRoots
swept_disc_capsule_boundary_roots(const Vector2& start, const Vector2& displacement,
                                  const double disc_radius, const Vector2& segment_start,
                                  const Vector2& segment_end, const double obstacle_radius) {
  return swept_capsule_boundary_roots(start, displacement, segment_start, segment_end,
                                      disc_boundary_radius(disc_radius, obstacle_radius));
}

} // namespace blob_royale::simulation
