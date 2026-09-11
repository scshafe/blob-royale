#include "continuous_motion.hpp"

#include "simulation_tolerance.hpp"
#include "swept_geometry.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace blob_royale::simulation {
namespace detail {

[[noreturn]] void fail_motion(const SimulationValidationCode code, const char* message) {
  throw SimulationValidationError(code, "continuous_motion", message);
}

void require_motion_budget(const std::size_t count, const std::size_t maximum,
                           const char* operation) {
  if (count > maximum) {
    fail_motion(SimulationValidationCode::kContinuousMotionBudgetExceeded, operation);
  }
}

// The only construction privilege is held here, after a swept/overlap geometric certificate.
// This never re-gates distance at a rounded TOI and never calls the legacy discrete detector.
struct MotionContactAccess final {
  [[nodiscard]] static PlayerPairContact create(const Vector2& normal, const double distance,
                                                const double relative_speed) {
    if (!std::isfinite(distance) || distance < 0.0 || !std::isfinite(relative_speed) ||
        !approximately_equal(normal.x() * normal.x() + normal.y() * normal.y(), 1.0,
                             kScalarTolerance)) {
      fail_motion(SimulationValidationCode::kContinuousMotionPrecisionLost,
                  "a swept contact certificate is not finite and normalized");
    }
    return PlayerPairContact{true, normal, distance, relative_speed};
  }
};

} // namespace detail

void MotionQueryBudget::roots(const std::size_t count) {
  if (count > limits_->root_queries - counts_->root_queries) {
    detail::fail_motion(SimulationValidationCode::kContinuousMotionBudgetExceeded,
                        "motion root-query budget exhausted");
  }
  counts_->root_queries += count;
}

void MotionQueryBudget::trigger() {
  detail::require_motion_budget(++counts_->trigger_queries, limits_->trigger_queries,
                                "motion trigger-query budget exhausted");
}

namespace detail {
namespace {

[[nodiscard]] Vector2 zero() { return Vector2::create(0.0, 0.0); }

[[nodiscard]] Vector2 geometric_velocity(const PhysicsBody& body) {
  return body.is_static() ? zero() : body.velocity();
}

void validate_limits(const MotionLimits& limits) {
  const std::array requested{
      limits.bodies,       limits.candidate_pairs, limits.pair_examinations,
      limits.root_queries, limits.events,          limits.trigger_queries,
      limits.paths,        limits.effects,         limits.trigger_declarations};
  const std::array ceilings{kMaximumMotionBodyCount,
                            kMaximumMotionCandidatePairCount,
                            kMaximumMotionPairExaminationCount,
                            kMaximumMotionRootQueryCount,
                            kMaximumMotionEventCount,
                            kMaximumMotionTriggerQueryCount,
                            kMaximumMotionPathSegmentCount,
                            kMaximumMotionEffectCount,
                            kMaximumMotionTriggerDeclarationCount};
  for (std::size_t index = 0; index < requested.size(); ++index) {
    if (requested[index] > ceilings[index]) {
      fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                  "requested motion limits exceed the declared prototype envelope");
    }
  }
  if (limits.trigger_cursor > kMaximumMotionTriggerCursorValue) {
    fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                "requested trigger cursor ceiling exceeds the declared prototype envelope");
  }
}

[[nodiscard]] double relative_speed(const PhysicsBody& first, const PhysicsBody& second,
                                    const Vector2& normal) {
  const auto first_velocity = geometric_velocity(first);
  const auto second_velocity = geometric_velocity(second);
  return ((second_velocity.x() - first_velocity.x()) * normal.x()) +
         ((second_velocity.y() - first_velocity.y()) * normal.y());
}

[[nodiscard]] bool closing(const double speed) {
  return !greater_than_or_approximately_equal(speed, 0.0, kVelocityTolerance);
}

struct ContactGeometry final {
  Vector2 normal;
  double distance;
};

