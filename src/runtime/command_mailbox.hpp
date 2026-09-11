#ifndef BLOB_ROYALE_RUNTIME_COMMAND_MAILBOX_HPP
#define BLOB_ROYALE_RUNTIME_COMMAND_MAILBOX_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "command_submission_result.hpp"
#include "movement_tuning_result.hpp"
#include "runtime_limits.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace blob_royale::runtime {

// Whether losing this command would change *whether an entity exists*. The mailbox never evicts
// one to make room, because a dropped despawn leaves a disconnected player's body in the arena and
// a dropped spawn leaves a connected player with no body -- both are visible, persistent
// corruptions of the roster, while a dropped thrust is one missed 2.5 ms of steering.
//
// **A `join` answers `true` because a dropped one is a person or a bot with no seat**, and while
// a session asks again a tenth of a second later, a bot's join is asked exactly once by the
// reconciliation that created it.
//
// **A `leave` answers `true` for the reason a despawn does, only more so.** It is the one command
// that removes a departed session's entities and vacates its seat, and a dropped one is exactly the
// body nobody owns that review finding 1 describes; the session that could have re-sent it is gone.
//
// **The four lobby kinds answer `false`, and the answer took some deciding.** They change the
// *seat* roster rather than the entity roster, so the question this function asks is answered no by
// construction. The tempting argument for `true` is that a dropped `start_match` is a button that
// did nothing, which feels worse than a dropped thrust -- and it is, but it is not the failure this
// classification protects against. A lost despawn is unrecoverable by anyone: the body stays,
// nobody owns it, and no person can press anything to remove it. A lost Start is visible to the
// person who pressed it, in a lobby they are looking at, and pressing again fixes it. Promoting
// them would also let a lobby press evict a queued spawn, which trades a recoverable annoyance for
// an unrecoverable one.
//
// The superseding rule above is what keeps that safe in the first place: a sender occupies one slot
// per lobby kind no matter how fast it clicks, so lobby traffic cannot flood the mailbox and the
// eviction path is reached by pressure from elsewhere.
//
// The static assertion is the guard that keeps this honest: a new command kind cannot be added
// without an author deciding, here, whether losing it changes the roster.
// related: command_registry.hpp -- the closed list this classifies.
[[nodiscard]] constexpr bool
is_entity_lifecycle_command(const simulation::CommandKind kind) noexcept {
  switch (kind) {
  case simulation::CommandKind::kSpawn:
  case simulation::CommandKind::kDespawn:
  case simulation::CommandKind::kLeave:
  case simulation::CommandKind::kJoin:
    return true;
  case simulation::CommandKind::kThrust:
  case simulation::CommandKind::kSetSeatCount:
  case simulation::CommandKind::kClearSeat:
  case simulation::CommandKind::kSeatNpc:
  case simulation::CommandKind::kStartMatch:
  case simulation::CommandKind::kSetMovementTuning:
    return false;
  }
  return false;
}

static_assert(simulation::kCommandKindCount == 10,
              "a new CommandKind must declare in is_entity_lifecycle_command whether losing it "
              "changes whether an entity exists");

