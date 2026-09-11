#include "terrain_queries.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "swept_geometry.hpp"
#include "terrain_boundary.hpp"
#include "terrain_projection_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::simulation {
namespace detail {

// Keep raw coordinates until the point-valued API requests Vector2 materialization. The existing
// distance-only reader does not validate projected coordinates and shares this proven loop
// without acquiring a new exception/termination path on standalone corridors.
[[nodiscard]] RawCorridorProjection project_corridor_raw(const TerrainCorridor& corridor,
                                                         const Vector2& point) noexcept {
  const auto track = corridor.points();
  RawCorridorProjection nearest{track.front().x(), track.front().y(),
                                std::numeric_limits<double>::infinity()};
  for (std::size_t index = 1; index < track.size(); ++index) {
    const simulation::Vector2& a = track[index - 1];
    const simulation::Vector2& b = track[index];
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
    if (distance < nearest.distance) {
      nearest = {cx, cy, distance};
    }
  }
  return nearest;
}

} // namespace detail
namespace {

[[noreturn]] void precision_lost(const char* message) {
  throw SimulationValidationError(SimulationValidationCode::kTerrainGeometryPrecisionLost,
                                  "terrain_queries", message);
}

void require_arrangement_bound(const std::size_t count) {
  if (count > kMaximumTerrainArrangementElementCount) {
    throw SimulationValidationError(SimulationValidationCode::kTerrainBoundaryLimitExceeded,
                                    "terrain_queries.compile_boundary",
                                    "terrain arrangement exceeds its bounded temporary storage");
  }
}

[[nodiscard]] double distance(const Vector2& first, const Vector2& second) {
  const double x = first.x() - second.x();
  const double y = first.y() - second.y();
  return std::sqrt(x * x + y * y);
}

[[nodiscard]] double cross(const Vector2& first, const Vector2& second) {
  return first.x() * second.y() - first.y() * second.x();
}

// The two-product residual is explicit compensated arithmetic, not contraction. Comparing the
// rounded products first and their residuals on equality gives the exact determinant sign, so
// sorting circular directions is transitive even for nearly parallel directions.
[[nodiscard]] int cross_sign(const Vector2& first, const Vector2& second) {
  const double positive = first.x() * second.y();
  const double negative = first.y() * second.x();
  if (positive < negative) {
    return -1;
  }
  if (positive > negative) {
    return 1;
  }
  const double positive_residual = std::fma(first.x(), second.y(), -positive);
  const double negative_residual = std::fma(first.y(), second.x(), -negative);
  return (positive_residual > negative_residual) - (positive_residual < negative_residual);
}

[[nodiscard]] bool upper_half(const Vector2& direction) {
  return direction.y() > 0.0 || (direction.y() == 0.0 && direction.x() >= 0.0);
}

[[nodiscard]] bool angle_less(const Vector2& first, const Vector2& second) {
  if (upper_half(first) != upper_half(second)) {
    return upper_half(first);
  }
  return cross_sign(first, second) > 0;
}

[[nodiscard]] bool same_direction(const Vector2& first, const Vector2& second) {
  return cross_sign(first, second) == 0 && first.dot(second) > 0.0;
}

[[nodiscard]] bool direction_in_arc(const Vector2& direction, const Vector2& begin,
                                    const Vector2& end) {
  if (direction.x() == 0.0 && direction.y() == 0.0) {
    return false;
  }
  if (same_direction(direction, begin) || same_direction(direction, end)) {
    return true;
  }
  if (angle_less(begin, end)) {
    return angle_less(begin, direction) && angle_less(direction, end);
  }
  return angle_less(begin, direction) || angle_less(direction, end);
}

[[nodiscard]] Vector2 circle_point(const Vector2& center, const double radius,
                                   const Vector2& direction) {
  const double magnitude = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
  if (magnitude == 0.0) {
    precision_lost("a circle direction collapsed to zero");
  }
  return Vector2::create(center.x() + radius * (direction.x() / magnitude),
                         center.y() + radius * (direction.y() / magnitude));
}

[[nodiscard]] double segment_distance(const Vector2& point, const Vector2& begin,
                                      const Vector2& end) {
  const double dx = end.x() - begin.x();
  const double dy = end.y() - begin.y();
  const double wx = point.x() - begin.x();
  const double wy = point.y() - begin.y();
  double t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
  if (t < 0.0) {
    t = 0.0;
  } else if (t > 1.0) {
    t = 1.0;
  }
  const double cx = begin.x() + (dx * t);
  const double cy = begin.y() + (dy * t);
  const double ex = point.x() - cx;
  const double ey = point.y() - cy;
  return std::sqrt(ex * ex + ey * ey);
}

[[nodiscard]] std::optional<TerrainSupportInterval>
convex_interval(const bool start_supported, const bool end_supported,
                const std::span<const MotionTime> roots) {
  if (start_supported && end_supported) {
    return TerrainSupportInterval{MotionTime::start(), MotionTime::end()};
  }
  if (roots.empty()) {
    if (start_supported != end_supported) {
      precision_lost("a convex primitive changed support without a representable root");
    }
    return std::nullopt;
  }
  if (start_supported) {
    return TerrainSupportInterval{MotionTime::start(), roots.back()};
  }
  if (end_supported) {
    return TerrainSupportInterval{roots.front(), MotionTime::end()};
  }
  return TerrainSupportInterval{roots.front(), roots.back()};
}

void merge_intervals(std::vector<TerrainSupportInterval>& intervals) {
  std::sort(intervals.begin(), intervals.end(), [](const auto& first, const auto& second) {
    return first.begin < second.begin || (first.begin == second.begin && first.end < second.end);
  });
  std::vector<TerrainSupportInterval> merged;
  for (const auto& interval : intervals) {
    if (!merged.empty() && interval.begin <= merged.back().end) {
      merged.back().end = std::max(merged.back().end, interval.end);
    } else {
      merged.push_back(interval);
    }
  }
  intervals = std::move(merged);
}

[[nodiscard]] std::optional<TerrainSupportInterval>
axis_interval(const double start, const double displacement, const double extent) {
  std::vector<MotionTime> roots;
  for (const double boundary : {0.0, extent}) {
    const auto boundary_roots = swept_line_boundary_roots(start, displacement, boundary);
    roots.insert(roots.end(), boundary_roots.times().begin(), boundary_roots.times().end());
  }
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  const double finish = start + displacement;
  return convex_interval(start >= 0.0 && start <= extent, finish >= 0.0 && finish <= extent, roots);
}

void intersect_interval(std::vector<TerrainSupportInterval>& intervals,
                        const std::optional<TerrainSupportInterval>& clipping) {
  if (!clipping) {
    intervals.clear();
    return;
  }
  std::vector<TerrainSupportInterval> clipped;
  for (const auto& interval : intervals) {
    const auto begin = std::max(interval.begin, clipping->begin);
    const auto end = std::min(interval.end, clipping->end);
    if (begin <= end) {
      clipped.push_back({begin, end});
    }
  }
  intervals = std::move(clipped);
}

void subtract_open_hole(std::vector<TerrainSupportInterval>& intervals,
                        const TerrainSupportInterval& hole, const bool remove_begin,
                        const bool remove_end) {
  std::vector<TerrainSupportInterval> remaining;
  for (const auto& interval : intervals) {
    if (hole.end < interval.begin || hole.begin > interval.end) {
      remaining.push_back(interval);
      continue;
    }
    if (hole.begin >= interval.begin && !remove_begin) {
      remaining.push_back({interval.begin, std::min(interval.end, hole.begin)});
    }
    if (hole.end <= interval.end && !remove_end) {
      remaining.push_back({std::max(interval.begin, hole.end), interval.end});
    }
  }
  intervals = std::move(remaining);
}

// Every capsule and hole is a shape, not a second authoring value. Shapes and raw curves only
// exist during bounded compilation. Curve incidences let the compiler classify exact local sides
// symbolically; it never probes a finite normal offset or samples a grid of candidate locations.
enum class ShapeKind { kEnvelope, kCapsule, kHole };
struct Shape final {
  ShapeKind kind;
  Vector2 begin;
  Vector2 end;
  double radius;
};
struct RawLine final {
  Vector2 begin;
  Vector2 end;
  // A capsule side's rounded offset endpoints need not subtract back to its authored axis.
  Vector2 inward_normal;
};
struct RawArc final {
  Vector2 center;
  double radius;
  Vector2 begin_direction;
  Vector2 end_direction;
};
struct Cut final {
  Vector2 point;
  Vector2 direction;
  MotionTime time;
};
struct RawFeature final {
  BoundaryFeatureId id;
  std::size_t shape;
  std::variant<RawLine, RawArc> geometry;
  std::vector<Cut> cuts;
};
struct LocalSupport final {
  bool on;
  bool left;
  bool right;
};

[[nodiscard]] Vector2 feature_normal(const RawFeature& feature, const Vector2& point) {
  if (const auto* line = std::get_if<RawLine>(&feature.geometry)) {
    return line->inward_normal;
  }
  return std::get<RawArc>(feature.geometry).center - point;
}

[[nodiscard]] bool line_contains(const RawLine& line, const Vector2& point) {
  const Vector2 direction = line.end - line.begin;
  const Vector2 offset = point - line.begin;
  return cross_sign(direction, offset) == 0 && offset.dot(direction) >= 0.0 &&
         (point - line.end).dot(direction) <= 0.0;
}

enum class LineAxis { kNeither, kHorizontal, kVertical };

[[nodiscard]] LineAxis line_axis(const RawLine& line) {
  // Rounded offset endpoints alone cannot certify an axis: a sloped authored side may lose
  // its small component on translation. The retained construction normal must agree too.
  if (line.begin.x() == line.end.x() && line.inward_normal.y() == 0.0 &&
      line.inward_normal.x() != 0.0) {
    return LineAxis::kVertical;
  }
  if (line.begin.y() == line.end.y() && line.inward_normal.x() == 0.0 &&
      line.inward_normal.y() != 0.0) {
    return LineAxis::kHorizontal;
  }
  return LineAxis::kNeither;
}

[[nodiscard]] bool axis_line_contains(const RawLine& line, const Vector2& point) {
  const auto axis = line_axis(line);
  if (axis == LineAxis::kVertical) {
    return point.x() == line.begin.x() && point.y() >= std::min(line.begin.y(), line.end.y()) &&
           point.y() <= std::max(line.begin.y(), line.end.y());
  }
  if (axis == LineAxis::kHorizontal) {
    return point.y() == line.begin.y() && point.x() >= std::min(line.begin.x(), line.end.x()) &&
           point.x() <= std::max(line.begin.x(), line.end.x());
  }
  return false;
}

[[nodiscard]] bool coincident_at(const RawFeature& source, const RawFeature& other,
                                 const Vector2& point) {
  if (const auto* first = std::get_if<RawLine>(&source.geometry)) {
    const auto* second = std::get_if<RawLine>(&other.geometry);
    return second != nullptr &&
           cross_sign(first->end - first->begin, second->end - second->begin) == 0 &&
           cross_sign(first->end - first->begin, second->begin - first->begin) == 0 &&
           line_contains(*second, point);
  }
  const auto* first = std::get_if<RawArc>(&source.geometry);
  const auto* second = std::get_if<RawArc>(&other.geometry);
  return second != nullptr && first->center == second->center && first->radius == second->radius &&
         direction_in_arc(point - second->center, second->begin_direction, second->end_direction);
}

[[nodiscard]] bool strictly_inside(const Shape& shape, const Vector2& point) {
  switch (shape.kind) {
  case ShapeKind::kEnvelope:
    return point.x() > 0.0 && point.x() < shape.end.x() && point.y() > 0.0 &&
           point.y() < shape.end.y();
  case ShapeKind::kCapsule:
    return segment_distance(point, shape.begin, shape.end) < shape.radius;
  case ShapeKind::kHole:
    return distance(point, shape.begin) < shape.radius;
  }
  precision_lost("unknown terrain shape");
}

[[nodiscard]] bool exactly_on_boundary(const Shape& shape, const Vector2& point) {
  switch (shape.kind) {
  case ShapeKind::kEnvelope:
    return ((point.x() == 0.0 || point.x() == shape.end.x()) && point.y() >= 0.0 &&
            point.y() <= shape.end.y()) ||
           ((point.y() == 0.0 || point.y() == shape.end.y()) && point.x() >= 0.0 &&
            point.x() <= shape.end.x());
  case ShapeKind::kCapsule:
    return segment_distance(point, shape.begin, shape.end) == shape.radius;
  case ShapeKind::kHole:
    return distance(point, shape.begin) == shape.radius;
  }
  precision_lost("unknown terrain shape");
}

[[nodiscard]] LocalSupport local_support(const Vector2& point, const Vector2& normal,
                                         const RawFeature& source,
                                         const std::span<const RawFeature> features,
                                         const std::span<const Shape> shapes,
                                         const TerrainGround ground) {
  LocalSupport envelope{false, false, false};
  LocalSupport positive{ground == TerrainGround::kSolid, ground == TerrainGround::kSolid,
                        ground == TerrainGround::kSolid};
  LocalSupport holes{false, false, false};
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    const Shape& shape = shapes[index];
    const RawFeature* incident = nullptr;
    for (const RawFeature& feature : features) {
      if (feature.shape == index &&
          (feature.id == source.id || coincident_at(source, feature, point))) {
        incident = &feature;
        break;
      }
    }
    LocalSupport membership{};
    if (incident != nullptr) {
      const bool aligned = normal.dot(feature_normal(*incident, point)) > 0.0;
      membership = {shape.kind != ShapeKind::kHole, aligned, !aligned};
    } else {
      if (exactly_on_boundary(shape, point)) {
        precision_lost("an open arrangement cell rounded onto a nonincident boundary");
      }
      const bool inside = strictly_inside(shape, point);
      membership = {inside, inside, inside};
    }
    auto combine_union = [&membership](LocalSupport& target) {
      target.on = target.on || membership.on;
      target.left = target.left || membership.left;
      target.right = target.right || membership.right;
    };
    switch (shape.kind) {
    case ShapeKind::kEnvelope:
      envelope = membership;
      break;
    case ShapeKind::kCapsule:
      combine_union(positive);
      break;
    case ShapeKind::kHole:
      combine_union(holes);
      break;
    }
  }
  return {envelope.on && positive.on && !holes.on, envelope.left && positive.left && !holes.left,
          envelope.right && positive.right && !holes.right};
}