[[nodiscard]] ContactGeometry contact_geometry(const PhysicsBody& first,
                                               const PhysicsBody& second) {
  // Relative intermediates may be twice the per-component storage bound. Only the final
  // normalized direction/displacement is a Vector2; no artificial bounded relative vector.
  const double dx = second.position().x() - first.position().x();
  const double dy = second.position().y() - first.position().y();
  const double distance = std::sqrt(dx * dx + dy * dy);
  Vector2 normal = Vector2::create(1.0, 0.0);
  if (dx != 0.0 || dy != 0.0) {
    // The unwired continuous solver distinguishes every representable pair of centers.
    // A positional tolerance must not substitute velocity direction for a tiny geometric normal.
    normal = Vector2::create(dx / distance, dy / distance);
  } else {
    const auto first_velocity = geometric_velocity(first);
    const auto second_velocity = geometric_velocity(second);
    const double vx = first_velocity.x() - second_velocity.x();
    const double vy = first_velocity.y() - second_velocity.y();
    const double magnitude = std::sqrt(vx * vx + vy * vy);
    if (!approximately_equal(magnitude, 0.0, kVelocityTolerance)) {
      normal = Vector2::create(vx / magnitude, vy / magnitude);
    }
  }
  return {normal, distance};
}

struct EpochBody final {
  ContactRule::Subject subject;
  MotionTime anchor = MotionTime::start();
  std::uint64_t revision{};
  MotionDisposition disposition = MotionDisposition::kContinue;
};

struct PairCache final {
  std::size_t first;
  std::size_t second;
  std::uint64_t first_revision = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t second_revision = std::numeric_limits<std::uint64_t>::max();
  std::optional<std::pair<std::uint64_t, std::uint64_t>> suppressed{};
  std::optional<MotionGeometryEvent> event{};
  bool candidate{};
};

struct SweptAabb final {
  std::size_t body;
  double minimum_x;
  double maximum_x;
  double minimum_y;
  double maximum_y;
};

} // namespace

struct ContinuousMotionSolver::State final {
  MotionLimits limits;
  MotionWorkCounts work;
  double seconds;
  double radius;
  ArenaBounds bounds;
  MotionTime now = MotionTime::start();
  std::vector<EpochBody> bodies;
  std::vector<PairCache> pairs;
  std::vector<std::array<std::optional<MotionGeometryEvent>, 2>> walls;
  std::vector<MotionPathSegment> paths;
  std::vector<MotionEventKey> events;
  bool dirty = true;

  State(const TickContext& context, const MotionLimits authored_limits)
      : limits(authored_limits), seconds(context.fixed_delta().seconds()),
        radius(context.simulation_config().player_radius()), bounds(context.map().bounds()) {}

