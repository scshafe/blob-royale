#ifndef BLOB_ROYALE_SIMULATION_EVENTS_CHARGE_CONTACT_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_CHARGE_CONTACT_EVENT_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"

namespace blob_royale::simulation {

enum class ChargeContactOutcome { kSuccessfulHit, kBlockedByShield };

// canonical: charge_contact_candidate -- frozen contact evidence for PostKernel commitment.
// A certified closing impact and incoming source motion nominate an active charge attempt. The
// activation identifies the attempt, so duplicates cannot refund or stun twice. Outcomes are
// captured before any same-tick status writes; mutual hits remain symmetric.
struct ChargeContactCandidate final {
  EntityId attacker;
  EntityId target;
  TickSequence activation_tick;
  ChargeContactOutcome outcome;

  friend bool operator==(const ChargeContactCandidate&, const ChargeContactCandidate&) = default;
};

} // namespace blob_royale::simulation

#endif