[[nodiscard]] bool point_supported_by_arrangement(const Vector2& point,
                                                  const std::span<const RawFeature> features,
                                                  const std::span<const Shape> shapes,
                                                  const TerrainGround ground) {
  bool positive = ground == TerrainGround::kSolid;
  for (std::size_t index = 0; index < shapes.size(); ++index) {
    const auto& shape = shapes[index];
    bool boundary = false;
    for (const auto& feature : features) {
      if (feature.shape == index &&
          std::any_of(feature.cuts.begin(), feature.cuts.end(),
                      [&point](const auto& cut) { return cut.point == point; })) {
        boundary = true;
        break;
      }
    }
    const bool inside = !boundary && strictly_inside(shape, point);
    if (shape.kind == ShapeKind::kEnvelope && !inside && !boundary) {
      return false;
    }
    if (shape.kind == ShapeKind::kCapsule) {
      positive = positive || inside || boundary;
    }
    if (shape.kind == ShapeKind::kHole && inside) {
      return false;
    }
  }
  return positive;
}

[[nodiscard]] bool direction_inside_sector(const Vector2& direction,
                                           const detail::TerrainSupportedSector& sector) {
  return cross_sign(sector.begin_direction, direction) > 0 &&
         cross_sign(direction, sector.end_direction) > 0;
}

