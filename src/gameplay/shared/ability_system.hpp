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
// **One system for every ability, not one system per move.** This step gives it exactly one
// mechanic -- the tap shield -- and Step 19 extends *this* owner with charge, its cooldown, and the
// conflict rule that resolves a shield and a charge eligible in the same tick (the plan's Step 19
// block: "if shield and charge are both eligible in one tick, shield wins and charge cooldown is
// not consumed"). A second system would have to re-derive the same eligibility from the same world
// and could not decide a conflict between the two without a third place holding the priority, so
// admission for every ability belongs here.
//
// It runs **last at `kPreKernel` in all four modes**, which mirrors "`status` runs last at
// `kPostKernel`" and satisfies race's one hard ordering constraint: `course_publisher` writes the
// `RaceModeState` block that the canonical input lock reads (`shared/input_lock.cpp`), so a
// finished racer is locked out of an activation on the very first quantum of a directly seeded
// world. `kPreKernel` is also the last stage before the world freezes, so a Shield written here is
// visible to the same tick's contact responses (`../../simulation/game_simulation.cpp` freezes the
// post-kPreKernel world) -- a pulse protects on the tick it is admitted, not the tick after.
//
// Per tick, in this order:
//
//     erase every Shield whose protection AND cooldown have both expired
//     for every entity carrying a Controllable and a PhysicsBody, ascending EntityId:
//         no recorded ShieldCommand -> nothing to decide
//         refuse unless the match is running
//         refuse at tick zero
//         refuse a static body
//         refuse while input is locked (active stun, or a completed race course)
//         refuse unless the pulse's generation exactly equals the entity's
//         refuse while an existing Shield still protects
//         refuse while an existing Shield's cooldown has not expired
//         otherwise activate, capturing every duration from the configuration
//
// **The sweep needs both windows expired, not either.** Empty or cancelled protection with a live
// cooldown must survive as a component, because the cooldown is the only thing left that can refuse
// the next pulse: erasing the Shield the moment its protection ended would hand back the ability
// early and turn every stun into a free reset (`shared/status_system.hpp` cancels protection and
// never erases the component for exactly this reason).
//
// **A refusal changes nothing at all.** No cooldown is consumed, no activation is queued for a
// later tick, no error is thrown and no event is emitted. A pulse that arrives while the ability is
// unavailable is simply not an activation, and a queued one would be a second, invisible input
// stream whose commitment tick no client could predict.
//
// **Queue acceptance and a local send are not activation confirmation.** A command accepted by the
// session boundary, ordered into an `InputBatch`, and recorded onto `Controllable` has passed
// transport and value validation and nothing more; every gate above is still ahead of it. The one
// proof that an activation committed is the published `Shield` and its windows
// (`../../simulation/components/shield_component.hpp`). **This step adds no per-request negative
// receipt** and must not be read as promising one: a refused pulse is silent, exactly as every
// other ordinary command refusal in the tree already is.
//
// The system is stateless apart from its owned configuration copy: every value it reads or writes
// is world-owned, so a tick's result stays a function of the committed world and the tick's
// InputBatch alone.
// related: shared/ability_configuration.hpp -- the tick counts this captures on activation.
// related: ../../simulation/components/shield_component.hpp -- the value it writes and erases.
// related: ../../simulation/commands/shield_command.hpp -- the pulse it admits.
// related: shared/input_lock.hpp -- the canonical lock the admission consults.
// related: shared/status_system.hpp -- the other writer of a Shield; it cancels, never erases.
class AbilitySystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by. It is `ability`
  // rather than `shield` because Step 19's charge joins the same system rather than a new one.
  static constexpr std::string_view kSystemName = "ability";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(AbilityConfiguration configuration);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor, exactly as `ZoneShrinkSystem` does.
  explicit AbilitySystem(AbilityConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Never throws for any world it can reach: every gate above refuses rather than rejects, and the
  // activation it does perform was proven constructible when the configuration was validated
  // (`shared/ability_configuration.hpp` requires the same positivity and ordering that
  // `Shield::activate` enforces). A SIMULATION.SHIELD_ACTIVATION_INVALID from here would mean the
  // configuration and the component disagree, which is a fault worth surfacing, not absorbing.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  // Held **by value**: the engine destroys a mode as soon as it has read the mode's declarations
  // (`../../simulation/game_mode.hpp`), so nothing a system reads may point back at one.
  AbilityConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
