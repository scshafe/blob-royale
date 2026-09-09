#include "session_welcome.hpp"

#include "protocol_encoding_error.hpp"
#include "protocol_v2_constants.hpp"

#include "simulation_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::protocol {
namespace {

[[nodiscard]] bool is_lowercase_letter(const char character) noexcept {
  return character >= 'a' && character <= 'z';
}

[[nodiscard]] bool is_ascii_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

[[nodiscard]] bool is_alphanumeric(const char character) noexcept {
  return is_ascii_digit(character) || is_lowercase_letter(character) ||
         (character >= 'A' && character <= 'Z');
}

void require(const bool accepted, const std::string_view context, const std::string_view detail) {
  if (accepted) {
    return;
  }
  throw ProtocolEncodingError{ProtocolEncodingErrorCode::kSessionWelcomeInvalid,
                              std::string{context}, std::string{detail}};
}

} // namespace

// `^[A-Za-z0-9](?:[A-Za-z0-9 ._'@-]{0,62}[A-Za-z0-9])?$`, transcribed as a scan so no regular
// expression engine reaches a proxy-supplied value.
bool is_accepted_display_name(const std::string_view display_name) noexcept {
  if (display_name.empty() || display_name.size() > kDisplayNameMaximumCharacterCount) {
    return false;
  }
  if (!is_alphanumeric(display_name.front()) || !is_alphanumeric(display_name.back())) {
    return false;
  }
  for (const char character : display_name) {
    const bool accepted = is_alphanumeric(character) || character == ' ' || character == '.' ||
                          character == '_' || character == '\'' || character == '@' ||
                          character == '-';
    if (!accepted) {
      return false;
    }
  }
  return true;
}

// `^[a-z][a-z0-9_]*$` bounded at 64.
bool is_accepted_kind_name(const std::string_view kind_name) noexcept {
  if (kind_name.empty() || kind_name.size() > kKindNameMaximumCharacterCount) {
    return false;
  }
  if (!is_lowercase_letter(kind_name.front())) {
    return false;
  }
  for (const char character : kind_name) {
    if (!is_lowercase_letter(character) && !is_ascii_digit(character) && character != '_') {
      return false;
    }
  }
  return true;
}

// `^[a-z0-9][a-z0-9._-]*$` bounded at 64.
bool is_accepted_map_name(const std::string_view map_name) noexcept {
  if (map_name.empty() || map_name.size() > kMapNameMaximumCharacterCount) {
    return false;
  }
  if (!is_lowercase_letter(map_name.front()) && !is_ascii_digit(map_name.front())) {
    return false;
  }
  for (const char character : map_name) {
    const bool accepted = is_lowercase_letter(character) || is_ascii_digit(character) ||
                          character == '.' || character == '_' || character == '-';
    if (!accepted) {
      return false;
    }
  }
  return true;
}

SessionWelcome
SessionWelcome::create(const simulation::EntityId entity, const simulation::ControllerId controller,
                       std::string display_name, std::string mode_name, std::string map_name,
                       const simulation::CommandKindMask accepted_command_kinds,
                       std::vector<std::string> npc_controller_kinds, const std::uint64_t lobby_id,
                       const std::uint64_t seat_count_maximum) {
  require(entity.value() >= simulation::kMinimumEntityId, "welcome_message.data.entity_id",
          "entity id must be in the inclusive range 1 to 2^53-1");
  require(controller.value() >= simulation::kMinimumControllerId,
          "welcome_message.data.controller_id",
          "controller id must be in the inclusive range 1 to 2^53-1");
  require(is_accepted_display_name(display_name), "welcome_message.data.display_name",
          "display name must be 1 to 64 printable ASCII characters from the accepted grammar, "
          "beginning and ending alphanumeric");
  require(is_accepted_kind_name(mode_name), "welcome_message.data.mode",
          "mode name must match the accepted lower snake case kind grammar");
  require(is_accepted_map_name(map_name), "welcome_message.data.map",
          "map name must match the accepted map-name grammar");
  // The published NPC vocabulary is validated here, once, so that neither the encoder nor the
  // command decoder has to decide what to do with a registered bot whose name cannot be published.
  // A registry row that fails this is a build-time mistake surfacing at the first session rather
  // than at the first right-click, which is the earlier of the two places it can surface.
  require(npc_controller_kinds.size() <= kNpcControllerKindLimit,
          "welcome_message.data.npc_controller_kinds",
          "the published NPC controller kinds must number at most " +
              std::to_string(kNpcControllerKindLimit));
  for (const std::string& npc_controller_kind : npc_controller_kinds) {
    require(is_accepted_kind_name(npc_controller_kind), "welcome_message.data.npc_controller_kinds",
            "NPC controller kind " + npc_controller_kind +
                " must match the accepted lower snake case kind grammar");
  }

  // The room and the seat ceiling are bounded by protocol constants: a room id past the directory
  // limit names a room no directory could list, and a ceiling past the seat bound is one no
  // `set_seat_count` could reach.
  require(lobby_id >= 1 && lobby_id <= kLobbyDirectoryLimit, "welcome_message.data.lobby_id",
          "lobby id must be in the inclusive range 1 to " + std::to_string(kLobbyDirectoryLimit));
  require(seat_count_maximum >= 1 && seat_count_maximum <= kLobbySeatCountMaximum,
          "welcome_message.data.seat_count_maximum",
          "seat count maximum must be in the inclusive range 1 to " +
              std::to_string(kLobbySeatCountMaximum));

  return SessionWelcome{entity,
                        controller,
                        std::move(display_name),
                        std::move(mode_name),
                        std::move(map_name),
                        accepted_command_kinds,
                        std::move(npc_controller_kinds),
                        lobby_id,
                        seat_count_maximum};
}

SessionWelcome::SessionWelcome(const simulation::EntityId entity,
                               const simulation::ControllerId controller, std::string display_name,
                               std::string mode_name, std::string map_name,
                               const simulation::CommandKindMask accepted_command_kinds,
                               std::vector<std::string> npc_controller_kinds,
                               const std::uint64_t lobby_id,
                               const std::uint64_t seat_count_maximum) noexcept
    : entity_(entity), controller_(controller), display_name_(std::move(display_name)),
      mode_name_(std::move(mode_name)), map_name_(std::move(map_name)),
      accepted_command_kinds_(accepted_command_kinds),
      npc_controller_kinds_(std::move(npc_controller_kinds)), lobby_id_(lobby_id),
      seat_count_maximum_(seat_count_maximum) {}

} // namespace blob_royale::protocol
