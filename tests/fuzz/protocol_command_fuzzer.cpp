#include "command_decoding.hpp"
#include "protocol_v3_constants.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/clear_seat_command.hpp"
#include "commands/seat_npc_command.hpp"
#include "commands/set_movement_tuning_command.hpp"
#include "commands/set_seat_count_command.hpp"
#include "commands/start_match_command.hpp"
#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "simulation_limits.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

// The active v3 command envelope is an attacker-controlled parse path in this project: file
// boundaries read input an operator wrote, while this one reads bytes a browser chose. The oracle
// is the decoder's own total contract (`command_decoding.hpp`):
//
//   * it never throws, for any byte sequence, because an exception on a hostile frame would make
//     that frame's failure mode observably different from a merely malformed one;
//   * a frame above `kClientMessageMaximumByteCount` is `kMessageTooLarge` and is refused before
//     any parsing, so an attacker cannot buy parser work with one oversized message;
//   * an accepted result carries exactly one client-sendable command allowed by the mode mask:
//     thrust addresses the session-stamped entity with finite components in the unit interval;
//     lobby commands carry the distinct session-stamped controller, bounded seats, and only an
//     NPC kind advertised by this session. Server-issued commands remain forbidden even when the
//     mode accepts them.
//     Movement tuning carries the stamped controller, safe correlation/revision numbers, and
//     finite shared movement values inside their intrinsic bounds.
namespace {

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;

constexpr std::uint64_t kStampedEntityIdValue = 4'242;
constexpr std::uint64_t kStampedControllerIdValue = 313;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  const char* const characters = size == 0 ? "" : reinterpret_cast<const char*>(data);
  const std::string_view frame{characters, size};
  const simulation::EntityId stamped_entity = simulation::EntityId::create(kStampedEntityIdValue);
  const simulation::ControllerId stamped_controller =
      simulation::ControllerId::create(kStampedControllerIdValue);
  // A fixture welcome vocabulary, not a second copy of ControllerRegistry. The decoder's
  // contract is membership in the supplied list, independent of which bots production registers.
  const std::array<std::string, 1> npc_controller_kinds{"fuzz_bot"};
  // Retain the original thrust-only wire surface, then exercise every current client payload.
  // Keeping server-issued kinds in both applicable masks proves that mask membership alone can
  // never grant a client permission to send one.
  const std::array<simulation::CommandKindMask, 2> mode_masks{
      simulation::CommandKindMask::create({simulation::CommandKind::kSpawn,
                                           simulation::CommandKind::kDespawn,
                                           simulation::CommandKind::kThrust}),
      simulation::CommandKindMask::all()};

  for (const simulation::CommandKindMask accepted_kinds : mode_masks) {
    protocol::CommandDecodeResult result =
        protocol::CommandDecodeResult::rejected(protocol::CommandDecodeRejection::kMalformed);
    try {
      result = protocol::decode_command_envelope(frame, accepted_kinds, stamped_entity,
                                                 stamped_controller, npc_controller_kinds);
    } catch (...) {
      std::abort();
    }

    if (size > protocol::kClientMessageMaximumByteCount &&
        result.rejection() != protocol::CommandDecodeRejection::kMessageTooLarge) {
      std::abort();
    }
    if (!result.is_accepted()) {
      if (!result.command().has_value()) {
        continue;
      }
      std::abort();
    }
    if (!result.command().has_value()) {
      std::abort();
    }
    if (!accepted_kinds.contains(simulation::command_kind_of(*result.command()))) {
      std::abort();
    }

    std::visit(
        [&]<typename CommandType>(const CommandType& command) {
          if constexpr (std::is_same_v<CommandType, simulation::ThrustCommand>) {
            if (command.entity != stamped_entity || !std::isfinite(command.direction.x()) ||
                !std::isfinite(command.direction.y()) ||
                std::abs(command.direction.x()) >
                    simulation::kMaximumThrustDirectionComponentMagnitude ||
                std::abs(command.direction.y()) >
                    simulation::kMaximumThrustDirectionComponentMagnitude ||
                (command.input_generation.has_value() &&
                 (command.input_generation->value() == 0 ||
                  command.input_generation->value() > simulation::kMaximumProtocolSafeInteger))) {
              std::abort();
            }
          } else if constexpr (std::is_same_v<CommandType, simulation::SetSeatCountCommand> ||
                               std::is_same_v<CommandType, simulation::ClearSeatCommand> ||
                               std::is_same_v<CommandType, simulation::SeatNpcCommand> ||
                               std::is_same_v<CommandType, simulation::StartMatchCommand>) {
            if (command.controller != stamped_controller) {
              std::abort();
            }
            if constexpr (std::is_same_v<CommandType, simulation::SetSeatCountCommand>) {
              if (command.seat_count == 0 ||
                  command.seat_count > protocol::kLobbySeatCountMaximum) {
                std::abort();
              }
            } else if constexpr (std::is_same_v<CommandType, simulation::ClearSeatCommand> ||
                                 std::is_same_v<CommandType, simulation::SeatNpcCommand>) {
              if (command.seat_index > protocol::kLobbySeatIndexMaximum) {
                std::abort();
              }
              if constexpr (std::is_same_v<CommandType, simulation::SeatNpcCommand>) {
                if (std::ranges::find(npc_controller_kinds, command.kind.value()) ==
                    npc_controller_kinds.end()) {
                  std::abort();
                }
              }
            }
          } else if constexpr (std::is_same_v<CommandType, simulation::SetMovementTuningCommand>) {
            if (command.controller != stamped_controller || command.tuning_request_id == 0 ||
                command.tuning_request_id > simulation::kMaximumProtocolSafeInteger ||
                command.expected_revision > simulation::kMaximumProtocolSafeInteger ||
                !std::isfinite(command.tuning.acceleration()) ||
                command.tuning.acceleration() < simulation::kMinimumMovementAcceleration ||
                command.tuning.acceleration() > simulation::kMaximumMovementAcceleration ||
                !std::isfinite(command.tuning.normal_top_speed()) ||
                command.tuning.normal_top_speed() < simulation::kMinimumNormalTopSpeed ||
                command.tuning.normal_top_speed() > simulation::kMaximumNormalTopSpeed) {
              std::abort();
            }
          } else {
            // A new variant must explicitly gain an oracle here; it must not silently inherit
            // the rejection rule for today's four server-issued commands.
            static_assert(std::is_same_v<CommandType, simulation::SpawnCommand> ||
                          std::is_same_v<CommandType, simulation::DespawnCommand> ||
                          std::is_same_v<CommandType, simulation::LeaveCommand> ||
                          std::is_same_v<CommandType, simulation::JoinCommand>);
            std::abort();
          }
        },
        *result.command());
  }
  return 0;
}
