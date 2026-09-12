#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_SYSTEM_HPP

#include "shared/ability_configuration.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: ability -- the one system that turns an ability pulse into a published effect window.
// @extension-point simulation_system
//
// **One system for every ability, not one system per move.** It now owns **two**: the tap shield,
// which publishes an effect window, and the one-shot charge, which publishes only a cooldown and
// commits its whole effect on the tick it is admitted. A second system would have to re-derive the
// same eligibility from the same world and could not decide a conflict between the two without a
// third place holding the priority, so admission for every ability belongs here. That is also why
// the system is named `ability` and not `shield`.
//
// **The priority between them is: shield wins, and losing the tie costs the charge nothing.** Both
// eligibility answers are computed against the world as it stood at entry, *before either write*,
// and the charge gate is spelled `charge_admissible && !shield_eligible`. Two things follow that a
// "do the shield, then do the charge" ordering would get wrong. An **ineligible** shield pulse --
// one on cooldown, or stale -- provably cannot suppress an eligible charge, because the gate asks
// about eligibility rather than about a `Shield` being present or a pulse having been sent. And a
// charge that loses the tie consumes **no** charge cooldown and writes no `Charge`, because nothing
// in the charge branch ran. Reading the store again after the shield branch had written to it would
// make "a shield is active" indistinguishable from "a shield was activated this tick", and would
// leave the tie working only by accident of `shield_duration_ticks > 0` being enforced in a
// different file (`shared/ability_configuration.hpp`).
//
// It runs **last at `kPreKernel` in all four modes**, which mirrors "`status` runs last at
// `kPostKernel`" and satisfies race's one hard ordering constraint: `course_publisher` writes the
// `RaceModeState` block that the canonical input lock reads (`shared/input_lock.cpp`), so a
// finished racer is locked out of an activation on the very first quantum of a directly seeded
// world. `kPreKernel` is also the last stage before the world freezes, so a Shield written here is
// visible to the same tick's contact responses (`../../simulation/game_simulation.cpp` freezes the
// post-kPreKernel world) -- a pulse protects on the tick it is admitted, not the tick after.
//
// **Running after `thrust_steering` is deliberate, and its one residual is accepted in writing.**
// On the activation tick the thrust limiter has already sized this tick's acceleration against the
// *pre-burst* velocity, so the committed endpoint is `v_pre + burst + a*dt` -- one tick of
// already-certified propulsion stacked on the burst, at most about 1 wu/s at the default
// acceleration. Running this system first would be worse: the limiter's `max(ceiling^2, v.v)` bound
// would then include the burst, letting thrust *sustain* a charged speed indefinitely, which is a
// much larger violation of "above the normal ceiling, controls may brake and turn but must not add
// speed until back within it" (`shared/locomotion.hpp`). From the next tick on that same `max` term
// is what lets a charged body steer without amplifying or braking, which is the intended feel.
//
// Per tick, in this order:
//
//     erase every Shield whose protection AND cooldown have both expired
//     erase every Charge whose cooldown has expired
//     for every entity carrying a Controllable and a PhysicsBody, ascending EntityId:
//         neither a recorded ShieldCommand nor a recorded ChargeCommand -> nothing to decide
//         refuse both unless the match is running
//         refuse both at tick zero
//         refuse both for a static body
//         refuse both while input is locked (active stun, or a completed race course)
//         read protection_active off any existing Shield, before anything is written
//         shield_eligible   = pulse present, generation matches, not protecting, cooldown expired
//         charge_admissible = pulse present, generation matches, not protecting, cooldown expired,
//                             a unit direction obtainable, and the safety envelope admitting the
//                             resulting velocity
//         activate the shield if shield_eligible
//         commit the charge if charge_admissible AND NOT shield_eligible
//
// **The Shield sweep needs both windows expired, not either.** Empty or cancelled protection with a
// live cooldown must survive as a component, because the cooldown is the only thing left that can
// refuse the next pulse: erasing the Shield the moment its protection ended would hand back the
// ability early and turn every stun into a free reset (`shared/status_system.hpp` cancels
// protection and never erases the component for exactly this reason). A `Charge` has only the one
// window, so its sweep asks the one question -- but the admission gate still reads the window
// rather than the component's presence, so a `Charge` in a directly seeded world, or in a world
// whose mode did not declare this system, is judged by the cooldown it actually carries.
//
// **A live shield cooldown does not gate a charge, and a live charge cooldown does not gate a
// shield.** The two cooldowns are separate keys on separate components. What the two abilities do
// share is active *protection*: a body that is currently guarded may not charge out of its own
// guard, which is the `!protection_active` term in the charge gate.
//
// **A refusal changes nothing at all.** No cooldown is consumed, no activation is queued for a
// later tick, no error is thrown and no event is emitted, for either ability. A pulse that arrives
// while the ability is unavailable is simply not an activation, and a queued one would be a second,
// invisible input stream whose commitment tick no client could predict. ADR 0008's "refuse an
// inadmissible activation explicitly, not silently convert it" contrasts refusal with *conversion*
// -- a half-strength charge, a delayed one -- and not with silence.
//
// **What the charge burst does, and what it does not decay under.** It is an instantaneous
// **additive** velocity burst of `charge_speed_fraction * the current normal ceiling` along the
// unit direction, written through `with_velocity`. Additive, so lateral velocity survives; it
// touches no acceleration, position, radius, mass, or collision capability, and it is never a
// teleport and never invulnerability. **Whether it decays at all is a configuration fact, not a
// property of the move**: `drag_per_second=0` is the checked-in value in `config/blob-royale.cfg`
// and in seventeen of the eighteen replay fixtures -- only `royale-drag-decay` authors a nonzero
// one -- and at zero drag the integrator's factor is exactly `1.0`, so there the burst is permanent
// until something else changes the velocity. `deploy/ubuntu-pc/blob-royale.cfg` authors
// `drag_per_second=2.0`, so a deployed burst does decay; development and deployment differ here and
// no comment may assume either. **The safety
// envelope, not drag, is what bounds repeated charges** -- that is the whole reason the envelope is
// a required authored key rather than a representability check. At the default 600 wu/s ceiling a
// body gains 450 wu/s per activation and is refused once the next burst would carry it past 20,000,
// so repeated charges converge on the envelope instead of growing without bound.
//
// **Queue acceptance and a local send are not activation confirmation.** A command accepted by the
// session boundary, ordered into an `InputBatch`, and recorded onto `Controllable` has passed
// transport and value validation and nothing more; every gate above is still ahead of it. The one
// proof that an activation committed is the published `Shield` and its windows, or the published
// `Charge` and its cooldown (`../../simulation/components/shield_component.hpp`,
// `../../simulation/components/charge_component.hpp`). **Neither step adds a per-request negative
// receipt** and neither must be read as promising one: a refused pulse is silent, exactly as every
// other ordinary command refusal in the tree already is.
//
// **`StatusSystem` needs no charge arm, and that is a decision rather than an omission.** A burst
// already in flight keeps flying through a stun, which is precisely ADR 0008's "never restores old
// velocity" and "subsequent external impulses may still move the stunned body": the status pass
// zeroes intent and acceleration and never touches velocity. A *fresh* activation is already
// blocked with no new code, by the canonical input lock plus the generation bump. And a live charge
// cooldown survives a stun for the same reason a shield's cooldown survives cancellation -- being
// stunned costs the effect, never the wait.
//
// The system is stateless apart from its owned configuration copy: every value it reads or writes
// is world-owned, so a tick's result stays a function of the committed world and the tick's
// InputBatch alone.
// related: shared/ability_configuration.hpp -- the tick counts and scalars this reads.
// related: ../../simulation/components/shield_component.hpp -- the value it writes and erases.
// related: ../../simulation/components/charge_component.hpp -- the cooldown a charge publishes.
// related: ../../simulation/commands/shield_command.hpp -- the pulse it admits.
// related: ../../simulation/commands/charge_command.hpp -- the directed pulse it admits.
// related: shared/locomotion.hpp -- `unit_direction`, the normalization a fixed gain requires.
// related: shared/input_lock.hpp -- the canonical lock the admission consults.
// related: shared/status_system.hpp -- the other writer of a Shield; it cancels, never erases.
class AbilitySystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by. It is `ability`
  // rather than `shield` because charge joins the same system rather than founding a new one.
  static constexpr std::string_view kSystemName = "ability";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(AbilityConfiguration configuration);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor, exactly as `ZoneShrinkSystem` does.
  explicit AbilitySystem(AbilityConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // **Never throws for any world a client can reach**, and this is a hard requirement rather than
  // a quality of the current implementation. A throw here escapes `GameSimulation::step`, the
  // runtime worker then records a worker failure and returns, and the simulation thread stops
  // permanently: one client's ability pulse would end the match for everyone in the room. So every
  // gate refuses rather than rejects; the direction is normalized through an optional
  // (`shared/locomotion.hpp`); and the safety envelope is checked in raw doubles *before* any
  // `Vector2` is constructed, because `Vector2` throws on a component past its domain.
  //
  // The two activations it does perform were proven constructible when the configuration was
  // validated: `shared/ability_configuration.hpp` requires the same positivity and ordering that
  // `Shield::activate` and `Charge::activate` enforce, and the strictly positive charge cooldown
  // the latter demands is exactly the rule the shield's zero-cooldown exemption does not transfer
  // to. A SIMULATION.SHIELD_ACTIVATION_INVALID or SIMULATION.CHARGE_ACTIVATION_INVALID from here
  // would therefore mean the configuration and the component disagree, which is a fault worth
  // surfacing, not absorbing.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  // Held **by value**: the engine destroys a mode as soon as it has read the mode's declarations
  // (`../../simulation/game_mode.hpp`), so nothing a system reads may point back at one.
  AbilityConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
