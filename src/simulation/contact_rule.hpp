#ifndef BLOB_ROYALE_SIMULATION_CONTACT_RULE_HPP
#define BLOB_ROYALE_SIMULATION_CONTACT_RULE_HPP

#include "candidate_pair.hpp"
#include "contact_rule_name.hpp"
#include "entity_id.hpp"
#include "events/contact_event.hpp"
#include "motion_contact_observation.hpp"
#include "motion_response.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "simulation_validation_error.hpp"
#include "world_event_registry.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::simulation {

class GameWorld;
class TickContext;

// canonical: contact_response -- the complete effect one matched contact may have.
//
// **A response may change only the two bodies.** Every other consequence leaves as a WorldEvent
// for a later stage to apply, which is what keeps the kernel's mutation surface exactly what
// `docs/architecture/0003-deterministic-simulation-contract.md` pins and why a flag pickup is
// expressible without giving phase 3 write access to scores, teams, or the roster
// (`docs/architecture/0004-gameplay-architecture.md` § "Contact rules").
//
// `unchanged()` is the response of a row that matched and declined. It is a distinguished state
// rather than "the same two bodies", because a row that computed nothing must not have to name
// bodies it never looked at; `replaces_bodies()` is how the kernel tells the two apart, and
// reading a body from an unchanged response is a logic error rather than a silent default.
class ContactResponse final {
public:
  // Matched, and nothing happens: neither body changes and no event is emitted. First match still
  // wins, so a row that declines ends the walk for this pair.
  [[nodiscard]] static ContactResponse unchanged() noexcept { return ContactResponse{}; }

  // The two replacement bodies plus every event this contact produces, in production order.
  [[nodiscard]] static ContactResponse create(MotionBodyResult first_body,
                                              MotionBodyResult second_body,
                                              std::vector<WorldEvent> events) {
    return ContactResponse{BodyPair{first_body, second_body}, std::move(events)};
  }

  ContactResponse(const ContactResponse&) = default;
  ContactResponse(ContactResponse&&) noexcept = default;
  ContactResponse& operator=(const ContactResponse&) = default;
  ContactResponse& operator=(ContactResponse&&) noexcept = default;
  ~ContactResponse() = default;

  // Whether this response names replacement bodies. False exactly for `unchanged()`.
  [[nodiscard]] bool replaces_bodies() const noexcept { return bodies_.has_value(); }

  [[nodiscard]] const MotionBodyResult& first_result() const& { return require_bodies().first; }
  [[nodiscard]] const MotionBodyResult& first_result() const&& = delete;
  [[nodiscard]] const MotionBodyResult& second_result() const& { return require_bodies().second; }
  [[nodiscard]] const MotionBodyResult& second_result() const&& = delete;
  [[nodiscard]] const PhysicsBody& first_body() const& { return first_result().body; }
  [[nodiscard]] const PhysicsBody& first_body() const&& = delete;
  [[nodiscard]] const PhysicsBody& second_body() const& { return second_result().body; }
  [[nodiscard]] const PhysicsBody& second_body() const&& = delete;

  [[nodiscard]] std::span<const WorldEvent> events() const& noexcept { return events_; }
  [[nodiscard]] std::span<const WorldEvent> events() const&& = delete;

  friend bool operator==(const ContactResponse&, const ContactResponse&) = default;

private:
  struct BodyPair final {
    MotionBodyResult first;
    MotionBodyResult second;

    friend bool operator==(const BodyPair&, const BodyPair&) = default;
  };

  ContactResponse() noexcept = default;
  ContactResponse(BodyPair bodies, std::vector<WorldEvent> events) noexcept
      : bodies_(bodies), events_(std::move(events)) {}

  [[nodiscard]] const BodyPair& require_bodies() const& {
    if (!bodies_.has_value()) {
      throw SimulationValidationError(
          SimulationValidationCode::kContactResponseBodiesAbsent, "contact_response.bodies",
          "an unchanged contact response names no body; test replaces_bodies() first");
    }
    return *bodies_;
  }

  std::optional<BodyPair> bodies_;
  std::vector<WorldEvent> events_;
};

// canonical: contact_rule -- one row of the contact chain of responsibility.
// @extension-point contact_rule
//
// The continuous solver owns chronological contact admission and bounded re-observation after
// external trajectory changes. This table owns only policy precedence and row orientation;
// equations stay named pure functions in physics.hpp and must consume the certified impact.
//
// **Evaluation is a chain of responsibility.** For each canonical pair `(a, b)` with `a.id < b.id`
// the kernel walks the table's rows in declared order. A row matches in the canonical orientation
// when `first_predicate(a) && second_predicate(b)` and in the swapped orientation when
// `first_predicate(b) && second_predicate(a)`. Canonical orientation is tried before swapped, and
// the first matching (row, orientation) wins. A matched swapped row receives its arguments in row
// orientation and the kernel maps the returned bodies back to the canonical pair. A pair matching
// no row is unchanged, which makes the table total without a default row.
//
// Adding an interaction is a new row, never a kernel edit:
//
//   new  src/gameplay/<mode>/<name>_contact_rule.{hpp,cpp}  the predicates and the response
//   edit the mode's contact_rules()                         one row, at its declared precedence
//   do not touch                                            physics.hpp, phase 3
//
// related: contact_rule_table.hpp -- the ordered chain one mode declares.
// related: physics.hpp -- the pure equations a response calls.
class ContactRule final {
public:
  // One side of one evaluated contact, in row orientation. The body is the working copy phase 3
  // holds, which is the accelerated and possibly already-resolved value, never the committed one.
  struct Subject final {
    EntityId entity;
    PhysicsBody body;

