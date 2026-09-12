#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_GUARDED_PAIR_CONTACT_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_GUARDED_PAIR_CONTACT_HPP

#include "contact_rule.hpp"
#include "entity_id.hpp"
#include "motion_contact_observation.hpp"
#include "motion_response.hpp"

#include <string_view>
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

// canonical: lethal_contact_predicates -- who may kill on touch, and who may be killed.
//
// These three moved here unchanged when plan Step 18 deleted
// `shared/lethal_hazard_contact_rule.{hpp,cpp}` and left the composition below as their only
// non-test consumer. The standalone row is gone; the admission it encoded is not, and neither is
// the diagnostic name a lethal contact still publishes, so both now live beside the branch that
// reads them (`docs/reviews/2026-09-12-shield-composition-contract.md` § "One live response path").
// Nothing about their semantics changed in the move.
//
// **The two predicates are asymmetric on purpose, and neither is "not a hazard".** The first is
// `LethalOnContact` presence; the second is `Controllable` presence, which is the engine's own
// spelling of "a player is driving this". Testing the *absence* of `LethalOnContact` on the second
// side would have been the obvious dual and is worse in three ways: a hazard would eliminate the
// zone entity, a hazard would eliminate a map's static walls, and two hazards crossing would be a
// pair where each side satisfies the other's predicate. Requiring a driver on the victim's side
// rules out all three at once with one test, and it matches what `placement_recorder` already
// demands -- it throws `kRoyaleEliminatedEntityWithoutController` for an eliminated entity carrying
// no `Controllable`, so an admission that could name one would turn a rule mistake into a hard tick
// failure at `kLifecycle` instead of a contact that never killed.
//
// A hazard therefore cannot eliminate another hazard, a wall, or the zone, and the pair is total:
// an entity carrying neither kind satisfies neither test.
//
// **Lethality ignores the zone grace period, and that is the point.** `zone_elimination` grants
// `elimination_grace_ticks` outside the zone before it emits; the lethal branch emits on the tick
// of contact. They are two different rules with two different causes -- "you strayed" is a warning
// with a countdown, "a comet hit you" is an event -- and a grace period on the second would mean a
// player walks through a comet unharmed. Both emit the same `EliminationEvent` into the same tick's
// list, so `placement_recorder` gives a hazard kill and a zone kill the same placement by the same
// arithmetic; it already sorts and de-duplicates the tick's set, so an entity both rules name on
// one tick is one elimination and one placement.
//
// **A hazard is lethal only while the match is `running`.** The first test reads the committed
// phase beside the marker, so outside `running` nothing is lethal and the pair falls through to the
// ordinary physical composition: a comet in `ended`, `lobby`, or `countdown` shoves a player and
// kills nobody. The gate is here rather than in `placement_recorder` or in a hazard wipe at
// `running -> ended` for two reasons. Gating the recorder would leave an `EliminationEvent` that
// nothing consumes, which hides a producer bug behind a silent tick; and destroying hazards when a
// match ends would make a boulder vanish mid-screen. Reading the phase on the hazard's side keeps
// every other rule and every other system exactly as it was, and it restores ADR 0005's rule that
// eliminations happen only during `running` -- a rule `zone_elimination` kept by construction and
// the deleted row broke by omission when it landed
// (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 2). A `hazard_spawn` that runs
// only while `running` was never enough on its own, because a hazard outlives the phase it was
// spawned in by its whole `Lifetime`.
// related: ../../simulation/components/lethal_on_contact_component.hpp -- the marker it reads.
// related: guarded_pair_contact_rule.hpp -- the live row whose response calls the composition.

// The diagnostic name a lethal contact still publishes, published so a consuming system matches a
// `ContactEvent`'s `rule_name` against one identity rather than a repeated literal. It is no longer
// a declared row name: the one declared gameplay row is `guarded_pair`, and it deliberately reports
// two names so the accepted lethal pass-through proofs keep the identity they always asserted.
inline constexpr std::string_view kLethalHazardContactRuleName = "lethal_hazard";

// Whether the entity carries `LethalOnContact` **and the match is `running`**. Total: an entity
// carrying no such component does not satisfy it, so a pair the broad phase could not have produced
// resolves to "not lethal" rather than to a lookup failure, and outside `running` no entity
// satisfies it at all.
[[nodiscard]] bool body_is_lethal_hazard(const simulation::GameWorld& world,
                                         simulation::EntityId entity);

// Whether the entity carries `Controllable`, which is the engine's spelling of "a player drives
// this". See the note above for why this rather than the absence of `LethalOnContact`.
[[nodiscard]] bool body_is_player_driven(const simulation::GameWorld& world,
                                         simulation::EntityId entity);

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
