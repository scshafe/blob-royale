#ifndef BLOB_ROYALE_PROTOCOL_V3_HTTP_ERROR_HPP
#define BLOB_ROYALE_PROTOCOL_V3_HTTP_ERROR_HPP

#include "http_error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::protocol {

// Why a proxy-forwarded connection's forwarded client address was refused.
//
// **A closed enum and not free text.** No byte of an attacker-supplied forwarding header may reach
// a response body or a log line, so the response names the failure mode and never the value
// (`docs/protocol/v3.md` § "Error registry additions").
enum class ForwardedClientReason : std::uint8_t {
  kAbsent = 0,
  kMultipleValues = 1,
  kNotCanonical = 2,
};

[[nodiscard]] constexpr std::string_view
forwarded_client_reason_name(const ForwardedClientReason reason) noexcept {
  switch (reason) {
  case ForwardedClientReason::kAbsent:
    return "absent";
  case ForwardedClientReason::kMultipleValues:
    return "multiple_values";
  case ForwardedClientReason::kNotCanonical:
    return "not_canonical";
  }
  return "forwarded_client_reason_invalid";
}

// Which of the three room refusals a `LOBBY.*` row is (`docs/protocol/v3.md` § "Error registry
// additions", 2.4). A closed enum: the message of each is a constant, and the only value a row
// carries is the lobby id the request named, which is a bounded integer the router validated.
enum class LobbyErrorKind : std::uint8_t {
  kNotFound = 0,
  kFull = 1,
  kUnavailable = 2,
};

// canonical: v3_http_error -- one member of the protocol v3 status/error registry.
//
// v1's fourteen rows retain their unchanged value/encoding policy through HttpError. Session v3
// also retains v2's forwarded-client and three lobby refusals, and adds one explicit retirement
// row. None widens v1's closed registry. This is the sole current session error value and encoder;
// historical v2 schemas describe past traffic, not a second active encoder.
//
// Every session-only message is fixed. Forwarded-client details use a closed reason enum, room
// details use a validated bounded integer, and retirement details contain only the required major.
// No forwarding header, offered token, or raw request target can flow into those details.
// related: http_error.hpp -- the fourteen shared rows.
// related: docs/protocol/schema/v3/error-response.schema.json -- the closed wire shape.
class V3HttpError final {
public:
  static constexpr std::string_view kSessionVersionUpgradeRequiredCode =
      "PROTOCOL.SESSION_VERSION_UPGRADE_REQUIRED";
  static constexpr std::string_view kSessionVersionUpgradeRequiredMessage =
      "Use GET /api/v3/lobbies and /api/v3/lobbies/<lobby_id>/session with "
      "subprotocol blob-royale.session.v3.";
  // A retired v2 route never admits a session or consumes an upgrade token. Guidance is fixed,
  // never copied from the untrusted target. This row is deliberately absent from v1 HttpError.
  [[nodiscard]] static V3HttpError session_version_upgrade_required();
  static constexpr std::string_view kInvalidForwardedClientCode =
      "PROTOCOL.INVALID_FORWARDED_CLIENT";
  static constexpr std::string_view kInvalidForwardedClientMessage =
      "A proxy-forwarded connection must present exactly one canonical forwarded client address.";

  // One of the fourteen rows v3 shares with v1, unchanged in every field.
  [[nodiscard]] static V3HttpError shared(HttpError error);

  // Never retryable: a forwarding header a proxy got wrong does not become
  // right by being sent again.
  [[nodiscard]] static V3HttpError invalid_forwarded_client(ForwardedClientReason reason);

  static constexpr std::string_view kLobbyNotFoundCode = "LOBBY.NOT_FOUND";
  static constexpr std::string_view kLobbyFullCode = "LOBBY.FULL";
  static constexpr std::string_view kLobbyUnavailableCode = "LOBBY.UNAVAILABLE";
  static constexpr std::string_view kLobbyNotFoundMessage =
      "No lobby has this id; GET /api/v3/lobbies lists the rooms that exist.";
  static constexpr std::string_view kLobbyFullMessage =
      "Every seat in this lobby is taken; read the directory and choose another.";
  static constexpr std::string_view kLobbyUnavailableMessage =
      "This lobby's runtime is not serving; choose another room or retry later.";

  // The three 2.4 rows. Not found is never retryable -- an id that names no room will not start
  // to; full and unavailable are, because a seat frees up and a room comes back.
  [[nodiscard]] static V3HttpError lobby_not_found();
  [[nodiscard]] static V3HttpError lobby_full(std::uint64_t lobby_id);
  [[nodiscard]] static V3HttpError lobby_unavailable(std::uint64_t lobby_id);

  V3HttpError(const V3HttpError&) = default;
  V3HttpError(V3HttpError&&) noexcept = default;
  V3HttpError& operator=(const V3HttpError&) = default;
  V3HttpError& operator=(V3HttpError&&) noexcept = default;
  ~V3HttpError() = default;

  [[nodiscard]] std::uint16_t status_code() const noexcept;
  [[nodiscard]] std::string_view code() const noexcept;
  [[nodiscard]] std::string_view message() const noexcept;
  [[nodiscard]] bool retryable() const noexcept;

  // The shared row this carries, or nullptr for a session-only row. The encoder reads the details
  // straight off it, so the details policy has one implementation across both versions.
  [[nodiscard]] const HttpError* shared_error() const& noexcept;
  [[nodiscard]] const HttpError* shared_error() const&& = delete;

  // Present exactly on the forwarded-client row, which makes `forwarded_client_reason` a required
  // detail there and an impossible one everywhere else.
  [[nodiscard]] std::optional<ForwardedClientReason> forwarded_client_reason() const noexcept {
    return forwarded_client_reason_;
  }
  // Present exactly on a `LOBBY.*` row.
  [[nodiscard]] std::optional<LobbyErrorKind> lobby_error() const noexcept { return lobby_error_; }
  // Present exactly on `LOBBY.FULL` and `LOBBY.UNAVAILABLE`: the room the request named, which the
  // encoder publishes as `details.lobby_id`.
  [[nodiscard]] std::optional<std::uint64_t> lobby_id() const noexcept { return lobby_id_; }
  [[nodiscard]] bool requires_session_version_upgrade() const noexcept {
    return session_version_upgrade_required_;
  }

  friend bool operator==(const V3HttpError&, const V3HttpError&) = default;

private:
  struct SessionVersionUpgradeRequired final {};
  explicit V3HttpError(SessionVersionUpgradeRequired) noexcept;
  explicit V3HttpError(HttpError shared_error) noexcept;
  explicit V3HttpError(ForwardedClientReason reason) noexcept;
  V3HttpError(LobbyErrorKind lobby_error, std::optional<std::uint64_t> lobby_id) noexcept;

  std::optional<HttpError> shared_error_;
  std::optional<LobbyErrorKind> lobby_error_;
  std::optional<std::uint64_t> lobby_id_;
  std::optional<ForwardedClientReason> forwarded_client_reason_;
  bool session_version_upgrade_required_ = false;
};

} // namespace blob_royale::protocol

#endif
