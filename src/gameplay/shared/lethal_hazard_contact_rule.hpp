#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_LETHAL_HAZARD_CONTACT_RULE_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_LETHAL_HAZARD_CONTACT_RULE_HPP

#include "contact_rule.hpp"
#include "entity_id.hpp"

#include <string_view>

namespace blob_royale::gameplay {

// canonical: lethal_hazard_contact_rule -- touching a lethal hazard eliminates the player.
// @extension-point contact_rule
//
// One row, declared by a mode **above** every impulse row, so lethality wins over bouncing. Order
// is the whole of that: `ContactRuleTable::first_match` takes the first matching row, and both
// `variable_impulse` and `elastic_disc` would otherwise match the same pair -- a hazard is a
// dynamic body with a non-baseline mass, so `variable_impulse` matches it -- and shove the player
// aside instead of killing them. Declared below either one, this row would be unreachable.
//
// **The response leaves the hazard travelling.** It returns both bodies unchanged and emits one
// `EliminationEvent` for the player. That is deliberate: a hazard that crosses the arena must not
// be deflected by what it kills, or a comet would visibly stagger through a crowd, and the player
// is about to be destroyed so its velocity is not a value anyone reads. It is the reason the row
// cannot be `ContactResponse::unchanged()`, which matches and declines *and emits nothing*: this
// row matches, declines to change the physics, and still has an event to produce.
//
// **The two predicates are asymmetric on purpose, and neither is "not a hazard".** The first is
// `LethalOnContact` presence; the second is `Controllable` presence, which is the engine's own
// spelling of "a player is driving this". Testing the *absence* of `LethalOnContact` on the second
// side would have been the obvious dual and is worse in three ways: a hazard would eliminate the
// zone entity, a hazard would eliminate a map's static walls, and two hazards crossing would be a
// pair where each side satisfies the other's predicate. Requiring a driver on the victim's side
// rules out all three at once with one test, and it matches what `placement_recorder` already
// demands -- it throws `kRoyaleEliminatedEntityWithoutController` for an eliminated entity carrying
// no `Controllable`, so a predicate that could name one would turn a rule mistake into a hard tick
// failure at `kLifecycle` instead of a contact that never matched.
//
// A hazard therefore cannot eliminate another hazard, a wall, or the zone, and the row is total:
// an entity carrying neither kind satisfies neither predicate.
//
// **Lethality ignores the zone grace period, and that is the point.** `zone_elimination` grants
// `elimination_grace_ticks` outside the zone before it emits; this row emits on the tick of
// contact. They are two different rules with two different causes -- "you strayed" is a warning
// with a countdown, "a comet hit you" is an event -- and a grace period on the second would mean a
// player walks through a comet unharmed. Both emit the same `EliminationEvent` into the same tick's
// list, so `placement_recorder` gives a hazard kill and a zone kill the same placement by the same
// arithmetic; it already sorts and de-duplicates the tick's set, so an entity both rules name on
// one tick is one elimination and one placement.
//
// **A hazard is lethal only while the match is `running`.** The first predicate reads the committed
// phase beside the marker, so outside `running` the row never matches and the pair falls through
// to `variable_impulse`: a comet in `ended`, `lobby`, or `countdown` shoves a player and kills
// nobody. The gate is here rather than in `placement_recorder` or in a hazard wipe at
// `running -> ended` for two reasons. Gating the recorder would leave an `EliminationEvent` that
// nothing consumes, which hides a producer bug behind a silent tick; and destroying hazards when a
// match ends would make a boulder vanish mid-screen. Reading the phase on the hazard's side keeps
// every other row and every other system exactly as it was, and it restores ADR 0005's rule that
// eliminations happen only during `running` -- a rule `zone_elimination` kept by construction and
// this row broke by omission when it landed (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`,
// finding 2). A `hazard_spawn` that runs only while `running` was never enough on its own, because
// a hazard outlives the phase it was spawned in by its whole `Lifetime`.
//
// It lives in `shared/` for the reason `thrust_steering_system.hpp` states: an object that kills on
// touch is a mechanic any mode may field, and filing it under `royale/` would make the second mode
// that wants hazards reach into the first. Royale declares it; sandbox does not, which is the test
// that the mechanic is optional rather than ambient.
// related: ../../simulation/components/lethal_on_contact_component.hpp -- the marker it reads.
// related: ../royale/royale_mode.hpp -- the one mode that declares it today.
// related: ../../simulation/contact_rule_table.hpp -- the chain this is one row of.

// The row's declared name, published so a consuming system matches a `ContactEvent`'s `rule_name`
// against one identity rather than a repeated literal, exactly as the built-in row names are.
inline constexpr std::string_view kLethalHazardContactRuleName = "lethal_hazard";

// Whether the entity carries `LethalOnContact` **and the match is `running`**. Total: an entity
// carrying no such component does not satisfy it, so a pair the broad phase could not have produced
// resolves to "no row matched" rather than to a lookup failure, and outside `running` no entity
// satisfies it at all.
[[nodiscard]] bool body_is_lethal_hazard(const simulation::GameWorld& world,
                                         simulation::EntityId entity);

// Whether the entity carries `Controllable`, which is the engine's spelling of "a player drives
// this". See the note above for why this rather than the absence of `LethalOnContact`.
[[nodiscard]] bool body_is_player_driven(const simulation::GameWorld& world,
                                         simulation::EntityId entity);

// Both bodies unchanged, one `EliminationEvent` for the player, and one `ContactEvent` under this
// row's name so a diagnostic can see which rule fired. `first` is the hazard and `second` the
// player, which is the row orientation the predicates above fix.
[[nodiscard]] simulation::ContactResponse lethal_hazard_response(
    const simulation::GameWorld& world, const simulation::ContactRule::Subject& first,
    const simulation::ContactRule::Subject& second,
    const simulation::PairContactObservation& observation, const simulation::TickContext& context);

// The complete row, so a mode declares it by name rather than by re-pairing the three parts and
// risking a predicate in the wrong position.
[[nodiscard]] simulation::ContactRule lethal_hazard_contact_rule();

} // namespace blob_royale::gameplay

#endif
