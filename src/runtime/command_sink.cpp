#include "command_sink.hpp"

#include "command_sink_error.hpp"
#include "simulation_limits.hpp"

#include <atomic>
#include <cmath>
#include <string>
#include <type_traits>
#include <variant>

namespace blob_royale::runtime {

CommandSink::CommandSink(CommandMailbox& mailbox, ControllerDirectory& controller_directory,
                         const EntityIdAllocator& entity_id_allocator) noexcept
    : mailbox_(&mailbox), controller_directory_(&controller_directory),
      entity_id_allocator_(&entity_id_allocator),
      next_controller_id_(simulation::kMinimumControllerId) {}

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

  if (const auto* const spawn = std::get_if<simulation::SpawnCommand>(&command);
      spawn != nullptr && spawn->controller != controller) {
    return CommandSubmissionResult::kRejectedForeignController;
  }

  const CommandSubmissionResult value_result = validate_command_values(command);
  if (value_result != CommandSubmissionResult::kAccepted) {
    return value_result;
  }

  return mailbox_->submit(command);
}

ControllerCloseResult CommandSink::close_session(const simulation::ControllerId controller) {
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
        } else {
          return CommandSubmissionResult::kAccepted;
        }
      },
      command);
}

} // namespace blob_royale::runtime
