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
// Every other value means the command will never reach a tick. Refusals reaching the mailbox are
// counted in `CommandMailbox::Statistics`; earlier sink validation is returned to its caller.
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
  kRejectedSeatIndexOutOfRange = 8,
  kRejectedSeatCountOutOfRange = 9,
  kRejectedTuningRequestIdReused = 10,
  kRejectedTuningRequestInFlight = 11,
  kRejectedTuningRequestIdOutOfRange = 12,
  kRejectedTuningRevisionOutOfRange = 13,
  kRejectedTuningRateLimited = 14,
  kRejectedThrustInputGenerationOutOfRange = 15,
  kRejectedNpcDeclarationUnknown = 16,
  kRejectedJoinDeclarationInvalid = 17,
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
  case CommandSubmissionResult::kRejectedSeatIndexOutOfRange:
    return "rejected_seat_index_out_of_range";
  case CommandSubmissionResult::kRejectedSeatCountOutOfRange:
    return "rejected_seat_count_out_of_range";
  case CommandSubmissionResult::kRejectedTuningRequestIdReused:
    return "rejected_tuning_request_id_reused";
  case CommandSubmissionResult::kRejectedTuningRequestInFlight:
    return "rejected_tuning_request_in_flight";
  case CommandSubmissionResult::kRejectedTuningRequestIdOutOfRange:
    return "rejected_tuning_request_id_out_of_range";
  case CommandSubmissionResult::kRejectedTuningRevisionOutOfRange:
    return "rejected_tuning_revision_out_of_range";
  case CommandSubmissionResult::kRejectedTuningRateLimited:
    return "rejected_tuning_rate_limited";
  case CommandSubmissionResult::kRejectedThrustInputGenerationOutOfRange:
    return "rejected_thrust_input_generation_out_of_range";
  case CommandSubmissionResult::kRejectedNpcDeclarationUnknown:
    return "rejected_npc_declaration_unknown";
  case CommandSubmissionResult::kRejectedJoinDeclarationInvalid:
    return "rejected_join_declaration_invalid";
  }
  return "command_submission_result_invalid";
}

} // namespace blob_royale::runtime

#endif
