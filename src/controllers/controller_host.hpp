#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLER_HOST_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLER_HOST_HPP

#include "command_sink.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "snapshot_publication.hpp"
#include "tick_sequence.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace blob_royale::controllers {

// What one `ControllerHost::decide_once` pass did. Returned so a caller can react to a single pass;
// the durable observation is `ControllerHost::statistics()`, which accumulates the same counts.
struct ControllerHostPass final {
  // The tick every controller in this pass observed. One value, because the snapshot is acquired
  // once per pass.
  simulation::TickSequence observed_tick_sequence;
  // Controllers whose `decide` returned normally.
  std::size_t deciding_controller_count;
  // Commands those controllers returned.
  std::size_t decided_command_count;
  // Of those, the ones the sink took: `kAccepted` plus `kSuperseded`, both of which reach a tick.
  std::size_t accepted_command_count;
  // Of those, the ones the sink refused. Every refusal is also counted in the mailbox's own
  // statistics, so a bot-specific problem stays separable from mailbox pressure.
  std::size_t refused_command_count;
  // Controllers whose `decide` threw. Their commands, if any, were discarded whole.
  std::size_t failed_controller_count;
};

// One controller's failed decision, kept so a rising failure count is diagnosable. Values are
// copied rather than referenced because a host may outlive nothing in particular and a diagnostic
// may be read long after the pass.
struct ControllerFailure final {
  simulation::ControllerId controller;
  std::string controller_kind;
  // `what()` of the thrown `std::exception`, or a fixed description for a non-standard throw.
  std::string message;
  // The tick the failing controller was observing.
  simulation::TickSequence observed_tick_sequence;
};

// canonical: controller_host -- runs in-process controllers at presentation cadence.
//
// **Two capabilities, and structurally no third.** The constructor takes
// `const SnapshotPublication&` and `CommandSink&` and nothing else, which are exactly the two
// things a networked player's session holds (`docs/architecture/0004-gameplay-architecture.md`
// § "Controllers"). There is no overload, no setter, and no accessor through which a
// `GameSimulation&`, a `GameWorld&`, or a `SimulationRuntime&` could arrive, so "a bot's cadence
// and thread choice cannot reach a tick" is a property of the type rather than a rule to remember
// (`docs/architecture/0002-simulation-architecture.md` § "Ownership and lifecycle").
//
// **One acquisition per pass.** `decide_once` calls `SnapshotPublication::latest()` exactly once
// and hands every hosted controller an `Observation` over that same retained value. Acquiring per
// controller would let the runtime worker publish between two controllers of one pass, so two bots
// in one roster would decide against different worlds -- a difference no human player can observe,
// since a session sees one pushed frame at a time, and therefore a break of the symmetry in the
// bots' favour.
//
// **Ascending `ControllerId` is the pass order, and it is structural.** ADR 0004's sketch says
// "ascending `entity()` order"; that was written before plan Step 22 settled the identity split,
// and it is not a usable order: `entity()` is absent before a first spawn, so every pending
// controller ties, and it *changes on every respawn*, so the same roster in the same world would
// reorder itself mid-match. `ControllerId` is issued monotonically by
// `CommandSink::open_session`, is never reused, and never changes, so ascending `ControllerId` is
// total and stable for the life of the host. It is also the key `InputBatch` already orders spawns
// by (`AddressedIdentity::of_controller`), so the host's submission order and the tick's canonical
// order agree for the one kind where the host's order could otherwise be observable. `add` inserts
// at the ascending position, so the order is a property of the container rather than of a sort
// somewhere in the pass.
//
// **A controller that throws is isolated and counted; it never ends the pass.** That is the
// symmetry again: a network session that misbehaves is refused, not fatal, so a bot must be exactly
// as unable to stop the match, and ADR 0004 anticipates controllers that call out to a model, a
// process, or a network. The failing controller's commands are discarded whole for that pass -- it
// returns by value, so there is nothing partial to submit -- and the remaining controllers decide
// normally. Nothing is swallowed: `failed_controller_count` rises and `last_failure()` names the
// controller, its kind, its message, and the tick. `blob_controllers` links no logger, exactly as
// `blob_runtime` does not, so the host counts and the composition root turns a rising count into a
// structured line (`docs/architecture/0002-simulation-architecture.md` § "Ownership and
// lifecycle").
//
// **A submission the sink refuses is counted and the pass continues.** `CommandSink::submit` is
// total and never throws; every refusal it can return -- a closed session, a foreign controller
// stamp, an unaccepted kind, an out-of-range thrust, an unissued entity id, a full mailbox -- is a
// defined answer that a network session receives identically. The host does not retry, because the
// mailbox supersedes by kind and identity so a retry would be either identical or already
// superseded, and it does not stop that controller's remaining commands, because each command is
// refused on its own merits. `refused_command_count` rises; the mailbox counts the same refusal in
// its own statistics.
//
// **Threading.** The host is not internally synchronized and is driven by one caller at
// presentation cadence, which is the composition root's presentation thread. Both capabilities it
// holds are thread-safe on their own, so the host may be driven from any one thread; what it may
// not do is have two threads in `decide_once` at once, because controllers own mutable behavior
// state.
// related: controller.hpp -- the role this runs.
// related: observation.hpp -- the value this builds once per controller per pass.
// related: ../runtime/command_sink.hpp -- the write capability, shared with a network session.
// related: ../runtime/snapshot_publication.hpp -- the read capability, shared with a network
// session.
class ControllerHost final {
public:
  // Everything the host has observed since construction. Monotonic counters, so a reader compares
  // two observations rather than resetting anything -- the same shape as
  // `CommandMailbox::Statistics`, for the same reason.
  struct Statistics final {
    std::uint64_t pass_count{0};
    std::uint64_t decided_command_count{0};
    std::uint64_t accepted_command_count{0};
    std::uint64_t refused_command_count{0};
    std::uint64_t failed_controller_count{0};
  };