struct VertexSector final {
  Vector2 point;
  std::optional<detail::TerrainSupportedSector> sector;
};

struct VertexIncidence final {
  std::size_t shape;
  Vector2 tangent;
};

[[nodiscard]] bool sector_inside_halfspace(const Vector2& begin, const Vector2& tangent) {
  const int side = cross_sign(begin, tangent);
  // Every incident tangent and its opposite is an angular breakpoint. The adjacent open
  // sector cannot cross this halfspace boundary: immediately CCW from tangent is outside,
  // while immediately CCW from its opposite is inside. No rounded midpoint is needed.
  return side > 0 || (side == 0 && !same_direction(begin, tangent));
}

[[nodiscard]] bool point_less(const Vector2& first, const Vector2& second) {
  return first.x() < second.x() || (first.x() == second.x() && first.y() < second.y());
}

[[nodiscard]] std::optional<detail::TerrainSupportedSector>
vertex_supported_sector(const Vector2& point, const std::span<const RawFeature> features,
                        const std::span<const Shape> shapes, const TerrainGround ground,
                        const std::size_t existing_storage) {
  std::vector<VertexIncidence> incident;
  std::vector<Vector2> rays;
  for (const auto& feature : features) {
    const auto cut = std::find_if(feature.cuts.begin(), feature.cuts.end(),
                                  [&point](const auto& value) { return value.point == point; });
    if (cut != feature.cuts.end()) {
      // A smooth side/cap join shares its authored normal. Subtracting rounded endpoints
      // would manufacture distinct tangent rays and a microscopic false angular sector.
      const auto normal = std::holds_alternative<RawLine>(feature.geometry)
                              ? feature_normal(feature, point)
                              : -cut->direction;
      const auto tangent = Vector2::create(-normal.y(), normal.x());
      if (tangent.x() == 0.0 && tangent.y() == 0.0) {
        precision_lost("a vertex tangent has no representable direction");
      }
      incident.push_back({feature.shape, tangent});
      rays.push_back(tangent);
      rays.push_back(-tangent);
      require_arrangement_bound(existing_storage + incident.size() + rays.size());
    }
  }
  std::sort(rays.begin(), rays.end(), angle_less);
  rays.erase(std::unique(rays.begin(), rays.end(), same_direction), rays.end());
  for (std::size_t ray = 0; ray < rays.size(); ++ray) {
    const auto& begin = rays[ray];
    const auto& end = rays[(ray + 1) % rays.size()];
    bool envelope = false;
    bool positive = ground == TerrainGround::kSolid;
    bool holes = false;
    for (std::size_t shape_index = 0; shape_index < shapes.size(); ++shape_index) {
      const Shape& shape = shapes[shape_index];
      bool touches = false;
      bool inside = true;
      for (const auto& incidence : incident) {
        if (incidence.shape == shape_index) {
          touches = true;
          // Keep every incidence even when equal angular breakpoints are deduplicated.
          inside = inside && sector_inside_halfspace(begin, incidence.tangent);
        }
      }
      if (!touches) {
        if (exactly_on_boundary(shape, point)) {
          precision_lost("a vertex rounded onto a boundary without preserved incidence");
        }
        inside = strictly_inside(shape, point);
      }
      switch (shape.kind) {
      case ShapeKind::kEnvelope:
        envelope = inside;
        break;
      case ShapeKind::kCapsule:
        positive = positive || inside;
        break;
      case ShapeKind::kHole:
        holes = holes || inside;
        break;
      }
    }
    if (envelope && positive && !holes) {
      // Only the canonical selected supported sector needs a representable recovery ray.
      // Failure cannot select a later sector or silently discard a supported branch.
      const auto zero = Vector2::create(0.0, 0.0);
      const auto interior = cross_sign(begin, end) == 0
                                ? Vector2::create(-begin.y(), begin.x())
                                : circle_point(zero, 1.0, begin) + circle_point(zero, 1.0, end);
      const detail::TerrainSupportedSector sector{begin, end, interior};
      if (!direction_inside_sector(interior, sector)) {
        precision_lost("a vertex sector has no representable strict interior direction");
      }
      return sector; // Canonical global angular order; never a discovery/pointer order.
    }
  }
  // No strict first-order cone: this also permits curved cusps, not only rims/isolated points.
  return std::nullopt;
}

