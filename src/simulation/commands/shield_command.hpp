#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_SHIELD_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_SHIELD_COMMAND_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"

#include <optional>

namespace blob_royale::simulation {

// canonical: shield_command -- one entity's request to raise its shield on one tick.
//
// **A pulse, not a setting.** It names no direction, no duration, and no strength: the mode's
// `AbilityConfiguration` owns every one of those, and the command carries only "this entity asked,
// with this activation token". That is the same division `ThrustCommand` draws between intent and
// effect -- a thrust names a direction and the mode supplies the acceleration -- and it is what
// keeps a client unable to author its own protection length by sending a longer number.
//
// **It carries no actor identity.** The entity is the addressed identity and the boundary stamps
// it from the session's own body (`../../server/session_websocket_session.cpp`), so a client
// cannot raise another player's shield by naming their entity. `addressed_identity_of` therefore
// needs no arm for this kind: it reads `value.entity` like every other entity-addressed kind
// (`../command_registry.hpp`).
//
// **One pulse per entity per tick reaches the world.** The mailbox supersedes by (kind, entity)
// and `InputBatch::create` keeps the last command of a kind for each identity, so a held button is
// one decision per tick rather than a burst the tick would have to count. An ability that needed
// to distinguish two presses inside one tick could not be expressed here, which is deliberate: the
// tick is the smallest unit of decision this simulation has.
// related: command_registry.hpp -- the closed list of command kinds.
// related: input_batch.hpp -- where a present zero generation is rejected.
// related: ../../gameplay/shared/ability_system.hpp -- the system that admits or refuses a pulse.
struct ShieldCommand final {
  EntityId entity;
  // Exact activation token, including absence, with ThrustCommand's semantics: absence is the
  // initial generation of an entity whose input has never been invalidated, a present zero is
  // rejected by InputBatch, and a generation that does not match the entity's current one cannot
  // activate. The match is exact optional equality rather than an ordering comparison, so a pulse
  // pressed before a stun landed is refused rather than queued
  // (`../../gameplay/shared/thrust_steering_system.cpp`).
  std::optional<TickSequence> input_generation{};

  friend bool operator==(const ShieldCommand&, const ShieldCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
