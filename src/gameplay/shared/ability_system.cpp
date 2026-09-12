#include "shared/ability_system.hpp"

#include "command_registry.hpp"
#include "component_join.hpp"
#include "component_store.hpp"
#include "components/charge_component.hpp"
#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/ability_configuration.hpp"
#include "shared/input_lock.hpp"
#include "shared/locomotion.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::gameplay {
namespace {

namespace simulation = blob_royale::simulation;

// The shield pulse this tick recorded for one entity, or nullptr when it recorded none. A recorded
// list holds at most one command of each kind (`components/controllable_component.hpp`), so the
// first match is the only one and the list is read in the order phase 0 wrote it rather than
// sorted -- this is `recorded_thrust_of`'s rule and this system may not invent a second one.
[[nodiscard]] const simulation::ShieldCommand*
recorded_shield_of(const simulation::Controllable& controllable) noexcept {
  for (const simulation::Command& command : controllable.commands_this_tick) {
    if (const auto* pulse = std::get_if<simulation::ShieldCommand>(&command); pulse != nullptr) {
      return pulse;
    }
  }
  return nullptr;
}

// The charge pulse this tick recorded for one entity, under the same rule as the shield above: the
// first match is the only one, and the list is read in the order phase 0 wrote it.
[[nodiscard]] const simulation::ChargeCommand*
recorded_charge_of(const simulation::Controllable& controllable) noexcept {
  for (const simulation::Command& command : controllable.commands_this_tick) {
    if (const auto* pulse = std::get_if<simulation::ChargeCommand>(&command); pulse != nullptr) {
      return pulse;
    }
  }
  return nullptr;
}

// The velocity a charge would commit, or nullopt when the activation is refused. Both refusals are
// silent no-ops at the call site: no cooldown, no component, no event, no error.
//
// **Everything here is raw doubles until the envelope has admitted the result, and that is not a
// style preference.** `Vector2::operator+` and `Vector2::create` throw on a component past
// `1e12`; such a throw escapes `AbilitySystem::apply` and `GameSimulation::step`, the runtime
// worker then records a worker failure and returns, and the simulation thread stops permanently --
// the room is dead. One client's charge must not be able to end a match, so the sum is formed in
// doubles, required finite, and required to be inside the envelope *before* any `Vector2` exists to
// throw. A magnitude bound at or below the component domain keeps both components representable,
// which is why one magnitude test covers both concerns and no per-component test is needed
// (`shared/ability_configuration.hpp` refuses an envelope above that domain at startup).
//
// The magnitude is the written-out `sqrt(x*x + y*y)` this codebase uses everywhere. An intermediate
// square that overflows to infinity is not a special case: infinity fails the comparison and the
// activation is refused, which is the same answer the exact arithmetic would give.
[[nodiscard]] std::optional<simulation::Vector2>
charged_velocity(const simulation::Vector2& velocity, const simulation::Vector2& direction,
                 const double burst_speed, const double safety_envelope_speed) {
  const std::optional<simulation::Vector2> unit = unit_direction(direction);
  if (!unit.has_value()) {
    return std::nullopt;
  }
  // Additive, never replacing: lateral velocity survives a charge, so a body crossing the arena
  // sideways is deflected by the burst rather than snapped onto it.
  const double next_x = velocity.x() + (unit->x() * burst_speed);
  const double next_y = velocity.y() + (unit->y() * burst_speed);
  if (!std::isfinite(next_x) || !std::isfinite(next_y)) {
    return std::nullopt;
  }
  if (std::sqrt((next_x * next_x) + (next_y * next_y)) > safety_envelope_speed) {
    return std::nullopt;
  }
  return simulation::Vector2::create(next_x, next_y);
}

} // namespace

std::unique_ptr<const simulation::SimulationSystem>
AbilitySystem::create(AbilityConfiguration configuration) {
  return std::make_unique<const AbilitySystem>(std::move(configuration));
}

