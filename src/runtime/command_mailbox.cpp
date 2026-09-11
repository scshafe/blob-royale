#include "command_mailbox.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::runtime {

CommandMailbox::CommandMailbox(const simulation::CommandKindMask accepted_kinds)
    : accepted_kinds_(accepted_kinds) {
  pending_.reserve(kMaximumMailboxCommandCount);
  tuning_exchanges_.reserve(kMaximumControllerDirectoryEntryCount);
}

CommandSubmissionResult CommandMailbox::submit(const simulation::Command& command) {
  if (const auto* tuning = std::get_if<simulation::SetMovementTuningCommand>(&command)) {
    return submit_tuning(*tuning, Clock::now());
  }
  const std::lock_guard lock(mutex_);
  ++statistics_.submitted_command_count;
  return submit_locked(command);
}

CommandSubmissionResult CommandMailbox::submit_locked(const simulation::Command& command) {
  const simulation::CommandKind kind = simulation::command_kind_of(command);
  const std::uint64_t ordering_key = simulation::addressed_identity_of(command).ordering_key();
  const bool is_lifecycle = is_entity_lifecycle_command(kind);

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
      record_tuning_eviction_locked(pending_[evictable_slot]);
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
  observed.open_tuning_exchange_count = tuning_exchanges_.size();
  observed.unresolved_tuning_exchange_count = static_cast<std::size_t>(std::count_if(
      tuning_exchanges_.begin(), tuning_exchanges_.end(),
      [](const TuningExchange& exchange) { return exchange.request_id.has_value(); }));
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

CommandMailbox::TuningExchange*
CommandMailbox::find_exchange_locked(const simulation::ControllerId controller) noexcept {
  for (auto& exchange : tuning_exchanges_) {
    if (exchange.controller == controller) {
      return &exchange;
    }
  }
  return nullptr;
}

bool CommandMailbox::register_controller(const simulation::ControllerId controller) {
  const std::lock_guard lock(mutex_);
  if (find_exchange_locked(controller) != nullptr ||
      tuning_exchanges_.size() == kMaximumControllerDirectoryEntryCount) {
    return false;
  }
  tuning_exchanges_.push_back(TuningExchange{controller});
  return true;
}

bool CommandMailbox::retire_controller(const simulation::ControllerId controller) {
  const std::lock_guard lock(mutex_);
  const auto found = std::find_if(
      tuning_exchanges_.begin(), tuning_exchanges_.end(),
      [controller](const TuningExchange& exchange) { return exchange.controller == controller; });
  if (found == tuning_exchanges_.end()) {
    return false;
  }
  tuning_exchanges_.erase(found);
  ++statistics_.submitted_command_count;
  static_cast<void>(submit_locked(simulation::LeaveCommand{controller}));
  return true;
}

CommandSubmissionResult
CommandMailbox::submit_tuning(const simulation::SetMovementTuningCommand& command,
                              const Clock::time_point now) {
  const std::lock_guard lock(mutex_);
  ++statistics_.submitted_command_count;
  if (!accepted_kinds_.contains(simulation::CommandKind::kSetMovementTuning)) {
    ++statistics_.rejected_unaccepted_kind_count;
    return CommandSubmissionResult::kRejectedUnacceptedKind;
  }
  auto* exchange = find_exchange_locked(command.controller);
  if (exchange == nullptr) {
    ++statistics_.rejected_tuning_command_count;
    return CommandSubmissionResult::kRejectedSessionNotOpen;
  }
  if (command.tuning_request_id == 0 ||
      command.tuning_request_id > simulation::kMaximumProtocolSafeInteger) {
    ++statistics_.rejected_tuning_command_count;
    return CommandSubmissionResult::kRejectedTuningRequestIdOutOfRange;
  }
  if (command.expected_revision > simulation::kMaximumProtocolSafeInteger) {
    ++statistics_.rejected_tuning_command_count;
    return CommandSubmissionResult::kRejectedTuningRevisionOutOfRange;
  }
  if (command.tuning_request_id <= exchange->request_high_water) {
    ++statistics_.rejected_tuning_command_count;
    return CommandSubmissionResult::kRejectedTuningRequestIdReused;
  }
  exchange->request_high_water = command.tuning_request_id;
  if (exchange->request_id.has_value()) {
    ++statistics_.rejected_tuning_command_count;
    return CommandSubmissionResult::kRejectedTuningRequestInFlight;
  }

  exchange->request_id = command.tuning_request_id;
  if (exchange->next_eligible_at.has_value() && now < *exchange->next_eligible_at) {
    const auto remaining =
        std::chrono::ceil<std::chrono::milliseconds>(*exchange->next_eligible_at - now).count();
    const auto retry = std::clamp<std::uint64_t>(static_cast<std::uint64_t>(remaining), 1,
                                                 kMovementTuningMinimumIntervalMilliseconds);
    exchange->result =
        MovementTuningAdmissionRefusal{command.controller, command.tuning_request_id,
                                       MovementTuningAdmissionStatus::kRateLimited, retry};
    ++statistics_.rate_limited_tuning_command_count;
    return CommandSubmissionResult::kRejectedTuningRateLimited;
  }

  exchange->next_eligible_at =
      now + std::chrono::milliseconds{kMovementTuningMinimumIntervalMilliseconds};
  const auto result = submit_locked(simulation::Command{command});
  if (result == CommandSubmissionResult::kDroppedMailboxFull) {
    exchange->result =
        MovementTuningAdmissionRefusal{command.controller, command.tuning_request_id,
                                       MovementTuningAdmissionStatus::kMailboxFull, std::nullopt};
  }
  return result;
}

void CommandMailbox::record_tuning_eviction_locked(const simulation::Command& command) {
  const auto* tuning = std::get_if<simulation::SetMovementTuningCommand>(&command);
  if (tuning == nullptr) {
    return;
  }
  auto* exchange = find_exchange_locked(tuning->controller);
  if (exchange != nullptr && exchange->request_id == tuning->tuning_request_id &&
      !exchange->result.has_value()) {
    exchange->result = MovementTuningAdmissionRefusal{
        tuning->controller, tuning->tuning_request_id,
        MovementTuningAdmissionStatus::kMailboxEvicted, std::nullopt};
  }
}

void CommandMailbox::complete_tuning_decisions(
    const std::span<const simulation::MovementTuningDecision> decisions) {
  const std::lock_guard lock(mutex_);
  for (const auto& decision : decisions) {
    auto* exchange = find_exchange_locked(decision.controller);
    if (exchange != nullptr && exchange->request_id == decision.tuning_request_id &&
        !exchange->result.has_value()) {
      exchange->result = decision;
    }
  }
}

std::optional<MovementTuningResult>
CommandMailbox::claim_tuning_result(const simulation::ControllerId controller,
                                    const simulation::TickSequence covering_snapshot_tick) {
  const std::lock_guard lock(mutex_);
  auto* exchange = find_exchange_locked(controller);
  if (exchange == nullptr || !exchange->result.has_value()) {
    return std::nullopt;
  }
  if (const auto* decision = std::get_if<simulation::MovementTuningDecision>(&*exchange->result);
      decision != nullptr && decision->decision_tick > covering_snapshot_tick) {
    return std::nullopt;
  }
  auto result = std::move(exchange->result);
  exchange->result.reset();
  exchange->request_id.reset();
  return result;
}

} // namespace blob_royale::runtime
