#include "command_decoding.hpp"
#include "protocol_v2_constants.hpp"

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "entity_id.hpp"
#include "simulation_limits.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <variant>

// The v2 command envelope is the first attacker-controlled parse path in this project: every other
// boundary reads a file an operator wrote, while this one reads bytes a browser chose. The oracle
// is the decoder's own total contract (`command_decoding.hpp`):
//
//   * it never throws, for any byte sequence, because an exception on a hostile frame would make
//     that frame's failure mode observably different from a merely malformed one;
//   * a frame above `kClientMessageMaximumByteCount` is `kMessageTooLarge` and is refused before
//     any parsing, so an attacker cannot buy parser work with one oversized message;
//   * an accepted result carries exactly one `ThrustCommand`, addressed to the entity the *session*
//     stamped and never to one the frame named, with both components finite and within the unit
//     interval `InputBatch::create` would revalidate against.
namespace {

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;

constexpr std::uint64_t kStampedEntityIdValue = 4'242;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  const char* const characters = size == 0 ? "" : reinterpret_cast<const char*>(data);
  const std::string_view frame{characters, size};
  const simulation::EntityId stamped_entity = simulation::EntityId::create(kStampedEntityIdValue);
  const simulation::CommandKindMask accepted_kinds = simulation::CommandKindMask::create(
      {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
       simulation::CommandKind::kThrust});

  protocol::CommandDecodeResult result =
      protocol::CommandDecodeResult::rejected(protocol::CommandDecodeRejection::kMalformed);
  try {
    result = protocol::decode_command_envelope(frame, accepted_kinds, stamped_entity);
  } catch (...) {
    std::abort();
  }

  if (size > protocol::kClientMessageMaximumByteCount &&
      result.rejection() != protocol::CommandDecodeRejection::kMessageTooLarge) {
    std::abort();
  }
  if (!result.is_accepted()) {
    if (!result.command().has_value()) {
      return 0;
    }
    std::abort();
  }
  if (!result.command().has_value()) {
    std::abort();
  }

  const auto* thrust = std::get_if<simulation::ThrustCommand>(&*result.command());
  if (thrust == nullptr) {
    std::abort();
  }
  if (thrust->entity != stamped_entity) {
    std::abort();
  }
  if (!std::isfinite(thrust->direction.x()) || !std::isfinite(thrust->direction.y())) {
    std::abort();
  }
  if (std::abs(thrust->direction.x()) > simulation::kMaximumThrustDirectionComponentMagnitude ||
      std::abs(thrust->direction.y()) > simulation::kMaximumThrustDirectionComponentMagnitude) {
    std::abort();
  }
  return 0;
}
