#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLER_HPP

#include "command_registry.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "observation.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace blob_royale::controllers {

// canonical: controller -- one deciding agent for one entity, in-process form.
// @extension-point controller
//
// **Controller is a role, and this class is only one of the three things that fill it**
// (`docs/architecture/0004-gameplay-architecture.md` § "Controllers"). A networked player's session
// in `blob_server` fills the same role without deriving from this and without `blob_server`
// depending on `blob_controllers` at all, because what the role actually requires is two
// capabilities and a command vocabulary, not an interface: `CommandSink&` to write,
// `const SnapshotPublication&` to read, and the registered `Command` kinds. A bot holds **exactly
// those two capabilities and nothing else**, which is what makes the simulation unable to tell a
// human from a bot: all three sources arrive at the world as indistinguishable `Command` values in
// one `InputBatch`, and the tick stores `Controllable::controller_id` and never branches on it.
//
// **What a new bot writes.** `kind()` and `decide_from_observation`. Everything else -- the durable
// identity, the body it is currently driving, and the observation check -- is answered once here,
// so three controllers do not carry three copies of the same two accessors.
//
// **`decide` is deliberately not `const`.** A controller owns behavior state: a seeded generator, a
// personality's held heading, a replay cursor, a pending asynchronous result. That state is
// precisely why a controller lives outside the tick, where nondeterminism is free
// (`docs/architecture/0002-simulation-architecture.md` § "Consequences": replay replays the
// recorded command log, not the controller).
//
// **`decide` must return without blocking.** A host runs every controller in one pass at
// presentation cadence; an asynchronous controller returns the latest decision it has rather than
// waiting for a new one. Nothing enforces this in the type system, so it is stated here and the
// host contains the damage: a controller that throws is isolated and counted, never allowed to end
// the pass (`controller_host.hpp`).
//
// **`entity()` is a `std::optional`, and that is the second deviation from the ADR's sketch.** ADR
// 0004 writes `EntityId entity()`, and it was written before plan Step 22 settled that a command
// source's durable handle is a `ControllerId`: a controller is constructed before it has ever been
// seated, so there is no `EntityId` for a total accessor to return, and the same ADR states that "a
// controller with no live entity" is a defined case. It is maintained by `decide` from each
// observation rather than stored by an implementation, so it can never disagree with the world the
// controller last saw.
//
// Adding a bot:
//
//   new  src/controllers/<name>_controller.{hpp,cpp}   the behavior and its personality struct
//   edit src/controllers/controller_registry.hpp       one include and one row
//   edit match configuration                           `[match] bots=`
//   do not touch                                       blob_simulation, blob_runtime,
//                                                      blob_gameplay, blob_server, any other bot
//
// Three implementations of this role, which is the evidence ADR 0002 § "OOP policy" accepts an
// interface on: `WandererController` and `ChaserController` here, and a networked session in
// `blob_server`; `ScriptedReplayController` is the fixtures' fourth.
// related: observation.hpp -- the only thing a controller may see.
// related: controller_host.hpp -- what runs these and submits what they decide.
// related: controller_registry.hpp -- the one map from a kind name to a factory.
// related: ../runtime/command_sink.hpp -- the whole write capability, shared with a network
// session.
class Controller {
public:
  virtual ~Controller() = default;

  Controller(const Controller&) = delete;
  Controller(Controller&&) = delete;
  Controller& operator=(const Controller&) = delete;
  Controller& operator=(Controller&&) = delete;

  // The registered behavior name: "wanderer", "chaser", "scripted_replay". It is the
  // `controller_registry.hpp` key, the `[match] bots=` key, and the `controller_kind` the wire
  // publishes through `ControllerDirectory`, so all three are one string and not three.
  [[nodiscard]] virtual std::string_view kind() const noexcept = 0;

  // The durable identity of this deciding agent: the value `CommandSink::open_session` issued.
  // Non-virtual and stored once, because it is the same answer for every implementation and it is
  // also the host's decision order (`controller_host.hpp`).
  [[nodiscard]] simulation::ControllerId controller() const noexcept { return controller_; }

  // The body this controller was driving at its most recent decision, or `std::nullopt` when it was
  // driving none. `std::nullopt` before the first `decide`, because a controller is constructed
  // before the engine has chosen it a body.
  [[nodiscard]] std::optional<simulation::EntityId> entity() const noexcept { return entity_; }

  // The tick this controller last asked for a body at, or `std::nullopt` when it has never asked or
  // has been seated since. Published so a diagnostic can tell "waiting to be seated" apart from
  // "deciding nothing", which look identical from outside.
  [[nodiscard]] std::optional<simulation::TickSequence> last_spawn_request_tick() const noexcept {
    return last_spawn_request_tick_;
  }

