#ifndef BLOB_ROYALE_SIMULATION_EVENTS_CONTACT_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_CONTACT_EVENT_HPP

#include "candidate_pair.hpp"
#include "contact_rule_name.hpp"
#include "vector2.hpp"

namespace blob_royale::simulation {

// canonical: contact_event -- one resolved contact, published to the systems that consume it.
//
// The event carries the canonical `(lower id, higher id)` pair, the contact normal, the relative
// normal speed, and the name of the contact rule row that matched, which is what lets one generic
// contact phase feed many different consuming systems without the phase naming any of them
// (`docs/architecture/0004-gameplay-architecture.md` § "World events").
//
// `rule_name` is an **owned** ContactRuleName rather than a view of the matched row's name. The
// event outlives the call that produced it -- it sits in the tick's event list until commit -- and
// a mode is free to build a row from a temporary string, so a borrowed view would dangle in
// exactly the case that surfaces as a garbled diagnostic rather than as a failure. The name is
// bounded, so owning it costs a fixed copy and no allocation.
//
// `normal` is always expressed in the canonical `(lower id -> higher id)` sense, whichever
// orientation the matched row was evaluated in, so a consuming system reads one convention.
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: contact_rule_name.hpp -- why the name is owned.
struct ContactEvent final {
  CandidatePair pair;
  Vector2 normal;
  double relative_normal_speed{};
  ContactRuleName rule_name;

  friend bool operator==(const ContactEvent&, const ContactEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