AbilitySystem::AbilitySystem(AbilityConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

void AbilitySystem::apply(simulation::GameWorld& world,
                          const simulation::TickContext& context) const {
  const auto tick = context.tick_sequence();

  // The shield expiry sweep. Both windows must have expired: a Shield whose protection ended -- or
  // was cancelled to nothing by a stun -- but whose cooldown is still running has to stay, because
  // the cooldown is what refuses the next pulse. The ids are collected first and erased afterwards
  // because `erase` rewrites the store the walk is reading.
  std::vector<simulation::EntityId> expired_shields;
  for (const auto& entry : world.store<simulation::Shield>().entries()) {
    if (entry.value.shield_window().expired(tick) && entry.value.cooldown_window().expired(tick)) {
      expired_shields.push_back(entry.entity);
    }
  }
  for (const auto entity : expired_shields) {
    world.mutable_store<simulation::Shield>().erase(entity);
  }

  // The charge expiry sweep, a second pass over a second store for the same reason and by the same
  // collect-then-erase rule. A `Charge` carries one window and nothing else -- the burst was
  // committed to the body's velocity on the activation tick and there is no active effect left to
  // outlive -- so an expired cooldown makes the whole component dead weight.
  std::vector<simulation::EntityId> expired_charges;
  for (const auto& entry : world.store<simulation::Charge>().entries()) {
    if (entry.value.cooldown_window().expired(tick)) {
      expired_charges.push_back(entry.entity);
    }
  }
  for (const auto entity : expired_charges) {
    world.mutable_store<simulation::Charge>().erase(entity);
  }

  // The canonical two-store join, walked **once** for both abilities: an entity that can activate
  // an ability is one carrying both a controller link and a body, which is the same question
  // `thrust_steering` asks (`component_join.hpp`). It visits in ascending EntityId order, which is
  // the order every phase owes ADR 0003, and it skips a bodyless entity rather than visiting it
  // with a missing half -- that skip *is* the "no body, no activation" rule, written once by the
  // join rather than as a second null check here. One join is also what makes the conflict rule
  // decidable: both pulses for one entity are in hand at the same point.
  simulation::for_each_entity_with_both(
      world.store<simulation::Controllable>(), world.store<simulation::PhysicsBody>(),
      [this, &world, tick](const simulation::EntityId entity,
                           const simulation::Controllable& controllable,
                           const simulation::PhysicsBody& body) {
        const simulation::ShieldCommand* shield_pulse = recorded_shield_of(controllable);
        const simulation::ChargeCommand* charge_pulse = recorded_charge_of(controllable);
        if (shield_pulse == nullptr && charge_pulse == nullptr) {
          return;
        }
        // The four gates common to both abilities, evaluated exactly once against the world at
        // entry. They are common because none of them is about an ability: they are about whether
        // this entity may act at all on this tick.
        //
        // Abilities are a running-match mechanic. A pulse in `lobby`, `countdown` or `ended` is
        // refused rather than held: the match machine is engine-owned and a queued activation
        // would commit at a tick no client chose (`../../simulation/match_phase.hpp`).
        if (world.match().phase != simulation::MatchPhase::kRunning) {
          return;
        }
        // Tick zero is the loaded initial state, which no activation may claim: both components
        // date their windows from a positive activation tick and reject a zero one outright.
        // Refusing here keeps a first-tick pulse a silent refusal rather than a thrown tick.
        if (tick == simulation::TickSequence::zero()) {
          return;
        }
        // A static body is scenery. It cannot be guarded -- the guarded composition throws
        // SIMULATION.GUARDED_PAIR_FACTS_INVALID for a guarded static subject -- and it cannot be
        // launched, because a static body's velocity is not what moves it.
        if (body.is_static()) {
          return;
        }
        // The canonical lock, shared with steering: an active stun, or retained RaceProgress that
        // has completed the published course (`shared/input_lock.hpp`). A stunned entity cannot
        // start a guard or a charge, and a finished racer's held input cannot act on the world.
        if (input_is_locked(world, entity, tick)) {
          return;
        }

        // **Read the world's answers before writing any of them.** `protection_active` in
        // particular is read here, before either branch below can write, which is what makes
        // "evaluate eligibility first" real rather than nominal: re-reading the Shield store after
        // the shield branch had written to it would make "a shield is active" indistinguishable
        // from "a shield was activated this tick", and the tie would then resolve correctly only
        // by accident of `shield_duration_ticks > 0` being enforced in a different file.
        const auto* shield = world.store<simulation::Shield>().find(entity);
        const bool protection_active = shield != nullptr && shield->shield_window().contains(tick);
        const bool shield_cooling = shield != nullptr && !shield->cooldown_window().expired(tick);
        // The window rather than the component's presence, even though the sweep above has already
        // erased every expired one: the sweep and the gate must each be correct on their own, so a
        // `Charge` in a directly seeded world or one whose mode never declared this system is still
        // judged by the cooldown it actually carries.
        const auto* charge = world.store<simulation::Charge>().find(entity);
        const bool charge_cooling = charge != nullptr && !charge->cooldown_window().expired(tick);

        // Both eligibility answers, computed before either write.
        //
        // Exact optional equality is the `thrust_steering_system.cpp` idiom and both pulses use it:
        // absence matches absence, which is the initial generation, and a pulse stamped before a
        // missed stun invalidated input cannot activate. That is the whole stale-input rule and it
        // is not weakened for a pulse just because a pulse carries no direction.
        //
        // Active protection refuses both: a re-tap that would restart the perfect opening, and a
        // charge out of the body's own live guard. The two *cooldowns* are separate keys on
        // separate components and neither gates the other ability.
        const bool shield_eligible =
            shield_pulse != nullptr &&
            shield_pulse->input_generation == controllable.input_generation && !protection_active &&
            !shield_cooling;
        const bool charge_pulse_admitted =
            charge_pulse != nullptr &&
            charge_pulse->input_generation == controllable.input_generation && !protection_active &&
            !charge_cooling;
        // The last two terms of `charge_admissible` -- a unit direction obtainable, and the safety
        // envelope admitting the result -- are exactly "a velocity came back". `body.velocity()` is
        // read here, before any write: the join hands out a reference into the `PhysicsBody` store,
        // and although `insert_or_assign` on an id the store already holds assigns in place --
        // which is what keeps the forward walk valid, exactly as the steering system records --
        // that assignment mutates the referent.
        //
        // The gain is `charge_speed_fraction` times the **current** normal ceiling read from match
        // state, not an authored speed: ADR 0008 says "0.75 times the current normal movement
        // ceiling" and that ceiling is live-tunable, so a room that retunes its movement retunes
        // its charge with it (`../../simulation/movement_tuning_state.hpp`).
        const std::optional<simulation::Vector2> committed_velocity =
            charge_pulse_admitted
                ? charged_velocity(body.velocity(), charge_pulse->direction,
                                   configuration_.charge_speed_fraction() *
                                       world.match().movement.current.normal_top_speed(),
                                   configuration_.charge_safety_envelope_speed())
                : std::optional<simulation::Vector2>{};
        const bool charge_admissible = committed_velocity.has_value();

        if (shield_eligible) {
          // The activation captures every duration from the configuration, so the frozen defender a
          // contact response reads later supplies the effect it actually owns rather than reaching
          // for a configuration a noncapturing response cannot hold.
          world.mutable_store<simulation::Shield>().insert_or_assign(
              entity, simulation::Shield::activate(tick, configuration_.shield_duration_ticks(),
                                                   configuration_.shield_perfect_window_ticks(),
                                                   configuration_.shield_cooldown_ticks(),
                                                   configuration_.parry_stun_duration_ticks()));
        }
        // The conflict gate, spelled `!shield_eligible` and not "no Shield present". An
        // **ineligible** shield pulse therefore cannot suppress an eligible charge, and a charge
        // that loses the tie consumes no cooldown and writes no `Charge`, because nothing in this
        // branch ran.
        if (charge_admissible && !shield_eligible) {
          world.mutable_store<simulation::Charge>().insert_or_assign(
              entity, simulation::Charge::activate(tick, configuration_.charge_cooldown_ticks()));
          // `insert_or_assign` on an id the store already holds assigns in place: it neither
          // inserts nor moves an entry, so the join's forward walk over the same store stays valid
          // and the ascending order is untouched.
          world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
              entity, body.with_velocity(*committed_velocity));
        }
      });
}

} // namespace blob_royale::gameplay