  // One decision against one observed world. Records which body the observation resolved and then
  // runs the behavior.
  //
  // Throws ControllersValidationError with `CONTROLLERS.OBSERVATION_CONTROLLER_MISMATCH` when the
  // observation was built for a different controller. That is a capability check and not a
  // formality: an observation carries one controller's resolved body, so deciding from a foreign
  // one would let a bot act on an identity it was never issued. The host never builds a mismatched
  // observation, so this can only fire for a direct caller.
  [[nodiscard]] std::vector<simulation::Command> decide(const Observation& observation);

protected:
  explicit Controller(simulation::ControllerId controller) noexcept;

  // Default preserves every legacy pass, including repeated snapshots. A stateful reader may
  // reject an observation before identity/retry mutation; controller identity was already checked.
  [[nodiscard]] virtual bool accepts_observation(const Observation&) const noexcept { return true; }

  // canonical: controller_spawn_request -- the one way a controller asks for a body, and the one
  // rule for when it may ask again.
  //
  // Returns a single `SpawnCommand` naming this controller's own identity -- the identical command
  // a networked session sends after `welcome`, with the engine choosing the `EntityId` from the
  // tick's reservation and the mode's `SpawnPolicy` choosing the seat
  // (`src/simulation/commands/spawn_command.hpp`) -- or an empty list when a previous request is
  // still within `kSpawnRequestRetryTicks` committed ticks of this observation.
  //
  // **The interval is the whole point of this being a shared function rather than one inline line
  // per bot.** Asking again every pass would give one controller two bodies, because a request is
  // in flight for at least one tick; never asking again would strand a bot whose spawn was refused.
  // `controllers_limits.hpp` states both, and records that the rule which would make the second
  // body *impossible* belongs to kernel phase 0 or a mode's `SpawnPolicy` rather than to a
  // controller.
  //
  // A controller that has been seated since its last request forgets it, so an elimination is
  // followed by an immediate request rather than by a wait.
  [[nodiscard]] std::vector<simulation::Command> request_body(const Observation& observation);

  // canonical: controller_thrust_request -- active bots author from this public observation.
  // Returns zero or one command for an owned dynamic body, suppressing an observed active stun.
  // Copies direction and the observed optional generation verbatim; no timing, RNG, or retry.
  [[nodiscard]] std::vector<simulation::Command>
  request_thrust(const Observation& observation, const simulation::Vector2& direction) const;

  // canonical: controller_ability_request -- the two combat pulses, on request_thrust's terms.
  //
  // **Siblings of `request_thrust`, never raw commands a behavior appends beside it.** All three
  // share one suppression -- a foreign observation, no seated entity, a missing or `is_static()`
  // `PhysicsBody`, an observed `Stun` whose window contains the observed tick, and a `Controllable`
  // whose `controller_id` is this controller's -- and each stamps `input_generation` from that same
  // observed `Controllable`. A behavior that built an ability command itself would emit one on
  // exactly the pass where its thrust was correctly suppressed: the stun pass, which is the one
  // pass an ability is most tempting and least admissible.
  //
  // **The generation is copied, never constructed.** Absence is a real value -- an entity whose
  // input has never been invalidated -- while a *present zero* is a hard tick failure
  // `InputBatch::create` refuses before any system sees it, so the only safe source is the one the
  // world published. The payload asymmetry the two commands carry is theirs and not these helpers':
  // a shield is a bare pulse and a charge carries a direction, which is why only one of these takes
  // one, and on the wire the asymmetry runs the other way round -- `shield`'s single generation is
  // required-and-nullable because a pulse carrying nothing else could not tell "I mean the initial
  // generation" from "I forgot the field", while `charge`'s is optional beside its `x` and `y`
  // exactly as a thrust's is (`../protocol/command_decoding.cpp`). Nothing here touches the wire.
  //
  // **They deliberately do not check match-running, tick zero, or a completed race course**, which
  // is the omission `request_thrust` already makes and `AbilitySystem` already covers -- it refuses
  // all three, and a refusal consumes no cooldown, queues nothing, throws nothing and emits no
  // event. Duplicating the three gates here would give one rule two homes, and the copy in this
  // library could not see the mode's input lock at all. The choice is written down rather than left
  // as an oversight because it is not free: a bot that keeps pulsing a shield after finishing a
  // race burns nothing and logs nothing, while a reader of mailbox refusal statistics sees a fault.
  // That cost is accepted here, in the open.
  // related: ../simulation/commands/shield_command.hpp -- the pulse, and why it names nothing else.
  // related: ../simulation/commands/charge_command.hpp -- the direction, and why it names no gain.
  // related: ../gameplay/shared/ability_system.hpp -- the three gates this deliberately omits.
  [[nodiscard]] std::vector<simulation::Command>
  request_shield(const Observation& observation) const;
  [[nodiscard]] std::vector<simulation::Command>
  request_charge(const Observation& observation, const simulation::Vector2& direction) const;

private:
  // The behavior half, and the one function a new bot writes. It is called with an observation
  // already verified to be this controller's own, and it never has to record its own entity.
  [[nodiscard]] virtual std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) = 0;

  simulation::ControllerId controller_;
  std::optional<simulation::EntityId> entity_;
  std::optional<simulation::TickSequence> last_spawn_request_tick_;
};

} // namespace blob_royale::controllers

#endif
