#include "shared/lethal_hazard_contact_rule.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "contact_rule_name.hpp"
#include "events/elimination_event.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "world_event_registry.hpp"

namespace blob_royale::gameplay {

namespace simulation = blob_royale::simulation;

bool body_is_lethal_hazard(const simulation::GameWorld& world, const simulation::EntityId entity) {
  // The phase is read here, on the hazard's side, because "lethal" is a property a hazard has only
  // while a match is on: outside `running` the same body still carries the marker and is still a
  // heavy disc the impulse rows resolve, it just cannot kill. See the header for why the gate is
  // the row's and not the recorder's.
  return world.match().phase == simulation::MatchPhase::kRunning &&
         world.store<simulation::LethalOnContact>().find(entity) != nullptr;
}

bool body_is_player_driven(const simulation::GameWorld& world, const simulation::EntityId entity) {
  return world.store<simulation::Controllable>().find(entity) != nullptr;
}

simulation::ContactResponse lethal_hazard_response(const simulation::ContactRule::Subject& first,
                                                   const simulation::ContactRule::Subject& second,
                                                   const simulation::PlayerPairContact& contact,
                                                   const simulation::TickContext&) {
  // Both bodies verbatim: the hazard keeps its course and the player keeps the velocity it had.
  // Neither is `unchanged()`, which emits no events, and this row's whole purpose is its event.
  //
  // `second` is the player, fixed by the row's predicates: the first predicate is the hazard marker
  // and the second is the driver, so whichever orientation matched, the kernel presents the
  // arguments in row order and this function never has to ask which side it is holding.
  return simulation::ContactResponse::create(
      first.body, second.body,
      {simulation::WorldEvent{simulation::EliminationEvent{second.entity}},
       simulation::WorldEvent{simulation::contact_event_of(
           first, second, contact,
           simulation::ContactRuleName::create(kLethalHazardContactRuleName))}});
}

simulation::ContactRule lethal_hazard_contact_rule() {
  return simulation::ContactRule::create(kLethalHazardContactRuleName, body_is_lethal_hazard,
                                         body_is_player_driven, lethal_hazard_response);
}

} // namespace blob_royale::gameplay
