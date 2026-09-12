#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_CHARGE_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_CHARGE_COMMAND_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <optional>

namespace blob_royale::simulation {

// canonical: charge_command -- one entity's request to spend its charge in one direction on one
// tick.
//
// **A direction, and only a direction.** Unlike `ShieldCommand`, which is a bare pulse, a charge
// has to say which way; unlike `ThrustCommand`, whose magnitude is a live analog throttle, a charge
// names no strength at all. The mode's `AbilityConfiguration` owns the gain, the cooldown and the
// safety envelope, so the client contributes a heading and the server contributes the physics.
//
// **The magnitude of `direction` is not the strength of the burst, and that is load-bearing.** Each
// component carries the same `[-1, 1]` unit-interval bound `ThrustCommand`'s does and is validated
// against it in `InputBatch::create`, but the ability system normalizes the vector through
// `gameplay::unit_direction` rather than clamping it through `gameplay::normalized_thrust_intent`.
// The clamp leaves a subunit vector at its own magnitude -- correct for an analog throttle -- so
// routing a charge through it would turn `{"x":0.5,"y":0}` into half a burst and make pointer
// distance into strength, which is exactly the authorship the fixed authoritative gain exists to
// deny (`docs/reviews/2026-09-12-charge-contract.md` § "Direction"). A direction that cannot be
// normalized -- zero, or subnormal enough that its squared magnitude underflows -- is a silent
// refusal in that system, never an error here: the value is well-formed, the world simply admits
// nothing for it.
//
// **It carries no actor identity.** The entity is the addressed identity and the boundary stamps it
// from the session's own body (`../../server/session_websocket_session.cpp`), so a client cannot
// charge another player's body by naming their entity. `addressed_identity_of` therefore needs no
// arm for this kind: it reads `value.entity` like every other entity-addressed kind
// (`../command_registry.hpp`).
//
// **One charge per entity per tick reaches the world.** The mailbox supersedes by (kind, entity)
// and `InputBatch::create` keeps the last command of a kind for each identity, so a held button is
// one decision per tick and the last heading submitted is the one that fires. Two presses inside
// one tick could not be expressed here, which is deliberate: the tick is the smallest unit of
// decision this simulation has, and a one-shot ability with a cooldown could not have honoured the
// second one anyway.
// related: command_registry.hpp -- the closed list of command kinds.
// related: input_batch.hpp -- where the component range and a present zero generation are rejected.
// related: shield_command.hpp -- the other ability command, and the pulse this one is not.
// related: ../../gameplay/shared/ability_system.hpp -- the system that admits or refuses a charge.
struct ChargeCommand final {
  EntityId entity;
  // Carried verbatim, not normalized here, for `ThrustCommand`'s reason: normalizing at
  // construction and again in the system would scale twice and is not bit-identical to scaling
  // once, and the written operation order of the one normalization is the contract
  // (`docs/architecture/0003-deterministic-simulation-contract.md` § "Floating-point contract").
  Vector2 direction;
  // Exact activation token, including absence, with ThrustCommand's semantics: absence is the
  // initial generation of an entity whose input has never been invalidated, a present zero is
  // rejected by InputBatch, and a generation that does not match the entity's current one cannot
  // activate. The match is exact optional equality rather than an ordering comparison, so a charge
  // pressed before a stun landed is refused rather than queued
  // (`../../gameplay/shared/thrust_steering_system.cpp`).
  std::optional<TickSequence> input_generation{};

  friend bool operator==(const ChargeCommand&, const ChargeCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
