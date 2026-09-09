#ifndef BLOB_ROYALE_PROTOCOL_SESSION_WELCOME_HPP
#define BLOB_ROYALE_PROTOCOL_SESSION_WELCOME_HPP

#include "command_kind_mask.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::protocol {

// canonical: session_welcome -- the validated value one session's welcome frame is encoded from.
//
// It is a validated value for the reason `PublicConfiguration` is: every member has a bounded
// grammar in the accepted schemas, three of them are strings a boundary supplies, and one of those
// three is derived from a header a trusted proxy wrote. Validating once at construction means the
// encoder never has to decide what to do with a name it cannot publish, and a rejected welcome
// fails at the session that built it rather than at the frame every peer is waiting for.
//
// **`entity_id` is the session's first body only.** Elimination and the lobby wipe destroy a body
// and the server seats the same controller on a new one, so a client that caches this value renders
// the wrong blob after one match; `controller_id` is the durable self-identifier and the client
// resolves its body each frame through it (`docs/protocol/v2.md` § "Entities, controllers, and what
// survives what"; `find_controlled_body` in `protocol_v2_json_encoding.hpp` is the server-side twin
// of that resolution).
//
// `accepted_command_kinds` is stored as the mask the mode declares. The encoder publishes the
// intersection with the client-sendable vocabulary, so a mask naming `spawn` or `despawn` cannot
// advertise either (`command_wire_kind.hpp`). The advertisement is not the enforcement: the
// boundary refuses an unaccepted kind independently and `InputBatch::create` filters again.
//
// **`npc_controller_kinds` is read from `ControllerRegistry` and is why registering a bot costs no
// client change.** A client renders exactly the kinds this frame names -- that is the menu behind a
// right-clicked empty seat -- and the boundary accepts a `seat_npc` naming exactly the kinds this
// frame named, because both read the one list the composition root took from the registry
// (`src/application/blob_royale_application.cpp`, `command_decoding.hpp`). Adding a bot is one row
// in `controller_registry.hpp`; the menu and the accepted vocabulary follow with no schema edit and
// no client edit, which is the property this member exists to buy.
//
// The list is **not** bounded by how many kinds are registered: `welcome-data.schema.json` bounds
// it at `kNpcControllerKindLimit`, a protocol constant, precisely so that the bot count and the
// wire contract are not the same number. A `maxItems` that tracked the registry would put a
// protocol version behind every new bot, which is the cost this member was added to avoid. related:
// docs/protocol/schema/v2/welcome-data.schema.json -- the closed wire shape.
class SessionWelcome final {
public:
  // Validates the complete accepted grammar of every member.
  // Throws ProtocolEncodingError with SESSION_WELCOME_INVALID for any violation.
  [[nodiscard]] static SessionWelcome create(simulation::EntityId entity,
                                             simulation::ControllerId controller,
                                             std::string display_name, std::string mode_name,
                                             std::string map_name,
                                             simulation::CommandKindMask accepted_command_kinds,
                                             std::vector<std::string> npc_controller_kinds);

  SessionWelcome(const SessionWelcome&) = default;
  SessionWelcome(SessionWelcome&&) noexcept = default;
  SessionWelcome& operator=(const SessionWelcome&) = default;
  SessionWelcome& operator=(SessionWelcome&&) noexcept = default;
  ~SessionWelcome() = default;

  [[nodiscard]] simulation::EntityId entity() const noexcept { return entity_; }
  [[nodiscard]] simulation::ControllerId controller() const noexcept { return controller_; }
  [[nodiscard]] const std::string& display_name() const& noexcept { return display_name_; }
  [[nodiscard]] const std::string& display_name() const&& = delete;
  [[nodiscard]] const std::string& mode_name() const& noexcept { return mode_name_; }
  [[nodiscard]] const std::string& mode_name() const&& = delete;
  [[nodiscard]] const std::string& map_name() const& noexcept { return map_name_; }
  [[nodiscard]] const std::string& map_name() const&& = delete;
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept {
    return accepted_command_kinds_;
  }

  // The seatable NPC kinds, in the registry's declared order, which is the order a client renders
  // the menu in. Order is preserved rather than sorted because the registry's order is somebody's
  // deliberate ordering of the bots and a client that re-sorted it would be inventing one.
  [[nodiscard]] std::span<const std::string> npc_controller_kinds() const& noexcept {
    return npc_controller_kinds_;
  }
  [[nodiscard]] std::span<const std::string> npc_controller_kinds() const&& = delete;

  friend bool operator==(const SessionWelcome&, const SessionWelcome&) = default;

private:
  SessionWelcome(simulation::EntityId entity, simulation::ControllerId controller,
                 std::string display_name, std::string mode_name, std::string map_name,
                 simulation::CommandKindMask accepted_command_kinds,
                 std::vector<std::string> npc_controller_kinds) noexcept;

  simulation::EntityId entity_;
  simulation::ControllerId controller_;
  std::string display_name_;
  std::string mode_name_;
  std::string map_name_;
  simulation::CommandKindMask accepted_command_kinds_;
  std::vector<std::string> npc_controller_kinds_;
};

// The accepted grammars, exposed because the identity boundary applies the display-name grammar to
// a `Tailscale-User-Name` value **before** deciding whether to fall back to `player-<entity_id>`,
// and duplicating the pattern there would be a second answer to one question
// (`docs/protocol/v2.md` § "Display names").
[[nodiscard]] bool is_accepted_display_name(std::string_view display_name) noexcept;
// `common.schema.json#/$defs/kind_name`: lower snake case, 1 to 64 characters.
[[nodiscard]] bool is_accepted_kind_name(std::string_view kind_name) noexcept;
// `common.schema.json#/$defs/map_name`: lower kebab/dot/underscore, 1 to 64 characters.
[[nodiscard]] bool is_accepted_map_name(std::string_view map_name) noexcept;

} // namespace blob_royale::protocol

#endif
