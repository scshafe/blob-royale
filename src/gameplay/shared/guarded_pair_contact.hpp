#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_GUARDED_PAIR_CONTACT_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_GUARDED_PAIR_CONTACT_HPP

#include "contact_rule.hpp"
#include "motion_contact_observation.hpp"
#include "motion_response.hpp"

#include <variant>

namespace blob_royale::gameplay {

enum class GuardState { kNone, kOrdinary, kPerfect };

// Frozen admission facts, never consumed by a hit. Step 18 supplies these from its real component
// and half-open window owners; this core owns neither a Shield component nor timer arithmetic.
struct PairGuardFacts final {
  GuardState first = GuardState::kNone;
  GuardState second = GuardState::kNone;
  friend bool operator==(const PairGuardFacts&, const PairGuardFacts&) = default;
};

struct GuardedPairContactFact final {
  simulation::ContactEvent contact;
  friend bool operator==(const GuardedPairContactFact&, const GuardedPairContactFact&) = default;
};

struct GuardedPairEliminationFact final {
  simulation::EntityId entity;
  friend bool operator==(const GuardedPairEliminationFact&,
                         const GuardedPairEliminationFact&) = default;
};

struct GuardedPairStunFact final {
  simulation::EntityId entity;
  friend bool operator==(const GuardedPairStunFact&, const GuardedPairStunFact&) = default;
};

// Typed gameplay consequences, not newly registered WorldEvents. At most two recipient facts in
// ascending EntityId order precede one canonical contact fact; Step 18 translates them once.
using GuardedPairConsequence =
    std::variant<GuardedPairContactFact, GuardedPairEliminationFact, GuardedPairStunFact>;
using GuardedPairOutcome = simulation::PairMotionResponse<GuardedPairConsequence>;

// canonical: guarded_pair_contact -- the permanent pair-symmetric defense composition.
//
// Reads committed presence/phase only; subjects carry working motion and the caller supplies a
// observation certified in their orientation. Effect eligibility is SOURCE-oriented: first's
// eligibility permits first's lethal effect on second, and vice versa; a victim's own policy does
// not admit an incoming effect. Touch geometry supplies diagnostics, never an impulse or parry.
// Only an optional closing impact enters the accepted baseline/general/static equations, quarters
// each guarded body's received velocity delta, applies simultaneous perfect stops from PRE-response
// incoming motion, then projects guarded closing normal motion away. With no guard, the chosen base
// equation's velocities are preserved exactly, including any rounding residual.
//
// The quartered response is an external gameplay impulse and can increase world-frame kinetic
// energy. Only its final inverse-mass-weighted separation projection is dissipative: static and
// newly stopped bodies have zero correction weight and zero motion velocity. A static body's
// stored velocity is preserved but is not geometric motion or part of that energy claim.
// Stops kill acceleration,
// not future external impulses, and do not change PhysicsBody::is_static.
//
// Projection is one written binary64 solve using the supplied normal's squared length. Its
// residual must satisfy the existing absolute-plus-relative kVelocityTolerance comparison to
// zero. Failure is visible, not a second nudge, iterative correction, or hidden time epsilon.
// Throws SIMULATION.GUARDED_PAIR_FACTS_INVALID for invalid states, repeated identity, a static
// guard; SIMULATION.GUARDED_PAIR_SEPARATION_FAILED for unrepresentable
// correction or a closing residual. Non-contact inputs, and observations with neither eligible
// source nor an impact, return unchanged bodies and no facts. An eligible touch without impact
// preserves both bodies and emits a contact fact unless an unguarded lethal recipient terminates.
[[nodiscard]] GuardedPairOutcome
compose_guarded_pair(const simulation::GameWorld& committed,
                     const simulation::ContactRule::Subject& first,
                     const simulation::ContactRule::Subject& second,
                     const simulation::PairContactObservation& observation,
                     const simulation::TickContext& context, const PairGuardFacts& guards);

} // namespace blob_royale::gameplay

#endif
