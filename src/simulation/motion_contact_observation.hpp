#ifndef BLOB_ROYALE_SIMULATION_MOTION_CONTACT_OBSERVATION_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_CONTACT_OBSERVATION_HPP

#include "entity_id.hpp"
#include "physics.hpp"

#include <optional>

namespace blob_royale::simulation {

// canonical: contact_effect_policy -- eligibility of one object's effects on its counterpart.
// This policy never grants a physical impulse; only a certified closing impact admits one.
enum class ContactEffectPolicy { kClosingImpact, kAnyTouch };

// Frozen effective instance policy for one solve. Missing entries default to kClosingImpact;
// the solver rejects duplicate/absent identities and undeclared policy values before motion.
struct MotionContactEffectPolicy final {
  EntityId entity;
  ContactEffectPolicy policy = ContactEffectPolicy::kClosingImpact;
  friend bool operator==(const MotionContactEffectPolicy&,
                         const MotionContactEffectPolicy&) = default;
};

// canonical: pair_contact_observation -- one geometric certificate and its independent uses.
// touch preserves certified time-local geometry even for a tangent/stationary observation or
// a retained certificate whose current velocity no longer closes. impact exists only after
// exact topology/radial admission and the established closing-speed predicate. Eligibility is
// SOURCE-oriented: first_effect_eligible admits first's effects on second, and vice versa.
// Callers must use impact, never touch alone, to invoke the established impulse equations.
struct PairContactObservation final {
  PlayerPairContact touch;
  std::optional<PlayerPairContact> impact;
  bool first_effect_eligible;
  bool second_effect_eligible;
};

} // namespace blob_royale::simulation

#endif
