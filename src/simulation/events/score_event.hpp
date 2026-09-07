#ifndef BLOB_ROYALE_SIMULATION_EVENTS_SCORE_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_SCORE_EVENT_HPP

#include "entity_id.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: score_event -- one scoring award a stage decided and a later system applies.
//
// The event is a *delta*, not a total, and it names the entity that owns the scoreboard cell --
// a player entity in a free-for-all, a team entity in a team mode -- exactly as the Score
// component does. Points are signed so a penalty needs no second kind. The contact phase may
// decide a score without write access to the Score store, which is what keeps the kernel's
// mutation surface the two bodies of a matched pair
// (`docs/architecture/0004-gameplay-architecture.md` § "Contact rules").
// related: world_event_registry.hpp -- the closed list of event kinds.
// related: components/score_component.hpp -- the cell an applying system writes.
struct ScoreEvent final {
  EntityId entity;
  std::int64_t points{};

  friend bool operator==(const ScoreEvent&, const ScoreEvent&) = default;
};

} // namespace blob_royale::simulation

#endif