  [[nodiscard]] ContactRule::Subject at(const std::size_t index, const MotionTime time) const {
    const auto& body = bodies[index];
    if (time < body.anchor) {
      fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                  "body evaluation precedes its velocity epoch");
    }
    if (body.subject.body.is_static() || body.disposition == MotionDisposition::kTerminate ||
        time == body.anchor) {
      return body.subject;
    }
    // Never form a remaining-step chord at a global event. This body owns this subtraction and
    // anchor until its own motion changes, preserving untouched endpoint/TOI bits.
    const double elapsed = seconds * (time.value() - body.anchor.value());
    return {body.subject.entity,
            body.subject.body.with_position(body.subject.body.position() +
                                            body.subject.body.velocity() * elapsed)};
  }

  [[nodiscard]] std::size_t pair_index(std::size_t first, std::size_t second) const {
    if (second < first) {
      std::swap(first, second);
    }
    return first * (2 * bodies.size() - first - 1) / 2 + second - first - 1;
  }

  void add_path(const std::size_t index, const MotionTime end) {
    const auto& body = bodies[index];
    if (body.subject.body.is_static() || end == body.anchor) {
      return;
    }
    require_motion_budget(paths.size() + 1, limits.paths, "motion path-segment budget exhausted");
    const auto endpoint = at(index, end);
    paths.push_back({body.subject.entity, body.anchor, end, body.subject.body.position(),
                     endpoint.body.position()});
  }

  [[nodiscard]] std::optional<MotionGeometryEvent> pair_event(const PairCache& pair) {
    const auto& first_epoch = bodies[pair.first];
    const auto& second_epoch = bodies[pair.second];
    const auto begin = std::max(first_epoch.anchor, second_epoch.anchor);
    const auto first = at(pair.first, begin);
    const auto second = at(pair.second, begin);
    const double contact_distance = pair_contact_distance(first.body, second.body, radius);
    auto make = [&](const MotionTime time) -> std::optional<MotionGeometryEvent> {
      const auto first_at = at(pair.first, time);
      const auto second_at = at(pair.second, time);
      const auto geometry = contact_geometry(first_at.body, second_at.body);
      if (!closing(relative_speed(first_at.body, second_at.body, geometry.normal))) {
        return std::nullopt;
      }
      return MotionGeometryEvent{
          MotionEventKey::contact(time, CandidatePair::create(first.entity, second.entity)),
          pair.first,
          pair.second,
          geometry.normal,
          geometry.distance,
          first_epoch.revision,
          second_epoch.revision};
    };
    const auto first_velocity = geometric_velocity(first.body);
    const auto second_velocity = geometric_velocity(second.body);
    const bool terminal_epoch = begin == MotionTime::end();
    // A response at t=1 may causally close an already touching third body. With no remaining
    // travel, use one canonical quantum only as a reference line for the SAME exact initial
    // relation/topology query. Its nonzero roots never become motion or future events.
    const double remaining = terminal_epoch ? seconds : seconds * (1.0 - begin.value());
    const auto displacement =
        Vector2::create((first_velocity.x() - second_velocity.x()) * remaining,
                        (first_velocity.y() - second_velocity.y()) * remaining);
    MotionQueryBudget{work, limits}.roots(1);
    const auto query = swept_circle_boundary_query(first.body.position(), displacement,
                                                   second.body.position(), contact_distance);
    if (query.topology == CircleLineTopology::kStationary ||
        query.topology == CircleLineTopology::kTangent) {
      return std::nullopt; // Exact topology precedes a rounded normal's closing-speed residual.
    }
    if (query.initial_relation != CircleInitialRelation::kOutside) {
      if (!query.initial_centers_coincident &&
          query.initial_radial_motion != CircleRadialMotion::kApproaching) {
        return std::nullopt;
      }
      return make(begin); // Exact initial overlap/boundary, with the existing closing predicate.
    }
    if (terminal_epoch || query.topology != CircleLineTopology::kSecant || query.roots.empty()) {
      return std::nullopt;
    }
    // An outside secant has one entry followed by one exit. Filtering a stale entry cannot
    // promote the later exit into an impact, irrespective of its rounded normal-speed sign.
    const auto time = map_motion_time(query.roots.times().front(), begin, MotionTime::end());
    return time >= now ? make(time) : std::nullopt;
  }

  void rebuild() {
    if (!dirty) {
      return;
    }
    ++work.broad_phase_rebuilds;
    for (auto& pair : pairs) {
      pair.candidate = false;
    }
    std::vector<SweptAabb> boxes;
    for (std::size_t index = 0; index < bodies.size(); ++index) {
      const auto& body = bodies[index];
      if (body.disposition == MotionDisposition::kTerminate) {
        continue;
      }
      const auto start = body.subject.body.position();
      // An unhandled whole-epoch endpoint is broad-phase scalar geometry, not a published
      // PhysicsBody position. An earlier contact/termination may keep the actual path inside
      // the physical coordinate bound even when this hypothetical endpoint would exceed it.
      const auto velocity = geometric_velocity(body.subject.body);
      const double remaining = seconds * (1.0 - body.anchor.value());
      const double finish_x = start.x() + velocity.x() * remaining;
      const double finish_y = start.y() + velocity.y() * remaining;
      const double extent = effective_radius(body.subject.body, radius);
      boxes.push_back(
          {index, std::min(start.x(), finish_x) - extent, std::max(start.x(), finish_x) + extent,
           std::min(start.y(), finish_y) - extent, std::max(start.y(), finish_y) + extent});
    }
    std::sort(boxes.begin(), boxes.end(), [&](const auto& first, const auto& second) {
      return first.minimum_x < second.minimum_x ||
             (first.minimum_x == second.minimum_x &&
              bodies[first.body].subject.entity < bodies[second.body].subject.entity);
    });
    std::size_t candidate_count = 0;
    for (std::size_t first = 0; first < boxes.size(); ++first) {
      for (std::size_t second = first + 1; second < boxes.size(); ++second) {
        require_motion_budget(++work.pair_examinations, limits.pair_examinations,
                              "motion broad-phase pair-examination budget exhausted");
        if (boxes[second].minimum_x > boxes[first].maximum_x) {
          break;
        }
        const auto& first_body = bodies[boxes[first].body].subject.body;
        const auto& second_body = bodies[boxes[second].body].subject.body;
        if ((first_body.is_static() && second_body.is_static()) ||
            !collision_masks_admit(first_body, second_body) ||
            boxes[second].minimum_y > boxes[first].maximum_y ||
            boxes[first].minimum_y > boxes[second].maximum_y) {
          continue;
        }
        require_motion_budget(++candidate_count, limits.candidate_pairs,
                              "motion candidate-pair budget exhausted");
        pairs[pair_index(boxes[first].body, boxes[second].body)].candidate = true;
      }
    }
    work.maximum_candidate_pairs = std::max(work.maximum_candidate_pairs, candidate_count);
    for (auto& pair : pairs) {
      const auto& first = bodies[pair.first];
      const auto& second = bodies[pair.second];
      const bool live = first.disposition == MotionDisposition::kContinue &&
                        second.disposition == MotionDisposition::kContinue;
      const auto revisions = std::pair{first.revision, second.revision};
      if (!live || pair.suppressed == revisions) {
        pair.event.reset();
      } else if (pair.event && pair.event->key.time() == now) {
        // Geometry is unchanged at this event time. Preserve a certified tied contact, and
        // recheck only its closing velocity when selected, never rounded contact membership.
      } else if (!pair.candidate) {
        pair.event.reset();
      } else if (pair.first_revision != first.revision || pair.second_revision != second.revision) {
        pair.event = pair_event(pair);
      }
      pair.first_revision = first.revision;
      pair.second_revision = second.revision;
    }
    for (std::size_t index = 0; index < bodies.size(); ++index) {
      const auto& epoch = bodies[index];
      const auto& body = epoch.subject.body;
      for (std::size_t axis = 0; axis < 2; ++axis) {
        auto& event = walls[index][axis];
        if (epoch.disposition == MotionDisposition::kTerminate || body.is_static() ||
            body.crosses_bounds()) {
          event.reset();
          continue;
        }
        if (event && event->key.time() == now) {
          continue;
        }
        const double speed = axis == 0 ? body.velocity().x() : body.velocity().y();
        event.reset();
        if (speed == 0.0) {
          continue;
        }
        const double extent = effective_radius(body, radius);
        const double boundary =
            speed < 0.0 ? extent : (axis == 0 ? bounds.width() : bounds.height()) - extent;
        const double start = axis == 0 ? body.position().x() : body.position().y();
        const auto make_wall = [&](const MotionTime time) {
          const auto normal = axis == 0 ? Vector2::create(speed < 0.0 ? 1.0 : -1.0, 0.0)
                                        : Vector2::create(0.0, speed < 0.0 ? 1.0 : -1.0);
          return MotionGeometryEvent{
              MotionEventKey::boundary(
                  time, axis == 0 ? MotionEventPriority::kWallX : MotionEventPriority::kWallY,
                  epoch.subject.entity, speed < 0.0 ? 0 : 1),
              index, std::nullopt, normal, 0.0};
        };
        // Wall-overlapping centers are admitted within the map envelope. Only outward
        // motion reflects immediately; inward motion leaves the penetration untouched.
        if ((speed < 0.0 && start <= boundary) || (speed > 0.0 && start >= boundary)) {
          event = make_wall(map_motion_time(MotionTime::start(), epoch.anchor, MotionTime::end()));
          continue;
        }
        MotionQueryBudget{work, limits}.roots(1);
        const auto roots = swept_line_boundary_roots(
            start, speed * (seconds * (1.0 - epoch.anchor.value())), boundary);
        for (const auto local : roots.times()) {
          const auto time = map_motion_time(local, epoch.anchor, MotionTime::end());
          if (time >= now) {
            event = make_wall(time);
            break;
          }
        }
      }
    }
    dirty = false;
  }
};

