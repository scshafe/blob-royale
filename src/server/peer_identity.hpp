#ifndef BLOB_ROYALE_SERVER_PEER_IDENTITY_HPP
#define BLOB_ROYALE_SERVER_PEER_IDENTITY_HPP

#include "server_config.hpp"
#include "v3_http_error.hpp"

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::server {

// How the boundary classified one connection's socket peer.
//
// **Trusted-proxy membership is tested first and the answer is total**, which is the precedence
// rule protocol v3 adds to v1's rule 7: on the accepted deployment the proxy *is* loopback, so a
// classification that consulted loopback first would grant every proxy-forwarded connection the
// direct-peer Origin relaxation in exactly the configuration that ships
// (`docs/protocol/v3.md` § "Upgrade validation deltas"). Making the two exclusive arms of one
// enumeration is what keeps that from being a rule someone has to remember.
enum class PeerClassification : std::uint8_t {
  kProxyForwarded = 0,
  kDirect = 1,
};

[[nodiscard]] constexpr std::string_view
peer_classification_name(const PeerClassification classification) noexcept {
  switch (classification) {
  case PeerClassification::kProxyForwarded:
    return "proxy_forwarded";
  case PeerClassification::kDirect:
    return "direct";
  }
  return "peer_classification_invalid";
}

// Why one connection's display name is what it is.
//
// Only `kProxySupplied` publishes a byte a proxy sent; every other value publishes the generated
// fallback. The value is a closed enumeration because it is logged and must never carry a byte of
// the received header (`docs/protocol/v3.md` § "Display names" rule 2).
enum class DisplayNameOutcome : std::uint8_t {
  // A trusted proxy supplied one field whose trimmed value matches the accepted grammar exactly.
  kProxySupplied = 0,
  // A direct peer: every caller-supplied identity field is ignored, including this one.
  kDirectPeer = 1,
  kAbsent = 2,
  kMultipleValues = 3,
  // Present once and not accepted: a byte outside 0x20..0x7E, an unaccepted character, an empty
  // or over-long value, or one that does not begin and end alphanumeric.
  kNotAccepted = 4,
};

[[nodiscard]] constexpr std::string_view
display_name_outcome_name(const DisplayNameOutcome outcome) noexcept {
  switch (outcome) {
  case DisplayNameOutcome::kProxySupplied:
    return "proxy_supplied";
  case DisplayNameOutcome::kDirectPeer:
    return "direct_peer";
  case DisplayNameOutcome::kAbsent:
    return "absent";
  case DisplayNameOutcome::kMultipleValues:
    return "multiple_values";
  case DisplayNameOutcome::kNotAccepted:
    return "not_accepted";
  }
  return "display_name_outcome_invalid";
}

// canonical: peer_identity -- the one derivation of a connection's accounting principal and
// display name.
//
// **The tailnet authenticates; this does not.** What the application derives is one accounting
// principal and one display name per request, by a total function of the socket peer address and
// two header fields, before any route runs (`docs/protocol/v3.md` § "Identity"). Neither value
// authorizes anything: the principal is a rate-accounting key and the name is a label.
//
// **Headers are credited only from a socket peer in `trusted_proxy_addresses`.** For a direct
// peer every caller-supplied identity field -- `Forwarded`, `X-Forwarded-For`, `X-Real-IP`,
// `Tailscale-User-Name`, and every other -- is ignored entirely rather than validated and
// preferred, so header presence is never proof of anything.
//
// **The asymmetry between the two rules is deliberate.** A malformed forwarded address is fatal
// because it is a security control; a malformed name is a fallback because it is a label. Neither
// is a credential, and no character of a name is ever substituted -- a sanitizer that replaced
// characters would turn one real name into a different real-looking name, which is worse than a
// visible `player-7`.
// related: docs/protocol/v3.md -- the accepted rules this implements verbatim.
// related: peer_traffic_policy.hpp -- what the principal keys.
// related: session_websocket_session.hpp -- what the display name is published through.
class PeerIdentity final {
public:
  // A direct connection: the canonical socket address is the principal and the name is the
  // fallback. `PeerTrafficPolicy` applies v1's loopback collapse to the principal, so this value
  // carries the socket address exactly as v1 has always accounted it.
  [[nodiscard]] static PeerIdentity direct(std::string socket_address);

  // A proxy-forwarded connection whose forwarded address was accepted. `display_name` is present
  // only for `DisplayNameOutcome::kProxySupplied`.
  [[nodiscard]] static PeerIdentity proxy_forwarded(std::string forwarded_address,
                                                    std::optional<std::string> display_name,
                                                    DisplayNameOutcome display_name_outcome,
                                                    std::size_t received_display_name_length);

  PeerIdentity(const PeerIdentity&) = default;
  PeerIdentity(PeerIdentity&&) noexcept = default;
  PeerIdentity& operator=(const PeerIdentity&) = default;
  PeerIdentity& operator=(PeerIdentity&&) noexcept = default;
  ~PeerIdentity() = default;

  [[nodiscard]] PeerClassification classification() const noexcept { return classification_; }

  // Whether the direct-peer relaxations apply to this connection. It is `classification()` phrased
  // as the question every caller actually asks, so no caller re-derives the precedence rule.
  [[nodiscard]] bool is_direct_peer() const noexcept {
    return classification_ == PeerClassification::kDirect;
  }

  // The key for every connection, request, upgrade, and rate bound on this connection, in both
  // protocol versions.
  [[nodiscard]] const std::string& accounting_principal() const& noexcept {
    return accounting_principal_;
  }
  [[nodiscard]] const std::string& accounting_principal() const&& = delete;

  [[nodiscard]] DisplayNameOutcome display_name_outcome() const noexcept {
    return display_name_outcome_;
  }

  // The length of the received `Tailscale-User-Name` value, which is the only thing about a
  // rejected name that may be logged. Zero when the field was absent or the peer was direct.
  [[nodiscard]] std::size_t received_display_name_length() const noexcept {
    return received_display_name_length_;
  }

  // The name this connection publishes: a byte-exact copy of the trusted value, or the generated
  // fallback `player-<connection_ordinal>`. Never a substitution of the received bytes.
  //
  // **The fallback is keyed on the server's own monotonic connection ordinal, and the reason is a
  // dependency cycle rather than a preference.** `docs/protocol/v3.md` § "Display names" spells the
  // fallback `player-<entity_id>`. A published name is fixed exactly once, at
  // `runtime::CommandSink::open_session`, which registers the directory entry in the same call
  // that issues the `ControllerId` -- and `runtime::ControllerDirectory` refuses a second
  // registration precisely so a later frame cannot rename a player. At that instant neither
  // candidate id exists: the `ControllerId` is this call's own return value, and the `EntityId`
  // does not exist until a later tick applies the spawn that the `ControllerId` is required to
  // submit. Entity id to name to directory entry to controller id to spawn to entity id is a
  // cycle, and it closes only with a one-time display-name seal on `ControllerDirectory`, which is
  // a `blob_runtime` change. The ordinal is monotonic, never reused, unique per connection, and
  // satisfies the same `display_name` grammar, so the published label is still obviously generated,
  // bounded, and non-colliding.
  [[nodiscard]] std::string display_name_for(std::uint64_t connection_ordinal) const;

  friend bool operator==(const PeerIdentity&, const PeerIdentity&) = default;

private:
  PeerIdentity(PeerClassification classification, std::string accounting_principal,
               std::optional<std::string> display_name, DisplayNameOutcome display_name_outcome,
               std::size_t received_display_name_length) noexcept;

  PeerClassification classification_;
  std::string accounting_principal_;
  std::optional<std::string> display_name_;
  DisplayNameOutcome display_name_outcome_;
  std::size_t received_display_name_length_;
};

// The total answer to "who is this connection": one identity, or one closed rejection reason.
//
// Exactly one member is engaged. A rejection is `400` on both protocol versions -- v3 names it
// `PROTOCOL.INVALID_FORWARDED_CLIENT` with the reason as a detail, and v1, whose closed code
// registry v3 may not widen, names it `PROTOCOL.INVALID_REQUEST` with the same closed reason as
// its `reason` detail.
struct PeerIdentityResolution final {
  std::optional<PeerIdentity> identity;
  std::optional<protocol::ForwardedClientReason> forwarded_client_rejection;

  [[nodiscard]] bool accepted() const noexcept { return identity.has_value(); }
};

// canonical: peer_identity_derivation -- the only place proxy trust is decided.
//
// Total and never throws. Classifies the socket peer against `trusted_proxy_addresses` first,
// then derives the principal and the display-name outcome under the rules of that classification
// and no other.
[[nodiscard]] PeerIdentityResolution
derive_peer_identity(const ServerConfig& server_config, std::string_view peer_address,
                     const boost::beast::http::request<boost::beast::http::string_body>& request);

// Whether one `X-Forwarded-For` value is exactly one address in canonical textual form: IPv4
// dotted-quad, or IPv6 in RFC 5952 canonical form, with no port, no brackets, no zone identifier,
// no list, and no surrounding whitespace. Exposed because it is the security control the identity
// rules turn on and it is tested directly against every rejected shape.
//
// The value is accepted or rejected; it is never normalized, trimmed, or repaired.
[[nodiscard]] bool is_canonical_forwarded_client_address(std::string_view value) noexcept;

} // namespace blob_royale::server

#endif
