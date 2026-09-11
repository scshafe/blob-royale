#ifndef BLOB_ROYALE_SIMULATION_CONTINUOUS_MOTION_HPP
#define BLOB_ROYALE_SIMULATION_CONTINUOUS_MOTION_HPP

#include "contact_rule.hpp"
#include "motion_contact_observation.hpp"
#include "motion_event_order.hpp"
#include "motion_response.hpp"
#include "simulation_limits.hpp"
#include "tick_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace blob_royale::simulation {

// Provisional prototype ceilings, not native-certified operating limits. Tests may lower these
// values but cannot exceed the named simulation_limits.hpp ceilings. Every exhaustion is fatal.
struct MotionLimits final {
  std::size_t bodies = kMaximumMotionBodyCount;
  std::size_t candidate_pairs = kMaximumMotionCandidatePairCount;
  std::size_t pair_examinations = kMaximumMotionPairExaminationCount;
  std::size_t root_queries = kMaximumMotionRootQueryCount;
  std::size_t events = kMaximumMotionEventCount;
  std::size_t trigger_queries = kMaximumMotionTriggerQueryCount;
  std::size_t paths = kMaximumMotionPathSegmentCount;
  std::size_t effects = kMaximumMotionEffectCount;
  std::size_t trigger_declarations = kMaximumMotionTriggerDeclarationCount;
  std::uint64_t trigger_cursor = kMaximumMotionTriggerCursorValue;
};

struct MotionWorkCounts final {
  std::size_t pair_examinations{};
  std::size_t root_queries{};
  std::size_t events{};
  std::size_t trigger_queries{};
  std::size_t broad_phase_rebuilds{};
  std::size_t maximum_candidate_pairs{};
  friend bool operator==(const MotionWorkCounts&, const MotionWorkCounts&) = default;
};

struct MotionPathSegment final {
  EntityId entity;
  MotionTime begin;
  MotionTime end;
  Vector2 start;
  Vector2 finish;
  friend bool operator==(const MotionPathSegment&, const MotionPathSegment&) = default;
};

struct MotionResolvedBody final {
  EntityId entity;
  MotionBodyResult result;
  friend bool operator==(const MotionResolvedBody&, const MotionResolvedBody&) = default;
};

// One body's unmodified velocity epoch, anchored only by its own velocity change/termination.
// displacement spans [begin,1]; unrelated events never reparameterize this motion.
struct AnchoredMotion final {
  ContactRule::Subject subject;
  MotionTime begin;
  Vector2 displacement;
};

struct MotionTriggerWindow final {
  AnchoredMotion motion;
  MotionTime eligible_from;
};

struct MotionTriggerProposal final {
  MotionTime time;
  MotionEventPriority priority;
};

struct MotionTriggerEvent final {
  MotionEventKey key;
  std::uint64_t cursor;
};

// A narrow tick-local resource meter, not mutable world access. Shared geometry trigger helpers
// charge their bounded root work here before evaluating it.
class MotionQueryBudget final {
public:
  MotionQueryBudget(MotionWorkCounts& counts, const MotionLimits& limits) noexcept
      : counts_(&counts), limits_(&limits) {}
  void roots(std::size_t count);
  void trigger();

private:
  MotionWorkCounts* counts_;
  const MotionLimits* limits_;
};

template <class Effect> struct MotionTriggerResponse final {
  MotionBodyResult body;
  std::uint64_t cursor;
  std::vector<Effect> effects;
};

