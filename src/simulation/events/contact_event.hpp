#ifndef BLOB_ROYALE_SIMULATION_EVENTS_CONTACT_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_CONTACT_EVENT_HPP

#include "candidate_pair.hpp"
#include "vector2.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: contact_event -- one resolved contact, published to the systems that consume it.
//
// The event carries the canonical `(lower id, higher id)` pair, the contact normal, the relative
// normal speed, and the name of the contact rule row that matched, which is what lets one generic
// contact phase feed many different consuming systems without the phase naming any of them
// (`docs/architecture/0004-gameplay-architecture.md` § "World events").
//
// `rule_name` is a view of the matched row's `name()`. A row's name is a static identity owned by
// the mode's contact rule table, which outlives every tick, and the tick's event list is cleared
// at commit, so the view cannot outlive the characters it names. The contact phase that produces
// this event arrives with the rule table in Step 18; nothing emits one yet.
// related: world_event_registry.hpp -- the closed list of event kinds.
struct ContactEvent final {
  CandidatePair pair;
  Vector2 normal;
  double relative_normal_speed{};
  std::string_view rule_name;

  friend bool operator==(const ContactEvent&, const ContactEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