[[nodiscard]] std::vector<VertexSector>
compile_vertex_sectors(const std::span<const RawFeature> features,
                       const std::span<const Shape> shapes, const TerrainGround ground,
                       const std::size_t incidence_count, std::size_t& retained_vertex_slots) {
  std::vector<VertexSector> vertices;
  for (const auto& feature : features) {
    for (const auto& cut : feature.cuts) {
      vertices.push_back({cut.point, std::nullopt});
      require_arrangement_bound(incidence_count + vertices.size());
    }
  }
  // Erase/dedup does not release the backing storage. Charge deterministic retained logical
  // slots, never allocator/vector growth policy: these bounds do not claim exact heap bytes.
  retained_vertex_slots = vertices.size();
  std::sort(vertices.begin(), vertices.end(), [](const auto& first, const auto& second) {
    return point_less(first.point, second.point);
  });
  vertices.erase(std::unique(vertices.begin(), vertices.end(),
                             [](const auto& first, const auto& second) {
                               return first.point == second.point;
                             }),
                 vertices.end());
  for (auto& vertex : vertices) {
    vertex.sector = vertex_supported_sector(vertex.point, features, shapes, ground,
                                            incidence_count + retained_vertex_slots);
  }
  return vertices;
}

[[nodiscard]] std::optional<detail::TerrainSupportedSector>
sector_at(const std::span<const VertexSector> vertices, const Vector2& point) {
  const auto found = std::lower_bound(
      vertices.begin(), vertices.end(), point,
      [](const auto& vertex, const auto& target) { return point_less(vertex.point, target); });
  if (found == vertices.end() || found->point != point) {
    precision_lost("an exposed boundary endpoint lost its arrangement vertex");
  }
  return found->sector;
}

void add_cut(RawFeature& feature, const Vector2& point, std::size_t& incidence_count) {
  if (std::any_of(feature.cuts.begin(), feature.cuts.end(),
                  [&point](const auto& cut) { return cut.point == point; })) {
    return;
  }
  if (auto* line = std::get_if<RawLine>(&feature.geometry)) {
    const Vector2 direction = line->end - line->begin;
    const bool use_x = std::abs(direction.x()) >= std::abs(direction.y());
    const auto roots = swept_line_boundary_roots(use_x ? line->begin.x() : line->begin.y(),
                                                 use_x ? direction.x() : direction.y(),
                                                 use_x ? point.x() : point.y());
    if (roots.empty()) {
      return;
    }
    feature.cuts.push_back({point, Vector2::create(0.0, 0.0), *roots.first()});
  } else {
    const auto& arc = std::get<RawArc>(feature.geometry);
    const auto direction = point - arc.center;
    if (!direction_in_arc(direction, arc.begin_direction, arc.end_direction)) {
      return;
    }
    feature.cuts.push_back({point, direction, MotionTime::start()});
  }
  require_arrangement_bound(++incidence_count);
}

[[nodiscard]] Vector2 line_point(const RawLine& line, const MotionTime time) {
  if (time == MotionTime::start()) {
    return line.begin;
  }
  if (time == MotionTime::end()) {
    return line.end;
  }
  return line.begin + (line.end - line.begin) * time.value();
}