    friend bool operator==(const Subject&, const Subject&) = default;
  };

  // Free function pointers, not std::function: a predicate or a response structurally cannot
  // capture state, which is how purity is enforced rather than merely requested.
  //
  // Both callbacks read the frozen post-phase-0/post-kPreKernel world. Current resolved motion
  // comes only from Subject; frozen body velocity is not a contact-time velocity. Responses may
  // replace velocity/acceleration/disposition and emit typed events, never mutate that world.
  using Predicate = bool (*)(const GameWorld& world, EntityId entity);
  using Response = ContactResponse (*)(const GameWorld& world, const Subject& first,
                                       const Subject& second,
                                       const PairContactObservation& observation,
                                       const TickContext& context);

  // Creates a validated row or throws SimulationValidationError for an empty or non-snake_case
  // name, a null predicate, and a null response.
  [[nodiscard]] static ContactRule create(std::string_view name, Predicate first_predicate,
                                          Predicate second_predicate, Response response) {
    // The name is copied into the row's own bounded storage, so a row built from a temporary
    // string is a complete value rather than a view that outlives its characters.
    const ContactRuleName validated_name = ContactRuleName::create(name);
    if (first_predicate == nullptr || second_predicate == nullptr) {
      throw SimulationValidationError(
          SimulationValidationCode::kContactRulePredicateMissing, "contact_rule.predicate",
          "contact rule " + std::string(name) + " must declare both predicates");
    }
    if (response == nullptr) {
      throw SimulationValidationError(
          SimulationValidationCode::kContactRuleResponseMissing, "contact_rule.response",
          "contact rule " + std::string(name) + " must declare a response");
    }
    return ContactRule{validated_name, first_predicate, second_predicate, response};
  }

  ContactRule(const ContactRule&) = default;
  ContactRule(ContactRule&&) noexcept = default;
  ContactRule& operator=(const ContactRule&) = default;
  ContactRule& operator=(ContactRule&&) noexcept = default;
  ~ContactRule() = default;

  // The row's stable identity, which is also what a ContactEvent copies so a consuming system can
  // tell which interaction produced it. The view names this row's own storage and is valid for as
  // long as the row is, which is the life of the match; a value that must outlive the row copies
  // `owned_name()` instead.
  [[nodiscard]] std::string_view name() const& noexcept { return name_.value(); }
  [[nodiscard]] std::string_view name() const&& = delete;
  [[nodiscard]] const ContactRuleName& owned_name() const& noexcept { return name_; }
  [[nodiscard]] const ContactRuleName& owned_name() const&& = delete;
  [[nodiscard]] Predicate first_predicate() const noexcept { return first_predicate_; }
  [[nodiscard]] Predicate second_predicate() const noexcept { return second_predicate_; }
  [[nodiscard]] Response response() const noexcept { return response_; }

  friend bool operator==(const ContactRule&, const ContactRule&) = default;

private:
  ContactRule(ContactRuleName name, const Predicate first_predicate,
              const Predicate second_predicate, const Response response) noexcept
      : name_(name), first_predicate_(first_predicate), second_predicate_(second_predicate),
        response_(response) {}

  ContactRuleName name_;
  Predicate first_predicate_;
  Predicate second_predicate_;
  Response response_;
};

// canonical: contact_event_of_match -- the one ContactEvent a matched contact publishes.
//
// Published rather than private because every row that reports a contact needs exactly this value
// and no row should have to re-derive the canonical orientation itself. The pair is canonicalized
// by CandidatePair, and the normal is expressed in that same canonical `(lower -> higher)` sense
// whichever orientation matched, so a consuming system reads one convention. The relative normal
// speed is orientation-invariant: reversing both the relative velocity and the normal reverses two
// signs in each product.
[[nodiscard]] inline ContactEvent contact_event_of(const ContactRule::Subject& first,
                                                   const ContactRule::Subject& second,
                                                   const PlayerPairContact& contact,
                                                   const ContactRuleName& rule_name) {
  const bool row_is_canonical = first.entity < second.entity;
  return ContactEvent{CandidatePair::create(first.entity, second.entity),
                      row_is_canonical ? contact.normal() : -contact.normal(),
                      contact.relative_normal_speed(), rule_name};
}

} // namespace blob_royale::simulation

#endif
