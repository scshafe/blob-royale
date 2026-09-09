#ifndef BLOB_ROYALE_RUNTIME_COMMAND_SINK_HPP
#define BLOB_ROYALE_RUNTIME_COMMAND_SINK_HPP

#include "command_mailbox.hpp"
#include "command_registry.hpp"
#include "command_submission_result.hpp"
#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "entity_id_allocator.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace blob_royale::runtime {

// canonical: command_sink -- the whole capability a command source is given.
//
// **Three operations, and nothing else.** A networked player's session, an in-process bot, and a
// scripted replay controller all hold exactly this object plus `const SnapshotPublication&`, which
// is what makes a bot act through precisely the path a human acts through and the simulation unable
// to tell them apart (`docs/architecture/0004-gameplay-architecture.md` § "Controllers"). It is a
// capability, not ambient authority: it grants no world read, no lifecycle transition, and no
// reference to `GameSimulation` or `SimulationRuntime`
// (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle").
//
// **`open_session` returns a `ControllerId`, not an `EntityId`.** Plan Step 22's note sketches
// `open_session() -> EntityId`; the ADRs it defers to make that impossible and wrong, so the ADRs
// win and this records why. It is *impossible* because the engine, not the command source, chooses
// an `EntityId`: `SpawnCommand` carries only a `ControllerId`, and the id is drawn inside `step`
// from the tick's `EntityIdReservation` (`src/simulation/commands/spawn_command.hpp`), so a value
// returned outside a tick could not be one. It is *wrong* because a controller outlives the
// entities it drives (ADR 0004 § "Controllers"): royale destroys a player's entity on elimination
// and again on the lobby wipe, so a session holding an `EntityId` would hold a dead identity for
// most of a match, while the `ControllerId` it holds instead is what carries score attribution and
// a respawned body under one durable name. Protocol v2's `welcome` carries both, and the
// `EntityId` half of it is read from the snapshot once a spawn has been seated.
//
// **What the sink can enforce, and what it deliberately cannot.** It knows which `ControllerId` it
// issued, so it refuses a submission from a closed session and a spawn stamped with a foreign
// controller. It holds no world state, so it cannot know which `EntityId` a controller currently
// drives; that ownership stamp is the command source's own (a session sends only its own entity id,
// from `welcome`) and a forged one is ignored by the tick, because a command naming an entity that
// is not live is one of the world-state disagreements ADR 0003 § "Accepted simulation input"
// ignores rather than fails on.
//
// **The sink refuses everything `InputBatch::create` would reject.** The accepted-kind check is the
// one ADR 0004 § "Commands" names; the thrust-range and unissued-id checks are the same discipline
// applied to values, and they exist because the alternative is a hard tick failure a single client
// could trigger. The direction is deliberate and one-way: the boundary refuses, and the engine
// still asserts. A rejection surviving to `InputBatch::create` means the boundary and the engine
// disagree about the running mode, which stays a hard failure.
//
// **Thread-safety contract.** Every operation is safe to call concurrently from any thread. The
// controller-id counter is atomic, the directory carries its own reader/writer lock, and the
// mailbox its own mutex; no operation holds two of them at once, so there is no lock order to
// violate. A close racing a submit from the same session may let one command through -- the tick
// then ignores it, since a command for a destroyed entity is ignored by contract -- so this is a
// well-defined benign race and not an ownership hole.
// related: command_mailbox.hpp -- where an accepted command waits for its tick.
// related: controller_directory.hpp -- where a session's presentation values live.
// related: simulation_runtime.hpp -- the owner that hands this out.
class CommandSink final {
public:
  // `first_controller_id` is the lowest id this sink may issue. It must open above every
  // controller id the committed world already carries, because a seeded entity derives its
  // controller id from its entity id and would otherwise share a value with the first session:
  // the client resolves its own body by controller id, so a colliding session silently adopts a
  // body it never asked for instead of spawning. `above_committed_state` computes it.
  CommandSink(CommandMailbox& mailbox, ControllerDirectory& controller_directory,
              const EntityIdAllocator& entity_id_allocator,
              simulation::ControllerId::Value first_controller_id) noexcept;

  // The lowest controller id no session has been issued yet, for diagnostics and tests.
  [[nodiscard]] simulation::ControllerId::Value next_controller_id() const noexcept {
    return next_controller_id_.load(std::memory_order_acquire);
  }

  CommandSink(const CommandSink&) = delete;
  CommandSink(CommandSink&&) = delete;
  CommandSink& operator=(const CommandSink&) = delete;
  CommandSink& operator=(CommandSink&&) = delete;
  ~CommandSink() = default;

  // Issues one durable controller identity and registers its presentation values. The returned id
  // is monotonic and is never reused, so a stale id from a closed session can never address a
  // later one.
  //
  // Throws CommandSinkError when the directory is full, when either string is rejected, and when
  // the controller-id space is exhausted. Nothing partial is left behind by a throw.
  [[nodiscard]] simulation::ControllerId open_session(std::string_view controller_kind,
                                                      std::string_view display_name);

  // Offers one command from one open session. Total and never throws; every refusal is a named
  // result and is also counted in the mailbox's statistics.
  [[nodiscard]] CommandSubmissionResult submit(simulation::ControllerId controller,
                                               const simulation::Command& command);

  // Enqueues this controller's `leave`, then retires the identity: its entry leaves the directory
  // and it may submit nothing more. The leave is what destroys everything the controller drove,
  // seated or still queued as a spawn, and vacates its seat, so a caller needs to know nothing
  // about the world to leave it cleanly (`commands/leave_command.hpp`). Idempotent -- a second
  // close enqueues nothing and reports `kUnknownControllerId` rather than failing -- because a
  // session's close path may run on more than one code path.
  [[nodiscard]] ControllerCloseResult close_session(simulation::ControllerId controller);

private:
  // Whether every value in this command is one `InputBatch::create` will accept.
  [[nodiscard]] CommandSubmissionResult
  validate_command_values(const simulation::Command& command) const;

  CommandMailbox* mailbox_;
  ControllerDirectory* controller_directory_;
  const EntityIdAllocator* entity_id_allocator_;
  std::atomic<simulation::ControllerId::Value> next_controller_id_;
};

} // namespace blob_royale::runtime

#endif