// canonical: command_mailbox -- the bounded buffer between every command source and one tick.
//
// **This is the only path from outside the runtime into a tick.** Any thread submits; the one
// runtime worker drains it exactly once per tick and hands the drained commands to
// `InputBatch::create` (`docs/architecture/0002-simulation-architecture.md` § "Ownership and
// lifecycle"). A `std::mutex` guards the whole value, so a submission and a drain are each one
// atomic step and a drained vector can never contain a half-written command.
//
// **Superseding for non-tuning commands.** When a command of the same kind for the same addressed
// identity is already pending, `submit` replaces it in place. That is lossless --
// `InputBatch::create` keeps the last command of a kind per identity, so the resulting batch is
// identical either way
// (`docs/architecture/0004-gameplay-architecture.md` § "Commands") -- and it turns the capacity
// bound into a bound on *distinct commanded identities* rather than on submission rate. One client
// spamming thrusts therefore occupies one slot forever and cannot evict another client's input,
// which is the property that makes a shared bounded buffer safe to expose to the network.
// Tuning instead retains one unresolved exchange per open controller: it rejects a second request
// until the terminal result is claimed, so no queued tuning request is silently superseded.
//
// **The overflow policy, in order.** A submission that needs a new slot in a full mailbox:
//
//  1. If it is a spawn or a despawn, the oldest pending *non-lifecycle* command is evicted to make
//     room and the incoming command is stored. The evicted command is counted as a drop.
//  2. If it is a spawn or a despawn and every pending command is also a spawn or a despawn, the
//     incoming command is dropped, because the alternative is dropping a queued lifecycle command
//     whose session already believes it was taken. `kDroppedMailboxFull` is returned so the caller
//     can apply back-pressure or close the session.
//  3. Otherwise the incoming command is dropped and `kDroppedMailboxFull` is returned.
//
// **No drop is silent.** Every refusal is both returned to the submitting caller and counted in
// `statistics()`, and a spawn or despawn that was dropped is counted a second time in its own
// counter, because "we dropped a steering intent" and "we dropped a body" are different severities.
// `blob_runtime` links no logger by contract (`docs/architecture/0002-simulation-architecture.md`
// § "Ownership and lifecycle": `StructuredLogger` "never enters simulation, runtime, or protocol
// values"), so the counter is the runtime's observation and the composition root turns a rising
// count into a `warning` log line.
//
// **The mailbox holds only commands `InputBatch::create` will accept.** `CommandSink` refuses
// every value the factory would reject before it is stored, so building a batch from a drained
// mailbox cannot throw. A throw from `InputBatch::create` therefore means the boundary and the
// engine disagree, which ADR 0003 § "Accepted simulation input" keeps a hard failure.
// related: command_sink.hpp -- the write-only capability that fronts this.
// related: simulation_runtime.hpp -- the sole drainer.
// related: input_batch.hpp -- the value a drained mailbox becomes.
class CommandMailbox final {
public:
  using Clock = std::chrono::steady_clock;
  // Everything the mailbox has observed since construction, plus its current occupancy. Monotonic
  // counters, so a reader compares two observations rather than resetting anything.
  struct Statistics final {
    // Every submission that reached the mailbox, whatever became of it.
    std::uint64_t submitted_command_count{0};
    // Submissions that are pending or were drained: `kAccepted` plus `kSuperseded`.
    std::uint64_t accepted_command_count{0};
    // Lossless replacements of a pending command of the same kind for the same identity.
    std::uint64_t superseded_command_count{0};
    // Commands that will never reach a tick: the evicted and the refused, together.
    std::uint64_t dropped_command_count{0};
    // The subset of the above that were a spawn or a despawn. Nonzero means the roster itself lost
    // a change, which is strictly worse than a lost steering intent.
    std::uint64_t dropped_entity_lifecycle_command_count{0};
    // Submissions refused because the running mode does not accept the kind.
    std::uint64_t rejected_unaccepted_kind_count{0};
    // Drains performed. The worker drains exactly once per tick, so this equals the committed tick
    // sequence on a quiescent runtime.
    std::uint64_t drain_count{0};
    // Commands handed to ticks across every drain.
    std::uint64_t drained_command_count{0};
    // Commands pending right now.
    std::size_t pending_command_count{0};
    std::uint64_t rejected_tuning_command_count{0};
    std::uint64_t rate_limited_tuning_command_count{0};
    std::size_t open_tuning_exchange_count{0};
    std::size_t unresolved_tuning_exchange_count{0};

    friend bool operator==(const Statistics&, const Statistics&) = default;
  };

  // The accepted set is `GameSimulation::accepted_command_kinds()`, copied by value at runtime
  // construction, so the mailbox cannot widen behind its holder's back
  // (`docs/architecture/0004-gameplay-architecture.md` § "Commands").
  explicit CommandMailbox(simulation::CommandKindMask accepted_kinds);