void intersect_features(RawFeature& first, RawFeature& second, const std::span<const Shape> shapes,
                        std::size_t& incidence_count) {
  auto add_shared = [&](const Vector2& point) {
    add_cut(first, point, incidence_count);
    add_cut(second, point, incidence_count);
  };
  auto* first_line = std::get_if<RawLine>(&first.geometry);
  auto* second_line = std::get_if<RawLine>(&second.geometry);
  if (first_line != nullptr && second_line != nullptr) {
    const auto first_direction = first_line->end - first_line->begin;
    const auto second_direction = second_line->end - second_line->begin;
    if (cross_sign(first_direction, second_direction) == 0) {
      if (cross_sign(first_direction, second_line->begin - first_line->begin) == 0) {
        for (const auto& point :
             {first_line->begin, first_line->end, second_line->begin, second_line->end}) {
          if (line_contains(*first_line, point) && line_contains(*second_line, point)) {
            add_shared(point);
          }
        }
      }
      return;
    }
    // An endpoint on an axis-aligned segment is certified by its literal constant coordinate
    // and closed coordinate bounds, without rounded line-offset subtraction. Nonparallel
    // segments have at most one such intersection; ambiguous identities must remain visible.
    std::optional<Vector2> endpoint_intersection;
    auto retain_endpoint = [&](const Vector2& endpoint, const RawLine& other) {
      if (axis_line_contains(other, endpoint)) {
        if (endpoint_intersection && *endpoint_intersection != endpoint) {
          precision_lost("nonparallel boundary lines have conflicting endpoint incidences");
        }
        endpoint_intersection = endpoint;
      }
    };
    retain_endpoint(first_line->begin, *second_line);
    retain_endpoint(first_line->end, *second_line);
    retain_endpoint(second_line->begin, *first_line);
    retain_endpoint(second_line->end, *first_line);
    if (endpoint_intersection) {
      add_shared(*endpoint_intersection);
      return;
    }
    const double start_coordinate = cross(first_line->begin - second_line->begin, second_direction);
    const double displacement_coordinate = cross(first_direction, second_direction);
    const auto roots = swept_line_boundary_roots(start_coordinate, displacement_coordinate, 0.0);
    for (const auto root : roots.times()) {
      const Vector2 point = line_point(*first_line, root);
      const auto offset = point - second_line->begin;
      const double projection = offset.dot(second_direction);
      if (projection >= 0.0 && projection <= second_direction.dot(second_direction)) {
        add_shared(point);
      }
    }
    return;
  }
  if (first_line != nullptr || second_line != nullptr) {
    const RawLine& line = first_line != nullptr ? *first_line : *second_line;
    const RawArc& arc = std::get<RawArc>(first_line != nullptr ? second.geometry : first.geometry);
    const Shape& line_shape = shapes[first_line != nullptr ? first.shape : second.shape];
    if (line_shape.kind == ShapeKind::kCapsule && line_shape.radius == arc.radius &&
        (arc.center == line_shape.begin || arc.center == line_shape.end)) {
      // A side and an equal-radius cap anchored at its authored endpoint are tangent by
      // construction. Re-solving the rounded side coordinate against the radius would create
      // two fictitious tiny crossings. Preserve this exact provenance identity instead.
      const bool center_at_begin = arc.center == line_shape.begin;
      const auto axis = line_shape.end - line_shape.begin;
      const bool line_forward = (line.end - line.begin).dot(axis) > 0.0;
      const auto tangent = center_at_begin == line_forward ? line.begin : line.end;
      if (direction_in_arc(tangent - arc.center, arc.begin_direction, arc.end_direction)) {
        add_shared(tangent);
      }
      return;
    }
    // A literal axis line through the circle center is a diameter. Its two cardinal
    // directions exhaust the circle intersections; append_arc already retained every one
    // included by this arc. Reuse that construction, not a rounded root's inferred ray.
    const auto axis_alignment = line_axis(line);
    const bool vertical_diameter =
        axis_alignment == LineAxis::kVertical && line.begin.x() == arc.center.x();
    const bool horizontal_diameter =
        axis_alignment == LineAxis::kHorizontal && line.begin.y() == arc.center.y();
    if (vertical_diameter || horizontal_diameter) {
      const auto& arc_feature = first_line != nullptr ? second : first;
      const auto axis = vertical_diameter ? Vector2::create(0.0, 1.0) : Vector2::create(1.0, 0.0);
      const std::array directions{axis, -axis};
      std::array<std::optional<Vector2>, 2> intersections;
      for (std::size_t index = 0; index < directions.size(); ++index) {
        const auto& direction = directions[index];
        if (!direction_in_arc(direction, arc.begin_direction, arc.end_direction)) {
          continue;
        }
        const auto point = circle_point(arc.center, arc.radius, direction);
        if (std::none_of(arc_feature.cuts.begin(), arc_feature.cuts.end(),
                         [&point, &direction](const auto& cut) {
                           return cut.point == point && cut.direction == direction;
                         })) {
          precision_lost("an axis-circle intersection lost its authored cardinal identity");
        }
        intersections[index] = point;
      }
      // Validate the complete construction certificate before mutating either feature.
      for (const auto& point : intersections) {
        if (point && axis_line_contains(line, *point)) {
          add_shared(*point);
        }
      }
      return;
    }
    const auto displacement = line.end - line.begin;
    const auto roots =
        swept_circle_boundary_roots(line.begin, displacement, arc.center, arc.radius);
    for (const auto root : roots.times()) {
      const Vector2 point = line_point(line, root);
      if (direction_in_arc(point - arc.center, arc.begin_direction, arc.end_direction)) {
        add_shared(point);
      }
    }
    return;
  }
  const RawArc& first_arc = std::get<RawArc>(first.geometry);
  const RawArc& second_arc = std::get<RawArc>(second.geometry);
  if (first_arc.center == second_arc.center) {
    if (first_arc.radius == second_arc.radius) {
      const auto first_cuts = first.cuts;
      const auto second_cuts = second.cuts;
      for (const auto& cut : first_cuts) {
        add_cut(second, cut.point, incidence_count);
      }
      for (const auto& cut : second_cuts) {
        add_cut(first, cut.point, incidence_count);
      }
    }
    return;
  }
  const Vector2 centers = second_arc.center - first_arc.center;
  const double center_distance = std::sqrt(centers.x() * centers.x() + centers.y() * centers.y());
  if (center_distance == 0.0) {
    precision_lost("distinct circle centers have no representable separation");
  }
  if (center_distance > first_arc.radius + second_arc.radius ||
      center_distance < std::abs(first_arc.radius - second_arc.radius)) {
    return;
  }
  const double center_squared = centers.x() * centers.x() + centers.y() * centers.y();
  const double axis_fraction = (center_squared + first_arc.radius * first_arc.radius -
                                second_arc.radius * second_arc.radius) /
                               (2.0 * center_squared);
  const Vector2 foot = first_arc.center + centers * axis_fraction;
  const Vector2 normal =
      Vector2::create(-centers.y() / center_distance, centers.x() / center_distance);
  const Vector2 start = foot - normal * first_arc.radius;
  const Vector2 displacement = normal * (2.0 * first_arc.radius);
  const auto roots =
      swept_circle_boundary_roots(start, displacement, first_arc.center, first_arc.radius);
  for (const auto root : roots.times()) {
    const auto point = start + displacement * root.value();
    if (direction_in_arc(point - first_arc.center, first_arc.begin_direction,
                         first_arc.end_direction) &&
        direction_in_arc(point - second_arc.center, second_arc.begin_direction,
                         second_arc.end_direction)) {
      add_shared(point);
    }
  }
}

void append_line(std::vector<RawFeature>& features, const std::size_t shape, const Vector2& begin,
                 const Vector2& end, const Vector2& inward_normal, std::size_t& incidence_count) {
  if (begin == end) {
    precision_lost("a terrain boundary line collapsed");
  }
  RawFeature feature{
      BoundaryFeatureId{features.size() + 1}, shape, RawLine{begin, end, inward_normal}, {}};
  add_cut(feature, begin, incidence_count);
  add_cut(feature, end, incidence_count);
  features.push_back(std::move(feature));
}

void append_arc(std::vector<RawFeature>& features, const std::size_t shape, const Vector2& center,
                const double radius, const Vector2& begin, const Vector2& end,
                std::size_t& incidence_count) {
  RawFeature feature{
      BoundaryFeatureId{features.size() + 1}, shape, RawArc{center, radius, begin, end}, {}};
  // Keep authored directions on the endpoints: a rounded circle point need not subtract back to
  // exactly the same direction. These identities are geometric, not an epsilon merge.
  feature.cuts.push_back({circle_point(center, radius, begin), begin, MotionTime::start()});
  feature.cuts.push_back({circle_point(center, radius, end), end, MotionTime::start()});
  incidence_count += 2;
  require_arrangement_bound(incidence_count);
  for (const auto& cardinal : {Vector2::create(1.0, 0.0), Vector2::create(0.0, 1.0),
                               Vector2::create(-1.0, 0.0), Vector2::create(0.0, -1.0)}) {
    if (direction_in_arc(cardinal, begin, end)) {
      feature.cuts.push_back(
          {circle_point(center, radius, cardinal), cardinal, MotionTime::start()});
      require_arrangement_bound(++incidence_count);
    }
  }
  features.push_back(std::move(feature));
}

[[nodiscard]] bool witness_less(const TerrainPointWitness& first,
                                const TerrainPointWitness& second) {
  if (first.distance != second.distance) {
    return first.distance < second.distance;
  }
  if (first.point.x() != second.point.x()) {
    return first.point.x() < second.point.x();
  }
  if (first.point.y() != second.point.y()) {
    return first.point.y() < second.point.y();
  }
  return first.feature < second.feature;
}

struct BoundaryCandidate final {
  TerrainPointWitness witness;
  Vector2 supported_direction;
  bool is_vertex;
  std::optional<detail::TerrainSupportedSector> sector;
};