ContinuousMotionSolver::ContinuousMotionSolver(const std::span<const ContactRule::Subject> input,
                                               const TickContext& context,
                                               const MotionLimits limits)
    : state_(std::make_unique<State>(context, limits)) {
  validate_limits(limits);
  require_motion_budget(input.size(), limits.bodies, "motion body budget exhausted");
  for (const auto& subject : input) {
    state_->bodies.push_back({subject, MotionTime::start(), 0, MotionDisposition::kContinue});
  }
  std::sort(state_->bodies.begin(), state_->bodies.end(),
            [](const auto& first, const auto& second) {
              return first.subject.entity < second.subject.entity;
            });
  for (std::size_t index = 0; index < state_->bodies.size(); ++index) {
    const auto& subject = state_->bodies[index].subject;
    if (index != 0 && state_->bodies[index - 1].subject.entity == subject.entity) {
      fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                  "duplicate motion body identity");
    }
    const double radius = effective_radius(subject.body, state_->radius);
    if (subject.body.is_static()) {
      if (!state_->bounds.contains(subject.body.position())) {
        fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                    "static motion geometry lies outside the envelope");
      }
    } else if (!subject.body.crosses_bounds()) {
      if (!state_->bounds.contains(subject.body.position()) ||
          state_->bounds.width() < 2.0 * radius || state_->bounds.height() < 2.0 * radius ||
          (state_->bounds.width() == 2.0 * radius && subject.body.velocity().x() != 0.0) ||
          (state_->bounds.height() == 2.0 * radius && subject.body.velocity().y() != 0.0)) {
        fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                    "folding motion body does not fit its envelope");
      }
    }
    for (std::size_t second = index + 1; second < state_->bodies.size(); ++second) {
      state_->pairs.push_back({index, second});
    }
  }
  state_->walls.resize(state_->bodies.size());
  state_->rebuild();
}