  CommandMailbox(const CommandMailbox&) = delete;
  CommandMailbox(CommandMailbox&&) = delete;
  CommandMailbox& operator=(const CommandMailbox&) = delete;
  CommandMailbox& operator=(CommandMailbox&&) = delete;
  ~CommandMailbox() = default;

  // Stores one command under the policy above. Callable from any thread and never throws.
  [[nodiscard]] CommandSubmissionResult submit(const simulation::Command& command);

  // Same admission owner with an explicit runtime time point for deterministic deadline checks.
  // The production submit path supplies Clock::now(); no wall clock reaches simulation.
  [[nodiscard]] CommandSubmissionResult
  submit_tuning(const simulation::SetMovementTuningCommand& command, Clock::time_point now);

  // Session lifetime registration is bounded by the existing controller-directory limit. Open
  // rolls directory registration back if this refuses. Retirement and Leave insertion are atomic.
  [[nodiscard]] bool register_controller(simulation::ControllerId controller);
  [[nodiscard]] bool retire_controller(simulation::ControllerId controller);

  // Called only with decisions returned from a successful step. Retired or nonmatching requests
  // are discarded; a late completion can never overwrite a newer exchange or terminal refusal.
  void complete_tuning_decisions(std::span<const simulation::MovementTuningDecision> decisions);

  // Moves every pending command out in submission order and leaves the mailbox empty. Called by
  // the runtime worker exactly once per tick; nothing else may call it, because a second drainer
  // would give one tick's commands to two ticks.
  [[nodiscard]] std::vector<simulation::Command> drain();

  // One coherent observation of every counter. Callable from any thread.
  [[nodiscard]] Statistics statistics() const;

  // The kinds this mailbox stores, for the boundary that wants to refuse earlier still.
  [[nodiscard]] simulation::CommandKindMask accepted_kinds() const noexcept {
    return accepted_kinds_;
  }

private:
  friend class MovementTuningResultDelivery;
  [[nodiscard]] std::optional<MovementTuningResult>
  claim_tuning_result(simulation::ControllerId controller,
                      simulation::TickSequence covering_snapshot_tick);

  struct TuningExchange final {
    simulation::ControllerId controller;
    std::uint64_t request_high_water{0};
    std::optional<Clock::time_point> next_eligible_at{};
    // A request ID without a result is pending; a result is terminal but still unresolved until
    // claimed. Both are reset together on claim. High-water/deadline survive that transfer.
    std::optional<std::uint64_t> request_id{};
    std::optional<MovementTuningResult> result{};
  };

  [[nodiscard]] TuningExchange* find_exchange_locked(simulation::ControllerId controller) noexcept;
  [[nodiscard]] CommandSubmissionResult submit_locked(const simulation::Command& command);
  void record_tuning_eviction_locked(const simulation::Command& command);
  // The pending index of a command with this kind and addressed identity, or `pending_.size()`
  // when none is pending. Linear, which is deliberate: occupancy is bounded by
  // kMaximumMailboxCommandCount and a scan needs no second data structure to keep in agreement
  // with `pending_`, which is the class of duplication this codebase removes.
  [[nodiscard]] std::size_t find_pending_slot_locked(simulation::CommandKind kind,
                                                     std::uint64_t ordering_key) const noexcept;

  // The index of the oldest pending command that is not a spawn or a despawn, or `pending_.size()`
  // when every pending command is one.
  [[nodiscard]] std::size_t find_evictable_slot_locked() const noexcept;

  mutable std::mutex mutex_;
  simulation::CommandKindMask accepted_kinds_;
  // Submission order. Canonical order is `InputBatch::create`'s job, not this buffer's.
  std::vector<simulation::Command> pending_;
  std::vector<TuningExchange> tuning_exchanges_;
  Statistics statistics_;
};

} // namespace blob_royale::runtime

#endif