template <class Effect, class Facts> struct MotionTrigger final {
  using Query = std::optional<MotionTriggerProposal> (*)(const GameWorld&,
                                                         const ContactRule::Subject&,
                                                         const MotionTriggerWindow&,
                                                         const TickContext&, const Facts&,
                                                         std::uint64_t, MotionQueryBudget&);
  using Response = MotionTriggerResponse<Effect> (*)(const GameWorld&, const ContactRule::Subject&,
                                                     const MotionTriggerEvent&, const TickContext&,
                                                     const Facts&);

  EntityId entity;
  // This declaration owns [feature_base,feature_base+cursor_limit]. Ranges for one entity must
  // not overlap, so each cursor advancement names a distinct stable authored feature.
  std::uint64_t feature_base;
  std::uint64_t initial_cursor;
  std::uint64_t cursor_limit;
  Query query;
  Response response;
};

template <class Effect, class Facts>
using PairMotionResponseFunction = PairMotionResponse<Effect> (*)(const GameWorld&,
                                                                  const ContactRule::Subject&,
                                                                  const ContactRule::Subject&,
                                                                  const PairContactObservation&,
                                                                  const TickContext&, const Facts&);

template <class Effect> struct OrderedMotionEffect final {
  MotionEventKey event;
  Effect effect;
};

struct MotionGeometryResult final {
  std::vector<MotionResolvedBody> bodies;
  std::vector<MotionPathSegment> paths;
  // Selected-certificate diagnostic trace, including cached contact/wall certificates later
  // declined after a velocity response. These are not emitted gameplay ContactEvents;
  // gameplay consequences are exclusively the typed ContinuousMotionResult::effects values.
  std::vector<MotionEventKey> events;
  MotionWorkCounts work;
};

template <class Effect> struct ContinuousMotionResult final {
  MotionGeometryResult motion;
  std::vector<OrderedMotionEffect<Effect>> effects;
  std::vector<std::uint64_t> trigger_cursors;
};

