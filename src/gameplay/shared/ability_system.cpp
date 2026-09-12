#include "shared/ability_system.hpp"

#include "command_registry.hpp"
#include "component_join.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "shared/ability_configuration.hpp"
#include "shared/input_lock.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"

#include <memory>
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

  // The expiry sweep. Both windows must have expired: a Shield whose protection ended -- or was
  // cancelled to nothing by a stun -- but whose cooldown is still running has to stay, because the
  // cooldown is what refuses the next pulse. The ids are collected first and erased afterwards
  // because `erase` rewrites the store the walk is reading.
  std::vector<simulation::EntityId> expired;
  for (const auto& entry : world.store<simulation::Shield>().entries()) {
    if (entry.value.shield_window().expired(tick) && entry.value.cooldown_window().expired(tick)) {
      expired.push_back(entry.entity);
    }
  }
  for (const auto entity : expired) {
    world.mutable_store<simulation::Shield>().erase(entity);
  }

  // The canonical two-store join: an entity that can activate an ability is one carrying both a
  // controller link and a body, which is the same question `thrust_steering` asks
  // (`component_join.hpp`). It visits in ascending EntityId order, which is the order every phase
  // owes ADR 0003, and it skips a bodyless entity rather than visiting it with a missing half --
  // that skip *is* the "no body, no activation" rule, written once by the join rather than as a
  // second null check here.
  simulation::for_each_entity_with_both(
      world.store<simulation::Controllable>(), world.store<simulation::PhysicsBody>(),
      [this, &world, tick](const simulation::EntityId entity,
                           const simulation::Controllable& controllable,
                           const simulation::PhysicsBody& body) {
        const simulation::ShieldCommand* pulse = recorded_shield_of(controllable);
        if (pulse == nullptr) {
          return;
        }
        // Abilities are a running-match mechanic. A pulse in `lobby`, `countdown` or `ended` is
        // refused rather than held: the match machine is engine-owned and a queued activation
        // would commit at a tick no client chose (`../../simulation/match_phase.hpp`).
        if (world.match().phase != simulation::MatchPhase::kRunning) {
          return;
        }
        // Tick zero is the loaded initial state, which no activation may claim: the three windows
        // share a positive activation tick, and `Shield::activate` rejects a zero one outright.
        // Refusing here keeps a first-tick pulse a silent refusal rather than a thrown tick.
        if (tick == simulation::TickSequence::zero()) {
          return;
        }
        // A static body is scenery. It cannot be guarded, and the guarded composition throws
        // SIMULATION.GUARDED_PAIR_FACTS_INVALID for a guarded static subject, so admitting one
        // here would fail a later tick rather than this pulse.
        if (body.is_static()) {
          return;
        }
        // The canonical lock, shared with steering: an active stun, or retained RaceProgress that
        // has completed the published course (`shared/input_lock.hpp`). A stunned entity cannot
        // start a guard, and a finished racer's held input cannot act on the world.
        if (input_is_locked(world, entity, tick)) {
          return;
        }
        // Exact optional equality, the `thrust_steering_system.cpp` idiom: absence matches absence,
        // which is the initial generation, and a pulse stamped before a missed stun invalidated
        // input cannot activate. This is the whole stale-input rule and it is not weakened for a
        // pulse just because a pulse carries no direction.
        if (pulse->input_generation != controllable.input_generation) {
          return;
        }
        // Both halves of availability, read off the one component that holds them. Already
        // protecting refuses a re-tap that would restart the perfect opening; a cooldown that has
        // not expired refuses the next tap even though the protection it started with is over.
        if (const auto* shield = world.store<simulation::Shield>().find(entity);
            shield != nullptr &&
            (shield->shield_window().contains(tick) || !shield->cooldown_window().expired(tick))) {
          return;
        }
        // The activation captures every duration from the configuration, so the frozen defender a
        // contact response reads later supplies the effect it actually owns rather than reaching
        // for a configuration a noncapturing response cannot hold.
        world.mutable_store<simulation::Shield>().insert_or_assign(
            entity, simulation::Shield::activate(tick, configuration_.shield_duration_ticks(),
                                                 configuration_.shield_perfect_window_ticks(),
                                                 configuration_.shield_cooldown_ticks(),
                                                 configuration_.parry_stun_duration_ticks()));
      });
}

} // namespace blob_royale::gameplay