[[nodiscard]] std::optional<BoundaryCandidate> nearest_boundary(const TerrainDefinition& terrain,
                                                                const Vector2& point) {
  std::optional<BoundaryCandidate> nearest;
  auto consider = [&](const Vector2& candidate, const BoundaryFeatureId feature,
                      const Vector2& supported_direction, const bool is_vertex,
                      const std::optional<detail::TerrainSupportedSector>& sector) {
    TerrainPointWitness witness{candidate, distance(point, candidate), feature};
    if (!nearest || witness_less(witness, nearest->witness)) {
      nearest = BoundaryCandidate{witness, supported_direction, is_vertex, sector};
    }
  };
  const auto& boundary = detail::TerrainQueryAccess::boundary(terrain);
  for (const auto& span : boundary.spans) {
    if (const auto* line = std::get_if<detail::TerrainLineSpan>(&span.geometry)) {
      const Vector2 direction = line->end - line->begin;
      const double parameter =
          std::clamp((point - line->begin).dot(direction) / direction.dot(direction), 0.0, 1.0);
      const auto supported_direction = Vector2::create(-direction.y() * span.supported_side,
                                                       direction.x() * span.supported_side);
      const auto projected = line_point(RawLine{line->begin, line->end, supported_direction},
                                        MotionTime::create(parameter));
      const bool at_begin = projected == line->begin;
      const bool at_end = projected == line->end;
      consider(projected, span.feature, supported_direction, at_begin || at_end,
               at_begin ? span.begin_sector : (at_end ? span.end_sector : std::nullopt));
    } else {
      const auto& arc = std::get<detail::TerrainArcSpan>(span.geometry);
      consider(arc.begin, span.feature, (arc.center - arc.begin) * span.supported_side, true,
               span.begin_sector);
      consider(arc.end, span.feature, (arc.center - arc.end) * span.supported_side, true,
               span.end_sector);
      const auto direction = point - arc.center;
      if (direction_in_arc(direction, arc.begin_direction, arc.end_direction) &&
          !same_direction(direction, arc.begin_direction) &&
          !same_direction(direction, arc.end_direction)) {
        const auto projected = circle_point(arc.center, arc.radius, direction);
        consider(projected, span.feature, (arc.center - projected) * span.supported_side, false,
                 std::nullopt);
      }
    }
  }
  for (const auto& isolated : boundary.isolated_points) {
    consider(isolated.point, isolated.feature, Vector2::create(0.0, 0.0), true, std::nullopt);
  }
  return nearest;
}

[[nodiscard]] TerrainPointWitness vertex_supported_witness(const TerrainDefinition& terrain,
                                                           const Vector2& query,
                                                           const BoundaryCandidate& candidate) {
  if (!candidate.sector) {
    precision_lost("the selected vertex has no strict supported recovery sector");
  }
  const auto& sector = *candidate.sector;
  const auto& origin = candidate.witness.point;
  const auto& direction = sector.interior_direction;
  const auto limit_coordinate = [](const double coordinate, const double component) {
    const double target = component > 0.0   ? std::numeric_limits<double>::infinity()
                          : component < 0.0 ? -std::numeric_limits<double>::infinity()
                                            : coordinate;
    double limit = coordinate;
    for (std::size_t step = 0; step < kMaximumTerrainWitnessRoundingStepCount; ++step) {
      limit = std::nextafter(limit, target);
    }
    return limit;
  };
  const auto limit = Vector2::create(limit_coordinate(origin.x(), direction.x()),
                                     limit_coordinate(origin.y(), direction.y()));
  double maximum_scale = std::numeric_limits<double>::infinity();
  if (direction.x() != 0.0) {
    maximum_scale = std::min(maximum_scale, std::abs((limit.x() - origin.x()) / direction.x()));
  }
  if (direction.y() != 0.0) {
    maximum_scale = std::min(maximum_scale, std::abs((limit.y() - origin.y()) / direction.y()));
  }
  for (std::size_t step = 1; step <= kMaximumTerrainWitnessRoundingStepCount; ++step) {
    const double fraction =
        static_cast<double>(step) / static_cast<double>(kMaximumTerrainWitnessRoundingStepCount);
    const auto corrected = origin + direction * (maximum_scale * fraction);
    const auto displacement = corrected - origin;
    if (corrected.x() < std::min(origin.x(), limit.x()) ||
        corrected.x() > std::max(origin.x(), limit.x()) ||
        corrected.y() < std::min(origin.y(), limit.y()) ||
        corrected.y() > std::max(origin.y(), limit.y()) ||
        !direction_inside_sector(displacement, sector)) {
      continue;
    }
    if (terrain_supports_point(terrain, corrected)) {
      return TerrainPointWitness{corrected, distance(query, corrected), candidate.witness.feature};
    }
  }
  precision_lost("the selected vertex sector ray exhausted its directed rounding budget");
}

[[nodiscard]] TerrainPointWitness supported_witness(const TerrainDefinition& terrain,
                                                    const Vector2& query,
                                                    const BoundaryCandidate& candidate,
                                                    const bool clearance_query) {
  TerrainPointWitness witness = candidate.witness;
  if (terrain_supports_point(terrain, witness.point)) {
    return witness;
  }
  if (!clearance_query && candidate.is_vertex) {
    return vertex_supported_witness(terrain, query, candidate);
  }
  const auto directed_target = [](const double coordinate, const double direction) {
    if (direction > 0.0) {
      return std::numeric_limits<double>::infinity();
    }
    if (direction < 0.0) {
      return -std::numeric_limits<double>::infinity();
    }
    return coordinate;
  };
  // Selection precedes correction. No farther curve replaces a selected unrepresentable one.
  // These are directed rounding steps of one analytic witness, not terrain/path samples. For
  // clearance, each absolute coordinate difference from query decreases monotonically; the
  // written squared-sum/sqrt distance therefore cannot increase. Feature identity never changes.
  for (std::size_t step = 0; step < kMaximumTerrainWitnessRoundingStepCount; ++step) {
    const double target_x =
        clearance_query ? query.x()
                        : directed_target(witness.point.x(), candidate.supported_direction.x());
    const double target_y =
        clearance_query ? query.y()
                        : directed_target(witness.point.y(), candidate.supported_direction.y());
    const auto corrected = Vector2::create(std::nextafter(witness.point.x(), target_x),
                                           std::nextafter(witness.point.y(), target_y));
    if (corrected == witness.point) {
      precision_lost("the selected rim-only boundary witness is not representable as supported");
    }
    witness.point = corrected;
    if (terrain_supports_point(terrain, witness.point)) {
      witness.distance = distance(query, witness.point);
      if (clearance_query && witness.distance > candidate.witness.distance) {
        precision_lost("directed witness rounding increased the selected boundary distance");
      }
      return witness;
    }
  }
  precision_lost("the selected boundary witness exhausted its directed representability budget");
}

} // namespace