  // The whole construction surface: read capability, write capability, nothing else.
  ControllerHost(const runtime::SnapshotPublication& publication,
                 runtime::CommandSink& sink) noexcept;

  ControllerHost(const ControllerHost&) = delete;
  ControllerHost(ControllerHost&&) = delete;
  ControllerHost& operator=(const ControllerHost&) = delete;
  ControllerHost& operator=(ControllerHost&&) = delete;
  ~ControllerHost() = default;

  // Takes ownership of one controller and files it at its ascending `ControllerId` position.
  //
  // Throws ControllersValidationError with `CONTROLLERS.CONTROLLER_ABSENT` for a null controller,
  // `CONTROLLERS.CONTROLLER_ID_DUPLICATE` when a hosted controller already claims that identity --
  // the sink issues each id once, and two controllers sharing one would supersede each other's
  // commands in the mailbox and share one directory entry -- and `CONTROLLERS.CONTROLLER_HOST_FULL`
  // above `kMaximumHostedControllerCount`.
  void add(std::unique_ptr<Controller> controller);

  [[nodiscard]] std::size_t size() const noexcept { return controllers_.size(); }

  // Whether a hosted controller claims this durable identity.
  [[nodiscard]] bool contains(simulation::ControllerId controller) const noexcept;

  // One decision pass: acquire the latest snapshot once, let every hosted controller decide in
  // ascending `ControllerId` order, and submit each controller's commands, in the order it returned
  // them, before the next controller decides.
  //
  // It contains every controller failure and every sink refusal, so nothing a controller does can
  // leave this function; it is not marked `noexcept` for the same reason `CommandMailbox::submit`
  // is not, because recording a failure's detail allocates and `std::bad_alloc` is a process-level
  // condition rather than a controller misbehaving.
  //
  // The return value is a convenience for a caller reacting to one pass; ignoring it loses nothing,
  // because `statistics()` accumulates every count in it and `last_failure()` keeps the detail.
  ControllerHostPass decide_once();

  [[nodiscard]] Statistics statistics() const noexcept { return statistics_; }

  // The most recent controller failure, or `std::nullopt` when none has occurred. Read beside
  // `statistics().failed_controller_count`, which is what says whether the failure is one event or
  // a pattern.
  [[nodiscard]] const std::optional<ControllerFailure>& last_failure() const& noexcept {
    return last_failure_;
  }
  [[nodiscard]] const std::optional<ControllerFailure>& last_failure() const&& = delete;

private:
  // The insertion point for this identity, and whether one already claims it.
  [[nodiscard]] std::size_t
  ascending_position_of(simulation::ControllerId controller) const noexcept;

  void record_failure(const Controller& controller, std::string message,
                      simulation::TickSequence observed_tick_sequence);

  const runtime::SnapshotPublication* publication_;
  runtime::CommandSink* sink_;
  // Strict ascending `ControllerId` by construction, so the pass order is the container's order and
  // nothing sorts at decision time.
  std::vector<std::unique_ptr<Controller>> controllers_;
  Statistics statistics_;
  std::optional<ControllerFailure> last_failure_;
};

} // namespace blob_royale::controllers

#endif
