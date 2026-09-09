#include "command_sink.hpp"

#include "command_sink_error.hpp"
#include "seat_roster.hpp"
#include "simulation_limits.hpp"

#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

namespace blob_royale::runtime {
namespace {

// The identity one command is stamped with, or nullopt for a kind that carries none. Total over the
// closed variant: a kind with a `controller` member answers with it, and every other kind answers
// nullopt (`src/simulation/command_registry.hpp`, AddressedIdentity, makes the same distinction for
// a different purpose).
[[nodiscard]] std::optional<simulation::ControllerId>
stamped_controller_of(const simulation::Command& command) noexcept {
  return std::visit(
      []<typename CommandType>(
          const CommandType& value) -> std::optional<simulation::ControllerId> {
        if constexpr (std::is_same_v<CommandType, simulation::SpawnCommand> ||
                      std::is_same_v<CommandType, simulation::SetSeatCountCommand> ||
                      std::is_same_v<CommandType, simulation::ClearSeatCommand> ||
                      std::is_same_v<CommandType, simulation::SeatNpcCommand> ||
                      std::is_same_v<CommandType, simulation::StartMatchCommand> ||
                      std::is_same_v<CommandType, simulation::LeaveCommand> ||
                      std::is_same_v<CommandType, simulation::JoinCommand>) {
          return value.controller;
        } else {
          return std::nullopt;
        }
      },
      command);
}

} // namespace

CommandSink::CommandSink(CommandMailbox& mailbox, ControllerDirectory& controller_directory,
                         const EntityIdAllocator& entity_id_allocator,
                         const simulation::ControllerId::Value first_controller_id) noexcept
    : mailbox_(&mailbox), controller_directory_(&controller_directory),
      entity_id_allocator_(&entity_id_allocator), next_controller_id_(first_controller_id) {}

simulation::ControllerId CommandSink::open_session(const std::string_view controller_kind,
                                                   const std::string_view display_name) {
  // Both strings are checked before an id is consumed, so the common refusals cost nothing and the
  // rules live in exactly one place -- the directory that stores them.
  if (!ControllerDirectory::is_valid_controller_kind(controller_kind)) {
    throw CommandSinkError(CommandSinkErrorCode::kControllerKindRejected,
                           "command_sink.controller_kind",
                           "a controller kind must be non-empty, at most " +
                               std::to_string(kMaximumControllerKindLength) +
                               " bytes, and free of control characters");
  }
  if (!ControllerDirectory::is_valid_display_name(display_name)) {
    throw CommandSinkError(CommandSinkErrorCode::kDisplayNameRejected, "command_sink.display_name",
                           "a display name must be at most " +
                               std::to_string(kMaximumDisplayNameLength) +
                               " bytes and free of control characters");
  }

  const simulation::ControllerId::Value issued =
      next_controller_id_.fetch_add(1, std::memory_order_acq_rel);
  if (issued > simulation::kMaximumControllerId) {
    throw CommandSinkError(CommandSinkErrorCode::kControllerIdExhausted,
                           "command_sink.controller_id",
                           "the monotonic ControllerId space is exhausted after " +
                               std::to_string(simulation::kMaximumControllerId) + " sessions");
  }
  const simulation::ControllerId controller = simulation::ControllerId::create(issued);

  const ControllerRegistrationResult registration =
      controller_directory_->register_controller(controller, controller_kind, display_name);
  if (registration != ControllerRegistrationResult::kRegistered) {
    // The consumed id is simply never issued again. That is harmless -- ids are monotonic over a
    // 2^53 space -- and it is what keeps a failed open from leaving a directory entry behind.
    throw CommandSinkError(
        CommandSinkErrorCode::kControllerDirectoryFull, "command_sink.controller_directory",
        "the controller directory refused ControllerId " + std::to_string(issued) + ": " +
            std::string(controller_registration_result_name(registration)));
  }
  return controller;
}

CommandSubmissionResult CommandSink::submit(const simulation::ControllerId controller,
                                            const simulation::Command& command) {
  if (!controller_directory_->contains(controller)) {
    return CommandSubmissionResult::kRejectedSessionNotOpen;
  }

  // Every command that carries an identity must carry *this* session's. A spawn has always been
  // checked here; the four lobby kinds carry the same stamp for the same reason, so one check
  // covers all five and a sixth kind that carries a controller cannot be forgotten --
  // `stamped_controller_of` is total over the variant.
  if (const std::optional<simulation::ControllerId> stamped = stamped_controller_of(command);
      stamped.has_value() && *stamped != controller) {
    return CommandSubmissionResult::kRejectedForeignController;
  }

  const CommandSubmissionResult value_result = validate_command_values(command);
  if (value_result != CommandSubmissionResult::kAccepted) {
    return value_result;
  }

  return mailbox_->submit(command);
}

ControllerCloseResult CommandSink::close_session(const simulation::ControllerId controller) {
  // The leave goes into the mailbox **before** the directory entry is retired, through the same
  // `submit` a session's own commands take, so a second close is refused there as a closed session
  // and enqueues nothing. The tick then destroys whatever this controller drove -- a body, a
  // pending entity, or a spawn drained in the same batch -- and vacates its seat, which is what
  // closes the window a session-side despawn could not: a spawn still queued at close time is
  // applied and then undone by the leave that follows it in phase 0 order
  // (`commands/leave_command.hpp`).
  //
  // The submission's result is deliberately not returned. It is an acceptance on every path but
  // two: a mode whose accepted kinds omit `leave`, which `GameSimulation::create` refuses at
  // startup, and a mailbox full of lifecycle commands, which the mailbox counts and the composition
  // root reports at error severity as a lost entity-lifecycle command.
  static_cast<void>(submit(controller, simulation::Command{simulation::LeaveCommand{controller}}));
  return controller_directory_->close(controller);
}

CommandSubmissionResult
CommandSink::validate_command_values(const simulation::Command& command) const {
  return std::visit(
      [this]<typename CommandType>(const CommandType& value) {
        if constexpr (std::is_same_v<CommandType, simulation::ThrustCommand>) {
          // The same range InputBatch::create enforces. Refused here so that one client cannot
          // turn its own out-of-range frame into a hard tick failure for everyone.
          const double limit = simulation::kMaximumThrustDirectionComponentMagnitude;
          if (!(std::abs(value.direction.x()) <= limit) ||
              !(std::abs(value.direction.y()) <= limit)) {
            return CommandSubmissionResult::kRejectedThrustDirectionOutOfRange;
          }
          return CommandSubmissionResult::kAccepted;
        } else if constexpr (std::is_same_v<CommandType, simulation::DespawnCommand>) {
          // A despawn naming an id inside the tick's own reservation is the one despawn
          // InputBatch::create rejects outright. The cursor only rises, so an id strictly below the
          // cursor observed here is below every later tick's reservation as well, and a client
          // cannot reach a future block by guessing.
          if (value.entity.value() >= entity_id_allocator_->next_entity_id().value()) {
            return CommandSubmissionResult::kRejectedUnissuedEntityId;
          }
          return CommandSubmissionResult::kAccepted;
        } else if constexpr (std::is_same_v<CommandType, simulation::JoinCommand>) {
          if (value.seat_index.has_value() &&
              *value.seat_index >= simulation::kMaximumLobbySeatCount) {
            return CommandSubmissionResult::kRejectedSeatIndexOutOfRange;
          }
          return CommandSubmissionResult::kAccepted;
        } else if constexpr (std::is_same_v<CommandType, simulation::SeatNpcCommand> ||
                             std::is_same_v<CommandType, simulation::ClearSeatCommand>) {
          // The engine's bound on a seat index, not the live roster's size: this sink holds no
          // world. An index inside the bound that names no seat in the running lobby is ignored by
          // the tick, which is the disagreement that cannot be settled anywhere but there.
          if (value.seat_index >= simulation::kMaximumLobbySeatCount) {
            return CommandSubmissionResult::kRejectedSeatIndexOutOfRange;
          }
          return CommandSubmissionResult::kAccepted;
        } else if constexpr (std::is_same_v<CommandType, simulation::SetSeatCountCommand>) {
          if (value.seat_count < simulation::SeatRoster::kMinimumSeatCount ||
              value.seat_count > simulation::SeatRoster::kMaximumSeatCount) {
            return CommandSubmissionResult::kRejectedSeatCountOutOfRange;
          }
          return CommandSubmissionResult::kAccepted;
        } else {
          return CommandSubmissionResult::kAccepted;
        }
      },
      command);
}

} // namespace blob_royale::runtime