CorridorCentrelineProjection corridor_project_to_centreline(const TerrainCorridor& corridor,
                                                            const Vector2& point) {
  const auto raw = detail::project_corridor_raw(corridor, point);
  return {Vector2::create(raw.x, raw.y), raw.distance};
}

double corridor_distance_to_centreline(const TerrainCorridor& corridor,
                                       const Vector2& point) noexcept {
  return detail::project_corridor_raw(corridor, point).distance;
}

bool terrain_supports_point(const TerrainDefinition& terrain, const Vector2& point) {
  if (!terrain.bounds().contains(point)) {
    return false;
  }
  bool supported = terrain.ground() == TerrainGround::kSolid;
  for (const auto& corridor : terrain.corridors()) {
    supported = supported || corridor_distance_to_centreline(corridor, point) <=
                                 corridor.half_width() + kPositionTolerance;
  }
  if (!supported) {
    return false;
  }
  for (const auto& hole : terrain.holes()) {
    if (distance(point, hole.center()) < std::max(0.0, hole.radius() - kPositionTolerance)) {
      return false;
    }
  }
  return true;
}

std::vector<TerrainSupportInterval> swept_support_intervals(const TerrainDefinition& terrain,
                                                            const Vector2& start,
                                                            const Vector2& displacement) {
  const Vector2 finish = start + displacement;
  std::vector<TerrainSupportInterval> intervals;
  if (terrain.ground() == TerrainGround::kSolid) {
    intervals.push_back({MotionTime::start(), MotionTime::end()});
  } else {
    for (const auto& corridor : terrain.corridors()) {
      const double radius = corridor.half_width() + kPositionTolerance;
      for (std::size_t index = 1; index < corridor.points().size(); ++index) {
        const auto& begin = corridor.points()[index - 1];
        const auto& end = corridor.points()[index];
        const auto roots = swept_capsule_boundary_roots(start, displacement, begin, end, radius);
        const auto interval =
            convex_interval(segment_distance(start, begin, end) <= radius,
                            segment_distance(finish, begin, end) <= radius, roots.times());
        if (interval) {
          intervals.push_back(*interval);
        }
      }
    }
    merge_intervals(intervals);
  }
  intersect_interval(intervals,
                     axis_interval(start.x(), displacement.x(), terrain.bounds().width()));
  intersect_interval(intervals,
                     axis_interval(start.y(), displacement.y(), terrain.bounds().height()));
  const bool stationary = displacement.x() == 0.0 && displacement.y() == 0.0;
  for (const auto& hole : terrain.holes()) {
    const double radius = std::max(0.0, hole.radius() - kPositionTolerance);
    if (radius == 0.0) {
      continue;
    }
    const double start_distance = distance(start, hole.center());
    const double end_distance = distance(finish, hole.center());
    if (stationary) {
      if (start_distance < radius) {
        intervals.clear();
      }
      continue;
    }
    const auto roots = swept_circle_boundary_roots(start, displacement, hole.center(), radius);
    const auto inside =
        convex_interval(start_distance <= radius, end_distance <= radius, roots.times());
    if (inside && inside->begin < inside->end) {
      subtract_open_hole(intervals, *inside, start_distance < radius, end_distance < radius);
    }
  }
  merge_intervals(intervals);
  return intervals;
}

std::optional<MotionTime> first_support_exit(const TerrainDefinition& terrain, const Vector2& start,
                                             const Vector2& displacement) {
  if (!terrain_supports_point(terrain, start)) {
    return MotionTime::start();
  }
  const auto intervals = swept_support_intervals(terrain, start, displacement);
  if (intervals.empty() || intervals.front().begin != MotionTime::start()) {
    precision_lost("point support and swept initial support disagree");
  }
  return intervals.front().end < MotionTime::end() ? std::optional{intervals.front().end}
                                                   : std::nullopt;
}

std::optional<TerrainPointWitness> nearest_supported_point(const TerrainDefinition& terrain,
                                                           const Vector2& point) {
  if (terrain_supports_point(terrain, point)) {
    return TerrainPointWitness{point, 0.0, BoundaryFeatureId{0}};
  }
  const auto result = nearest_boundary(terrain, point);
  if (!result) {
    return std::nullopt;
  }
  return supported_witness(terrain, point, *result, false);
}

std::optional<TerrainPointWitness> disc_clearance(const TerrainDefinition& terrain,
                                                  const Vector2& point) {
  if (!terrain_supports_point(terrain, point)) {
    return std::nullopt;
  }
  const auto result = nearest_boundary(terrain, point);
  if (!result) {
    precision_lost("supported bounded terrain has no represented boundary");
  }
  return supported_witness(terrain, point, *result, true);
}

bool terrain_supports_disc(const TerrainDefinition& terrain, const Vector2& center,
                           const double radius) {
  if (!std::isfinite(radius) || radius < 0.0 || radius > kMaximumPhysicalComponentMagnitude) {
    throw SimulationValidationError(
        SimulationValidationCode::kTerrainQueryRadiusInvalid, "terrain_queries.supports_disc",
        "disc radius must be finite, nonnegative, and physically bounded");
  }
  const auto clearance = disc_clearance(terrain, center);
  return clearance && clearance->distance >= radius;
}

