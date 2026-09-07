#include "command_mailbox.hpp"

#include <cstddef>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace blob_royale::runtime {

CommandMailbox::CommandMailbox(const simulation::CommandKindMask accepted_kinds)
    : accepted_kinds_(accepted_kinds) {
  pending_.reserve(kMaximumMailboxCommandCount);
}

CommandSubmissionResult CommandMailbox::submit(const simulation::Command& command) {
  const simulation::CommandKind kind = simulation::command_kind_of(command);
  const std::uint64_t ordering_key = simulation::addressed_identity_of(command).ordering_key();
  const bool is_lifecycle = is_entity_lifecycle_command(kind);

  const std::lock_guard lock(mutex_);
  ++statistics_.submitted_command_count;

  if (!accepted_kinds_.contains(kind)) {
    ++statistics_.rejected_unaccepted_kind_count;
    return CommandSubmissionResult::kRejectedUnacceptedKind;
  }

  const std::size_t existing_slot = find_pending_slot_locked(kind, ordering_key);
  if (existing_slot != pending_.size()) {
    pending_[existing_slot] = command;
    ++statistics_.accepted_command_count;
    ++statistics_.superseded_command_count;
    return CommandSubmissionResult::kSuperseded;
  }

  if (pending_.size() < kMaximumMailboxCommandCount) {
    pending_.push_back(command);
    ++statistics_.accepted_command_count;
    return CommandSubmissionResult::kAccepted;
  }

  if (is_lifecycle) {
    const std::size_t evictable_slot = find_evictable_slot_locked();
    if (evictable_slot != pending_.size()) {
      pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(evictable_slot));
      pending_.push_back(command);
      ++statistics_.accepted_command_count;
      ++statistics_.dropped_command_count;
      return CommandSubmissionResult::kAccepted;
    }
    // Every pending command is itself a spawn or a despawn, so honouring "never evict a lifecycle
    // command" leaves only the incoming one to refuse. The caller learns immediately and the
    // dedicated counter records that the roster, not a steering intent, lost a change.
    ++statistics_.dropped_command_count;
    ++statistics_.dropped_entity_lifecycle_command_count;
    return CommandSubmissionResult::kDroppedMailboxFull;
  }

  ++statistics_.dropped_command_count;
  return CommandSubmissionResult::kDroppedMailboxFull;
}

std::vector<simulation::Command> CommandMailbox::drain() {
  std::vector<simulation::Command> drained;
  const std::lock_guard lock(mutex_);
  // Moved out element-wise rather than swapped, so the mailbox keeps the capacity it reserved once
  // at construction and a tick with no commands allocates nothing at all.
  drained.assign(std::make_move_iterator(pending_.begin()),
                 std::make_move_iterator(pending_.end()));
  pending_.clear();
  ++statistics_.drain_count;
  statistics_.drained_command_count += static_cast<std::uint64_t>(drained.size());
  return drained;
}

CommandMailbox::Statistics CommandMailbox::statistics() const {
  const std::lock_guard lock(mutex_);
  // Occupancy is read from `pending_` rather than tracked, so the two can never disagree; the
  // stored copy of that one field is never meaningful and is always overwritten here.
  Statistics observed = statistics_;
  observed.pending_command_count = pending_.size();
  return observed;
}

std::size_t
CommandMailbox::find_pending_slot_locked(const simulation::CommandKind kind,
                                         const std::uint64_t ordering_key) const noexcept {
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (simulation::command_kind_of(pending_[index]) != kind) {
      continue;
    }
    if (simulation::addressed_identity_of(pending_[index]).ordering_key() == ordering_key) {
      return index;
    }
  }
  return pending_.size();
}

std::size_t CommandMailbox::find_evictable_slot_locked() const noexcept {
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (!is_entity_lifecycle_command(simulation::command_kind_of(pending_[index]))) {
      return index;
    }
  }
  return pending_.size();
}

} // namespace blob_royale::runtime