namespace detail {

[[noreturn]] void fail_motion(SimulationValidationCode code, const char* message);
void require_motion_budget(std::size_t count, std::size_t maximum, const char* operation);

struct MotionGeometryEvent final {
  MotionEventKey key;
  std::size_t first;
  std::optional<std::size_t> second;
  Vector2 normal;
  double center_distance;
  // Pair certificate provenance, independent of current candidate-cache revision bookkeeping.
  // Walls leave these zero; a retained pair checks them before current-velocity admission.
  std::uint64_t first_motion_revision{};
  std::uint64_t second_motion_revision{};
  // Exact topology/radial admission at the generating revisions. A revised retained touch
  // instead uses the canonical current-radial veto without revoking certified geometry.
  bool impact_geometry_admitted{};
};

// Concrete canonical geometry/caching mechanism. Only typed facts/effects orchestration is a
// template; no gameplay alternative, fast-body path, or per-effect copy of root arithmetic exists.
class ContinuousMotionSolver final {
public:
  ContinuousMotionSolver(std::span<const ContactRule::Subject> bodies, const TickContext& context,
                         MotionLimits limits,
                         std::span<const MotionContactEffectPolicy> effect_policies);
  ~ContinuousMotionSolver();
  ContinuousMotionSolver(const ContinuousMotionSolver&) = delete;
  ContinuousMotionSolver& operator=(const ContinuousMotionSolver&) = delete;
  [[nodiscard]] std::optional<MotionGeometryEvent> next_geometry_event() const;
  [[nodiscard]] ContactRule::Subject subject(std::size_t index, MotionTime time) const;
  [[nodiscard]] std::size_t index_of(EntityId entity) const;
  [[nodiscard]] AnchoredMotion anchored_motion(std::size_t index) const;
  [[nodiscard]] std::uint64_t revision(std::size_t index) const;
  [[nodiscard]] bool terminated(std::size_t index) const;
  [[nodiscard]] MotionTime now() const;
  [[nodiscard]] MotionQueryBudget query_budget();
  [[nodiscard]] std::optional<PairContactObservation>
  contact(const MotionGeometryEvent& event) const;
  void begin_event(const MotionEventKey& key);
  void replace(std::size_t index, const MotionBodyResult& result);
  void reflect_wall(const MotionGeometryEvent& event);
  void finish_event(const std::optional<MotionGeometryEvent>& processed_pair);
  [[nodiscard]] MotionGeometryResult finish();

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace detail

// canonical: continuous_motion -- one pure chronological fixed-quantum solver, not wired to the
// live kernel until Step16. Bodies are already accelerated/dragged. Callbacks borrow committed
// world/facts and can replace only velocity, acceleration, and disposition. Failure publishes no
// partial result and mutates no world/facts. Trigger responses must advance their bounded cursor,
// change geometric velocity, or terminate. All times come from swept_geometry/MotionEventKey.
// Body impact admission uses the canonical circle polynomial's exact initial relation at summed
// effective radii, without the old discrete proximity band, and rejects exact tangent/stationary
// topology before testing closing velocity. Initial noncoincident contacts additionally require
// exact approaching radial motion. A revised retained certificate applies only that radial-sign
// veto to current motion, never a new hit-membership/topology gate or a changed normal/time.
// Distinct centers always use their geometric normal; only exact coincidence uses velocity.
// Gate occupancy separately retains its existing
// written binary64 distance predicate and authored-radius position-tolerance band.
// An epoch created exactly at t=1 uses a canonical-quantum reference line solely to classify
// closing initial contact topology. It can admit causal touching-body chains at t=1, but its
// nonzero reference roots never create additional travel or later contact events.
// Frozen per-object effect policies may request certified closed touches, including tangent,
// stationary, and separating overlap, through this same pair path. Impulse admission is unchanged.
// Observations are consumed at both post-response revisions even when the response is a no-op;
// external trajectory changes may re-enable them, within the same bounded event/effect budgets.
template <class Effect, class Facts>
[[nodiscard]] ContinuousMotionResult<Effect>
solve_continuous_motion(const GameWorld& committed_world,
                        const std::span<const ContactRule::Subject> bodies,
                        const TickContext& context, const Facts& facts,
                        const PairMotionResponseFunction<Effect, Facts> pair_response,
                        const std::span<const MotionTrigger<Effect, Facts>> triggers = {},
                        const MotionLimits limits = {},
                        const std::span<const MotionContactEffectPolicy> effect_policies = {}) {
  if (pair_response == nullptr) {
    detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                        "continuous motion requires a pair response function");
  }
  detail::require_motion_budget(triggers.size(), limits.trigger_declarations,
                                "motion trigger declaration budget");
  detail::ContinuousMotionSolver solver{bodies, context, limits, effect_policies};
  struct TriggerCache final {
    std::size_t body;
    std::uint64_t cursor;
    std::uint64_t revision{};
    bool initialized{};
    std::optional<MotionEventKey> event;
  };
  std::vector<TriggerCache> caches;
  for (const auto& trigger : triggers) {
    if (trigger.query == nullptr || trigger.response == nullptr ||
        trigger.initial_cursor > trigger.cursor_limit ||
        trigger.cursor_limit > limits.trigger_cursor ||
        trigger.feature_base > kMaximumProtocolSafeInteger - trigger.cursor_limit) {
      detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                          "invalid motion trigger callback, cursor, or feature range");
    }
    for (std::size_t earlier = 0; earlier < caches.size(); ++earlier) {
      const auto& other = triggers[earlier];
      if (other.entity == trigger.entity &&
          trigger.feature_base <= other.feature_base + other.cursor_limit &&
          other.feature_base <= trigger.feature_base + trigger.cursor_limit) {
        detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidInput,
                            "motion trigger feature ranges overlap for one entity");
      }
    }
    caches.push_back(
        {solver.index_of(trigger.entity), trigger.initial_cursor, 0, false, std::nullopt});
  }
  ContinuousMotionResult<Effect> result;
  auto append_effects = [&](const MotionEventKey& event, std::vector<Effect>& effects) {
    if (effects.size() > limits.effects - result.effects.size()) {
      detail::fail_motion(SimulationValidationCode::kContinuousMotionBudgetExceeded,
                          "continuous motion effect budget exhausted");
    }
    for (auto& effect : effects) {
      result.effects.push_back({event, std::move(effect)});
    }
  };
  for (;;) {
    auto budget = solver.query_budget();
    for (std::size_t index = 0; index < triggers.size(); ++index) {
      auto& cache = caches[index];
      const auto& trigger = triggers[index];
      if (solver.terminated(cache.body)) {
        cache.event.reset();
        continue;
      }
      const auto revision = solver.revision(cache.body);
      if (cache.initialized &&
          (cache.revision == revision || (cache.event && cache.event->time() == solver.now()))) {
        continue; // Preserve a tied geometric certificate across a velocity-only response.
      }
      budget.trigger();
      const MotionTriggerWindow window{solver.anchored_motion(cache.body), solver.now()};
      const auto proposal = trigger.query(committed_world, solver.subject(cache.body, solver.now()),
                                          window, context, facts, cache.cursor, budget);
      cache.event.reset();
      if (proposal) {
        if (proposal->time < solver.now() ||
            (proposal->priority != MotionEventPriority::kSupportLoss &&
             proposal->priority != MotionEventPriority::kCheckpoint)) {
          detail::fail_motion(SimulationValidationCode::kContinuousMotionInvalidResponse,
                              "motion trigger proposed a past time or undeclared priority");
        }
        cache.event = MotionEventKey::boundary(proposal->time, proposal->priority, trigger.entity,
                                               trigger.feature_base + cache.cursor);
      }
      cache.revision = revision;
      cache.initialized = true;
    }
    const auto geometry = solver.next_geometry_event();
    std::optional<MotionEventKey> selected = geometry ? std::optional{geometry->key} : std::nullopt;
    std::optional<std::size_t> selected_trigger;
    for (std::size_t index = 0; index < caches.size(); ++index) {
      if (caches[index].event && (!selected || *caches[index].event < *selected)) {
        selected = caches[index].event;
        selected_trigger = index;
      }
    }
    if (!selected) {
      break;
    }
    solver.begin_event(*selected);
    if (selected_trigger) {
      auto& cache = caches[*selected_trigger];
      const auto& trigger = triggers[*selected_trigger];
      const auto current = solver.subject(cache.body, selected->time());
      auto response = trigger.response(committed_world, current,
                                       MotionTriggerEvent{*selected, cache.cursor}, context, facts);
      const bool changes_motion =
          !current.body.is_static() && response.body.body.velocity() != current.body.velocity();
      if (response.cursor < cache.cursor || response.cursor > trigger.cursor_limit ||
          (response.cursor == cache.cursor && !changes_motion &&
           response.body.disposition != MotionDisposition::kTerminate)) {
        detail::fail_motion(
            SimulationValidationCode::kContinuousMotionNoProgress,
            "motion trigger must advance its cursor, change velocity, or terminate");
      }
      append_effects(*selected, response.effects);
      solver.replace(cache.body, response.body);
      cache.cursor = response.cursor;
      cache.initialized = false;
      cache.event.reset();
      solver.finish_event(std::nullopt);
    } else if (geometry->second) {
      if (const auto contact = solver.contact(*geometry)) {
        auto response = pair_response(
            committed_world, solver.subject(geometry->first, selected->time()),
            solver.subject(*geometry->second, selected->time()), *contact, context, facts);
        append_effects(*selected, response.effects);
        solver.replace(geometry->first, response.first);
        solver.replace(*geometry->second, response.second);
      }
      solver.finish_event(geometry);
    } else {
      solver.reflect_wall(*geometry);
      solver.finish_event(std::nullopt);
    }
  }
  result.motion = solver.finish();
  for (const auto& cache : caches) {
    result.trigger_cursors.push_back(cache.cursor);
  }
  return result;
}

} // namespace blob_royale::simulation

#endif
