#include "shared/guarded_pair_contact_rule.hpp"

#include "component_store.hpp"
#include "components/shield_component.hpp"
#include "events/elimination_event.hpp"
#include "events/stun_request_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "physics_body.hpp"
#include "shared/guarded_pair_contact.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "world_event_registry.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::gameplay {
namespace {

// The one projection from committed state into the core's frozen facts. Read out of `world`, never
// out of the subject bodies: a `Subject` carries this quantum's working motion, which is exactly
// the value a defensive decision must not depend on.
//
// A static subject is forced to `kNone` rather than merely expected to have no shield, and the
// forcing is load-bearing: `compose_guarded_pair` throws SIMULATION.GUARDED_PAIR_FACTS_INVALID for
// a guarded static subject, so a stale `Shield` left on a body that later became static -- or a
// wall an authoring mistake gave one -- would fail the whole tick on the first blob-versus-wall
// contact instead of doing the obvious thing. Walls do not defend; they are the thing defended
// against.
[[nodiscard]] GuardState guard_state_of(const simulation::GameWorld& world,
                                        const simulation::ContactRule::Subject& subject,
                                        const simulation::TickSequence tick) {
  if (subject.body.is_static()) {
    return GuardState::kNone;
  }
  const simulation::Shield* shield = world.store<simulation::Shield>().find(subject.entity);
  if (shield == nullptr) {
    return GuardState::kNone;
  }
  // Perfect is tested first because the perfect window is a prefix of the shield window: a tick
  // inside both is the short opening, and reading them the other way round would make the opening
  // unreachable.
  if (shield->perfect_window().contains(tick)) {
    return GuardState::kPerfect;
  }
  if (shield->shield_window().contains(tick)) {
    return GuardState::kOrdinary;
  }
  // Expired protection whose cooldown is still running, and protection cancelled by a stun earlier
  // in the match, both land here: a `Shield` that exists is not a `Shield` that defends.
  return GuardState::kNone;
}

// The parry duration the defending shield captured when it activated. A stun fact implies the
// opposite subject's guard was `kPerfect`, which implies that subject carried a `Shield` on this
// same frozen world, so an absent one is a structurally impossible state and is reported as one
// rather than silently becoming a zero-tick stun that `status` would then discard as a no-op.
[[nodiscard]] std::uint64_t
captured_parry_duration_ticks(const simulation::GameWorld& world,
                              const simulation::ContactRule::Subject& defender) {
  const simulation::Shield* shield = world.store<simulation::Shield>().find(defender.entity);
  if (shield == nullptr) {
    throw GameplayValidationError(
        GameplayValidationCode::kGuardedPairStunDefenderWithoutShield,
        "guarded_pair_contact_rule.stun.defender",
        "entity " + std::to_string(defender.entity.value()) +
            " parried a contact while carrying no shield, so the stun it owes has no captured "
            "duration");
  }
  return shield->parry_stun_duration_ticks();
}

} // namespace

bool body_has_contact_presence(const simulation::GameWorld& world,
                               const simulation::EntityId entity) {
  return world.store<simulation::PhysicsBody>().find(entity) != nullptr;
}

simulation::ContactResponse guarded_pair_response(
    const simulation::GameWorld& world, const simulation::ContactRule::Subject& first,
    const simulation::ContactRule::Subject& second,
    const simulation::PairContactObservation& observation, const simulation::TickContext& context) {
  const simulation::TickSequence tick = context.tick_sequence();
  const PairGuardFacts guards{guard_state_of(world, first, tick),
                              guard_state_of(world, second, tick)};
  // One call, and the only call: every equation, every window comparison and every disposition is
  // the core's. This function owns the projection in and the translation out, and nothing else.
  const GuardedPairOutcome outcome =
      compose_guarded_pair(world, first, second, observation, context, guards);

  std::vector<simulation::WorldEvent> events;
  events.reserve(outcome.effects.size());
  for (const GuardedPairConsequence& consequence : outcome.effects) {
    // The core already produced these in the tick's production order -- recipients in ascending
    // EntityId, then the one canonical contact -- so the walk preserves that order rather than
    // grouping by kind.
    if (const auto* contact = std::get_if<GuardedPairContactFact>(&consequence);
        contact != nullptr) {
      // Verbatim: the fact already carries the canonical pair, the canonically oriented normal and
      // the rule name the branch chose, which is `lethal_hazard` or `guarded_pair`.
      events.emplace_back(simulation::WorldEvent{contact->contact});
      continue;
    }
    if (const auto* elimination = std::get_if<GuardedPairEliminationFact>(&consequence);
        elimination != nullptr) {
      events.emplace_back(
          simulation::WorldEvent{simulation::EliminationEvent{elimination->entity}});
      continue;
    }
    const auto& stun = std::get<GuardedPairStunFact>(consequence);
    // The defender is the opposite subject by definition: a body is stunned because it rammed
    // somebody else's perfect shield, so the duration is read off that other body.
    const simulation::ContactRule::Subject& defender = stun.entity == first.entity ? second : first;
    events.emplace_back(simulation::WorldEvent{
        simulation::StunRequest{stun.entity, captured_parry_duration_ticks(world, defender)}});
  }

  // Always `create`, never `unchanged()`: a matched pair that changes neither body may still owe a
  // contact event, `unchanged()` matches and emits nothing, and reading a body off it throws.
  return simulation::ContactResponse::create(outcome.first, outcome.second, std::move(events));
}

simulation::ContactRule guarded_pair_contact_rule() {
  return simulation::ContactRule::create(kGuardedPairContactRuleName, body_has_contact_presence,
                                         body_has_contact_presence, guarded_pair_response);
}

} // namespace blob_royale::gameplay