ContinuousMotionSolver::~ContinuousMotionSolver() = default;

std::optional<MotionGeometryEvent> ContinuousMotionSolver::next_geometry_event() const {
  std::optional<MotionGeometryEvent> next;
  auto consider = [&](const std::optional<MotionGeometryEvent>& event) {
    if (event && (!next || event->key < next->key)) {
      next = event;
    }
  };
  for (const auto& pair : state_->pairs) {
    consider(pair.event);
  }
  for (const auto& axes : state_->walls) {
    for (const auto& event : axes) {
      consider(event);
    }
  }
  return next;
}

ContactRule::Subject ContinuousMotionSolver::subject(const std::size_t index,
                                                     const MotionTime time) const {
  return state_->at(index, time);
}

std::size_t ContinuousMotionSolver::index_of(const EntityId entity) const {
  const auto found =
      std::lower_bound(state_->bodies.begin(), state_->bodies.end(), entity,
                       [](const auto& body, const auto& key) { return body.subject.entity < key; });
  if (found == state_->bodies.end() || found->subject.entity != entity) {
    fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                "motion trigger names an absent body");
  }
  return static_cast<std::size_t>(found - state_->bodies.begin());
}

AnchoredMotion ContinuousMotionSolver::anchored_motion(const std::size_t index) const {
  const auto& body = state_->bodies[index];
  return {body.subject, body.anchor,
          geometric_velocity(body.subject.body) * (state_->seconds * (1.0 - body.anchor.value()))};
}

std::uint64_t ContinuousMotionSolver::revision(const std::size_t index) const {
  return state_->bodies[index].revision;
}
bool ContinuousMotionSolver::terminated(const std::size_t index) const {
  return state_->bodies[index].disposition == MotionDisposition::kTerminate;
}
MotionTime ContinuousMotionSolver::now() const { return state_->now; }
MotionQueryBudget ContinuousMotionSolver::query_budget() {
  return MotionQueryBudget{state_->work, state_->limits};
}

std::optional<PlayerPairContact>
ContinuousMotionSolver::contact(const MotionGeometryEvent& event) const {
  const auto first = state_->at(event.first, event.key.time());
  const auto second = state_->at(*event.second, event.key.time());
  if (event.first_motion_revision != state_->bodies[event.first].revision ||
      event.second_motion_revision != state_->bodies[*event.second].revision) {
    const auto first_velocity = geometric_velocity(first.body);
    const auto second_velocity = geometric_velocity(second.body);
    // This positive-duration reference direction is valid even for a retained t=1 contact.
    // Only exact radial admission is read: the selected hit's geometry/time remain certified,
    // even if rounded current positions classify outside or this new line misses/is tangent
    // elsewhere. No reference root, membership, topology, or new normal replaces that hit.
    const auto reference =
        Vector2::create((first_velocity.x() - second_velocity.x()) * state_->seconds,
                        (first_velocity.y() - second_velocity.y()) * state_->seconds);
    MotionQueryBudget{state_->work, state_->limits}.roots(1);
    const auto query =
        swept_circle_boundary_query(first.body.position(), reference, second.body.position(),
                                    pair_contact_distance(first.body, second.body, state_->radius));
    if (!query.initial_centers_coincident &&
        query.initial_radial_motion != CircleRadialMotion::kApproaching) {
      return std::nullopt;
    }
  }
  const double speed = relative_speed(first.body, second.body, event.normal);
  return closing(speed) ? std::optional{MotionContactAccess::create(event.normal,
                                                                    event.center_distance, speed)}
                        : std::nullopt;
}

