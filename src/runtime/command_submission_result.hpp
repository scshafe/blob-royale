#ifndef BLOB_ROYALE_RUNTIME_COMMAND_SUBMISSION_RESULT_HPP
#define BLOB_ROYALE_RUNTIME_COMMAND_SUBMISSION_RESULT_HPP

#include <cstdint>
#include <string_view>

namespace blob_royale::runtime {

// canonical: command_submission_result -- the total answer to one `CommandSink::submit`.
//
// One vocabulary for both the sink and the mailbox, because "what happened to my command?" is one
// question and a caller that had to translate between two enumerations would be the ambiguity this
// codebase removes rather than adds. Submission never throws: a command source is a network
// session, and a hard failure would let one client stop the match
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Accepted simulation input").
//
// Two of these values are acceptances. `kAccepted` took a new slot; `kSuperseded` replaced this
// identity's earlier command of the same kind, which is **lossless**: `InputBatch::create` keeps
// the last command of a kind for each addressed identity, so the tick that reads the mailbox sees
// exactly the batch it would have seen had every submission been appended
// (`docs/architecture/0004-gameplay-architecture.md` § "Commands").
//
// Every other value means the command will never reach a tick, and every one of them is counted in
// `CommandMailbox::Statistics`, so a refusal is observable in aggregate even when the submitting
// caller ignores the return value.
// related: command_mailbox.hpp -- the bounded buffer that produces most of these.
// related: command_sink.hpp -- the write-only capability that produces the rest.
enum class CommandSubmissionResult : std::uint8_t {
  kAccepted = 0,
  kSuperseded = 1,
  kRejectedSessionNotOpen = 2,
  kRejectedForeignController = 3,
  kRejectedUnacceptedKind = 4,
  kRejectedThrustDirectionOutOfRange = 5,
  kRejectedUnissuedEntityId = 6,
  kDroppedMailboxFull = 7,
};

// Whether the command is now pending for a tick. The two acceptances are the only values for which
// the caller may assume its decision was taken.
[[nodiscard]] constexpr bool
command_submission_accepted(const CommandSubmissionResult result) noexcept {
  return result == CommandSubmissionResult::kAccepted ||
         result == CommandSubmissionResult::kSuperseded;
}

// The stable wire/log name of one result, for structured logging and diagnostics.
[[nodiscard]] constexpr std::string_view
command_submission_result_name(const CommandSubmissionResult result) noexcept {
  switch (result) {
  case CommandSubmissionResult::kAccepted:
    return "accepted";
  case CommandSubmissionResult::kSuperseded:
    return "superseded";
  case CommandSubmissionResult::kRejectedSessionNotOpen:
    return "rejected_session_not_open";
  case CommandSubmissionResult::kRejectedForeignController:
    return "rejected_foreign_controller";
  case CommandSubmissionResult::kRejectedUnacceptedKind:
    return "rejected_unaccepted_kind";
  case CommandSubmissionResult::kRejectedThrustDirectionOutOfRange:
    return "rejected_thrust_direction_out_of_range";
  case CommandSubmissionResult::kRejectedUnissuedEntityId:
    return "rejected_unissued_entity_id";
  case CommandSubmissionResult::kDroppedMailboxFull:
    return "dropped_mailbox_full";
  }
  return "command_submission_result_invalid";
}

} // namespace blob_royale::runtime

#endif