namespace detail {

TerrainBoundary compile_terrain_boundary(const ArenaBounds& bounds, const TerrainGround ground,
                                         const std::span<const TerrainCorridor> corridors,
                                         const std::span<const TerrainHole> holes) {
  std::vector<Shape> shapes;
  std::vector<RawFeature> features;
  std::size_t incidence_count = 0;
  const auto zero = Vector2::create(0.0, 0.0);
  const auto bottom_right = Vector2::create(bounds.width(), 0.0);
  const auto top_right = Vector2::create(bounds.width(), bounds.height());
  const auto top_left = Vector2::create(0.0, bounds.height());
  shapes.push_back({ShapeKind::kEnvelope, zero, top_right, 0.0});
  append_line(features, 0, zero, bottom_right, Vector2::create(0.0, bounds.width()),
              incidence_count);
  append_line(features, 0, bottom_right, top_right, Vector2::create(-bounds.height(), 0.0),
              incidence_count);
  append_line(features, 0, top_right, top_left, Vector2::create(0.0, -bounds.width()),
              incidence_count);
  append_line(features, 0, top_left, zero, Vector2::create(bounds.height(), 0.0), incidence_count);
  for (const auto& corridor : corridors) {
    const double radius = corridor.half_width() + kPositionTolerance;
    for (std::size_t index = 1; index < corridor.points().size(); ++index) {
      const auto& begin = corridor.points()[index - 1];
      const auto& end = corridor.points()[index];
      const auto direction = end - begin;
      const double length =
          std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
      if (length == 0.0) {
        precision_lost("a corridor segment collapsed");
      }
      const auto normal = Vector2::create(-direction.y(), direction.x());
      const auto offset =
          Vector2::create(radius * (normal.x() / length), radius * (normal.y() / length));
      const std::size_t shape = shapes.size();
      shapes.push_back({ShapeKind::kCapsule, begin, end, radius});
      append_line(features, shape, end + offset, begin + offset, -normal, incidence_count);
      append_line(features, shape, begin - offset, end - offset, normal, incidence_count);
      append_arc(features, shape, begin, radius, normal, -normal, incidence_count);
      append_arc(features, shape, end, radius, -normal, normal, incidence_count);
    }
  }
  const std::array cardinals{Vector2::create(1.0, 0.0), Vector2::create(0.0, 1.0),
                             Vector2::create(-1.0, 0.0), Vector2::create(0.0, -1.0)};
  for (const auto& hole : holes) {
    const double radius = std::max(0.0, hole.radius() - kPositionTolerance);
    if (radius == 0.0) {
      continue;
    }
    const auto shape = shapes.size();
    shapes.push_back({ShapeKind::kHole, hole.center(), hole.center(), radius});
    for (std::size_t index = 0; index < cardinals.size(); ++index) {
      append_arc(features, shape, hole.center(), radius, cardinals[index],
                 cardinals[(index + 1) % cardinals.size()], incidence_count);
    }
  }
  for (std::size_t first = 0; first < features.size(); ++first) {
    for (std::size_t second = first + 1; second < features.size(); ++second) {
      // A shape's own exact feature endpoints are already present; recomputing its analytic
      // tangencies would manufacture nearby cut identities from rounded circle coordinates.
      if (features[first].shape != features[second].shape) {
        intersect_features(features[first], features[second], shapes, incidence_count);
      }
    }
  }
  std::size_t retained_vertex_slots = 0;
  const auto vertex_sectors =
      compile_vertex_sectors(features, shapes, ground, incidence_count, retained_vertex_slots);
  TerrainBoundary boundary;
  std::vector<Vector2> supported_incident_vertices;
  for (auto& feature : features) {
    if (std::holds_alternative<RawLine>(feature.geometry)) {
      std::sort(feature.cuts.begin(), feature.cuts.end(),
                [](const auto& first, const auto& second) { return first.time < second.time; });
      for (std::size_t index = 1; index < feature.cuts.size(); ++index) {
        if (feature.cuts[index - 1].time == feature.cuts[index].time &&
            feature.cuts[index - 1].point != feature.cuts[index].point) {
          precision_lost("distinct arrangement vertices collapsed to one line parameter");
        }
      }
      feature.cuts.erase(std::unique(feature.cuts.begin(), feature.cuts.end(),
                                     [](const auto& first, const auto& second) {
                                       return first.time == second.time;
                                     }),
                         feature.cuts.end());
    } else {
      const auto& arc = std::get<RawArc>(feature.geometry);
      std::sort(feature.cuts.begin(), feature.cuts.end(),
                [&arc](const auto& first, const auto& second) {
                  const bool first_wrapped = angle_less(first.direction, arc.begin_direction);
                  const bool second_wrapped = angle_less(second.direction, arc.begin_direction);
                  if (first_wrapped != second_wrapped) {
                    return !first_wrapped;
                  }
                  return angle_less(first.direction, second.direction);
                });
      for (std::size_t index = 1; index < feature.cuts.size(); ++index) {
        if (same_direction(feature.cuts[index - 1].direction, feature.cuts[index].direction) &&
            feature.cuts[index - 1].point != feature.cuts[index].point) {
          precision_lost("distinct arrangement vertices collapsed to one arc direction");
        }
      }
      feature.cuts.erase(std::unique(feature.cuts.begin(), feature.cuts.end(),
                                     [](const auto& first, const auto& second) {
                                       return same_direction(first.direction, second.direction);
                                     }),
                         feature.cuts.end());
    }
    for (std::size_t index = 1; index < feature.cuts.size(); ++index) {
      const auto& begin = feature.cuts[index - 1];
      const auto& end = feature.cuts[index];
      Vector2 representative = zero;
      std::variant<TerrainLineSpan, TerrainArcSpan> span = TerrainLineSpan{begin.point, end.point};
      if (std::holds_alternative<RawLine>(feature.geometry)) {
        representative = begin.point + (end.point - begin.point) * 0.5;
      } else {
        const auto& arc = std::get<RawArc>(feature.geometry);
        const auto first_unit = circle_point(zero, 1.0, begin.direction);
        const auto second_unit = circle_point(zero, 1.0, end.direction);
        representative = circle_point(arc.center, arc.radius, first_unit + second_unit);
        span = TerrainArcSpan{arc.center, arc.radius,      begin.point,
                              end.point,  begin.direction, end.direction};
      }
      if (representative == begin.point || representative == end.point) {
        precision_lost("an arrangement cell has no representable interior witness");
      }
      const auto support = local_support(representative, feature_normal(feature, representative),
                                         feature, features, shapes, ground);
      if (support.on) {
        supported_incident_vertices.push_back(begin.point);
        supported_incident_vertices.push_back(end.point);
        require_arrangement_bound(incidence_count + retained_vertex_slots +
                                  supported_incident_vertices.size());
      }
      if (support.on && (!support.left || !support.right)) {
        // Coincident curves have one cache representative, in authored feature order.
        bool earlier_coincident = false;
        for (const auto& other : features) {
          if (other.id < feature.id && coincident_at(feature, other, representative)) {
            earlier_coincident = true;
            break;
          }
        }
        if (!earlier_coincident) {
          boundary.spans.push_back(
              {feature.id, std::move(span), support.left ? 1 : (support.right ? -1 : 0),
               sector_at(vertex_sectors, begin.point), sector_at(vertex_sectors, end.point)});
        }
      }
    }
  }
  for (const auto& feature : features) {
    for (const auto& cut : feature.cuts) {
      if (std::find(supported_incident_vertices.begin(), supported_incident_vertices.end(),
                    cut.point) == supported_incident_vertices.end() &&
          point_supported_by_arrangement(cut.point, features, shapes, ground) &&
          std::none_of(boundary.isolated_points.begin(), boundary.isolated_points.end(),
                       [&cut](const auto& existing) { return existing.point == cut.point; })) {
        boundary.isolated_points.push_back({feature.id, cut.point});
      }
    }
  }
  if (boundary.spans.size() + boundary.isolated_points.size() >
      kMaximumTerrainBoundaryElementCount) {
    throw SimulationValidationError(SimulationValidationCode::kTerrainBoundaryLimitExceeded,
                                    "terrain_queries.compile_boundary",
                                    "exposed terrain boundary exceeds its bounded cache");
  }
  return boundary;
}

} // namespace detail
} // namespace blob_royale::simulation