void ContinuousMotionSolver::begin_event(const MotionEventKey& key) {
  if (key.time() < state_->now) {
    fail_motion(SimulationValidationCode::kContinuousMotionNoProgress,
                "motion event time moved backward");
  }
  require_motion_budget(++state_->work.events, state_->limits.events,
                        "motion event budget exhausted");
  state_->now = key.time();
  state_->events.push_back(key);
}

void ContinuousMotionSolver::replace(const std::size_t index, const MotionBodyResult& replacement) {
  auto& epoch = state_->bodies[index];
  const auto current = state_->at(index, state_->now);
  if (replacement.disposition != MotionDisposition::kContinue &&
      replacement.disposition != MotionDisposition::kTerminate) {
    fail_motion(SimulationValidationCode::kContinuousMotionInvalidResponse,
                "undeclared motion disposition");
  }
  if (current.body.with_velocity(replacement.body.velocity())
          .with_acceleration(replacement.body.acceleration()) != replacement.body) {
    fail_motion(SimulationValidationCode::kContinuousMotionInvalidResponse,
                "motion response changed position, geometry, mass, or collision capabilities");
  }
  const bool changes_velocity =
      !current.body.is_static() && current.body.velocity() != replacement.body.velocity();
  const bool terminates = replacement.disposition == MotionDisposition::kTerminate;
  if (changes_velocity || terminates) {
    state_->add_path(index, state_->now);
    epoch.subject.body = replacement.body;
    epoch.anchor = state_->now;
    ++epoch.revision;
    epoch.disposition = replacement.disposition;
    state_->dirty = true;
  } else {
    epoch.subject.body = epoch.subject.body.with_velocity(replacement.body.velocity())
                             .with_acceleration(replacement.body.acceleration());
  }
}

void ContinuousMotionSolver::reflect_wall(const MotionGeometryEvent& event) {
  const std::size_t axis = event.key.priority() == MotionEventPriority::kWallX ? 0 : 1;
  state_->walls[event.first][axis].reset();
  // Consuming a retained tied certificate invalidates this axis even when an earlier pair
  // response now points inward. Its opposite-wall future still needs to be rebuilt.
  state_->dirty = true;
  const auto current = state_->at(event.first, state_->now);
  if (current.body.velocity().dot(event.normal) >= 0.0) {
    return;
  }
  const auto velocity =
      axis == 0 ? Vector2::create(-current.body.velocity().x(), current.body.velocity().y())
                : Vector2::create(current.body.velocity().x(), -current.body.velocity().y());
  replace(event.first,
          MotionBodyResult{current.body.with_velocity(velocity), MotionDisposition::kContinue});
}

void ContinuousMotionSolver::finish_event(const std::optional<MotionGeometryEvent>& pair) {
  if (pair && pair->second) {
    auto& cache = state_->pairs[state_->pair_index(pair->first, *pair->second)];
    cache.suppressed =
        std::pair{state_->bodies[pair->first].revision, state_->bodies[*pair->second].revision};
    cache.event.reset();
  }
  state_->rebuild();
}

MotionGeometryResult ContinuousMotionSolver::finish() {
  MotionGeometryResult result;
  for (std::size_t index = 0; index < state_->bodies.size(); ++index) {
    const auto& body = state_->bodies[index];
    if (body.disposition == MotionDisposition::kContinue) {
      state_->add_path(index, MotionTime::end());
    }
    result.bodies.push_back(
        {body.subject.entity, {state_->at(index, MotionTime::end()).body, body.disposition}});
  }
  result.paths = std::move(state_->paths);
  result.events = std::move(state_->events);
  result.work = state_->work;
  return result;
}

} // namespace detail
} // namespace blob_royale::simulation
