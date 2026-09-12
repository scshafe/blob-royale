#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_GUARDED_PAIR_CONTACT_RULE_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_GUARDED_PAIR_CONTACT_RULE_HPP

#include "contact_rule.hpp"
#include "entity_id.hpp"

#include <string_view>

namespace blob_royale::gameplay {

// canonical: guarded_pair_contact_rule -- the one live row every gameplay pair resolves through.
// @extension-point contact_rule
//
// **One pair-symmetric row, not a table of flag combinations.** A `ContactRule::Response` is a raw
// function pointer that structurally cannot capture state (`../../simulation/contact_rule.hpp`), so
// the only way defensive state could have reached a response through the table alone is by *which*
// row matched: one row per (guarded, unguarded) x (perfect, ordinary) x (lethal, ordinary)
// combination, each with its own predicate pair and its own place in the precedence order. ADR 0008
// rejects that table because it grows multiplicatively -- every new defensive state doubles it, and
// the precedence between the rows becomes a fourth unwritten thing to get right. This row instead
// matches on presence alone and reads the defensive facts itself, out of the same committed world
// every predicate already receives, so the combinations live in `compose_guarded_pair`'s branches
// where they are one tested pure function rather than a declaration order.
//
// **It is declared above the built-in rows, so in the four gameplay modes they are unreachable.**
// `first_match` takes the first matching (row, orientation) and both sides of this row are the same
// total presence test, so every pair the broad phase offers matches it canonically. That is
// deliberate and it is the price of having exactly one response path: royale, king_of_the_hill,
// race and sandbox no longer reach `variable_impulse`, `elastic_disc` or `reflect_static`. Little
// is lost, because the composition selects the same two accepted pair equations by the same
// `body_has_baseline_physics` test the built-in predicates use, and an unguarded pair keeps their
// bits exactly. The built-ins stay the engine's baseline for a mode that declares no rows at all --
// the kernel's own default table and the simulation-only test doubles still resolve through them --
// which keeps them a live tested baseline rather than dead code.
//
// Two differences from the built-ins are real and are not rounding. A dynamic/static pair reflects
// only while the contact is closing, where `reflect_static` reflects on any admitted impact; and a
// touch carrying no impact and no eligible source emits no `ContactEvent` at all, where a built-in
// row emits one unconditionally. Both are the composition's accepted Step 4 behaviour, pinned by
// `tests/unit/gameplay/shared/guarded_pair_contact_tests.cpp`, not something this adapter adds.
//
// **One row, two diagnostic names.** The declared row name is `guarded_pair`, and every composed
// non-lethal contact now publishes it in place of the `variable_impulse`, `elastic_disc` or
// `reflect_static` name a gameplay pair used to report. The unguarded-lethal branch still publishes
// `lethal_hazard` (`guarded_pair_contact.hpp`), because that name is what the accepted hazard
// pass-through proofs assert and what a reader looking for "a comet killed someone" greps for. A
// `ContactEvent.rule_name` therefore names the interaction that fired, not always a declared row.
//
// **Defense is fixed for the whole tick, by construction.** Predicates and responses read the
// frozen post-phase-0/post-kPreKernel world, and the solver may re-observe one pair several times
// inside a quantum; projecting the guard out of that one frozen world means every observation of
// every pair in the tick sees the same `Shield`. A shield that `ability` activates at kPreKernel
// therefore protects on the tick it was requested, and a shield that `status` cancels at
// kPostKernel still protected for the whole of the tick it was cancelled on. That bounded same-tick
// grace is intended rather than tolerated: the alternative is contact-time status mutation, which
// would make a pair's outcome depend on the order the solver happened to visit pairs in
// (`docs/reviews/2026-09-12-shield-composition-contract.md` § "One live response path").
//
// **Cliffs and zone rules bypass shield entirely, because neither is a contact.** Support loss is a
// `MotionTrigger` and zone elimination is a kPostKernel system; no contact row is consulted on
// either path, so no `Shield` is read and none could be. A shield answers what a blob touches, not
// where it stands: it is not an invulnerability window.
// related: guarded_pair_contact.hpp -- the pure core this adapter projects facts into and adapts.
// related: ../../simulation/components/shield_component.hpp -- the committed facts it reads.
// related: ability_system.hpp -- the kPreKernel owner that writes them.
// related: ../../simulation/contact_rule_table.hpp -- the chain this is the first row of.

// The declared row name, published so a consuming system matches a `ContactEvent`'s `rule_name`
// against one identity rather than a repeated literal, exactly as the built-in row names are. The
// pure core emits the same characters for every composed non-lethal contact.
inline constexpr std::string_view kGuardedPairContactRuleName = "guarded_pair";

// Whether the entity carries any `PhysicsBody`, static or dynamic. Both sides of the row use this
// one test. Static/static pairs never reach a row -- the broad phase drops them before any table is
// consulted -- so a symmetric presence test is safe for walls while still admitting every
// dynamic/dynamic and dynamic/static pair. Total in the same way the built-in predicates are: an
// entity carrying no body, such as the zone or a deferred joiner, satisfies neither side, so the
// walk ends with "no row matched" rather than with a lookup failure.
[[nodiscard]] bool body_has_contact_presence(const simulation::GameWorld& world,
                                             simulation::EntityId entity);

// Projects each subject's committed guard, calls `compose_guarded_pair` once, and translates its
// typed consequences into WorldEvents in the core's own order. A stun request carries the
// **defending** body's captured `parry_stun_duration_ticks`, which is the opposite subject: the
// duration belongs to the shield that parried, never to the body being stunned and never to a
// configuration this noncapturing function structurally could not hold.
[[nodiscard]] simulation::ContactResponse guarded_pair_response(
    const simulation::GameWorld& world, const simulation::ContactRule::Subject& first,
    const simulation::ContactRule::Subject& second,
    const simulation::PairContactObservation& observation, const simulation::TickContext& context);

// The complete row, so a mode declares it by name rather than by re-pairing the three parts and
// risking a predicate in the wrong position.
[[nodiscard]] simulation::ContactRule guarded_pair_contact_rule();

} // namespace blob_royale::gameplay

#endif
