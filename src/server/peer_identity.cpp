#include "peer_identity.hpp"

#include "protocol_v3_constants.hpp"
#include "session_welcome.hpp"

#include <boost/asio/ip/address.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::server {
namespace {

inline constexpr std::string_view kForwardedForHeader = "X-Forwarded-For";
inline constexpr std::string_view kTailscaleUserNameHeader = "Tailscale-User-Name";

using HttpRequest = boost::beast::http::request<boost::beast::http::string_body>;

[[nodiscard]] std::size_t header_count(const HttpRequest& request, const std::string_view name) {
  return request.base().count(name);
}

// The proxy sends spaces around a name it means; it never sends a space it means. Only ASCII
// space is trimmed, and only from the ends, exactly as `docs/protocol/v3.md` § "Display names"
// rule 1 spells it. Nothing else about the value is altered.
[[nodiscard]] std::string_view trim_ascii_spaces(std::string_view value) noexcept {
  while (!value.empty() && value.front() == ' ') {
    value.remove_prefix(1);
  }
  while (!value.empty() && value.back() == ' ') {
    value.remove_suffix(1);
  }
  return value;
}

} // namespace

PeerIdentity::PeerIdentity(const PeerClassification classification,
                           std::string accounting_principal,
                           std::optional<std::string> display_name,
                           const DisplayNameOutcome display_name_outcome,
                           const std::size_t received_display_name_length) noexcept
    : classification_(classification), accounting_principal_(std::move(accounting_principal)),
      display_name_(std::move(display_name)), display_name_outcome_(display_name_outcome),
      received_display_name_length_(received_display_name_length) {}

PeerIdentity PeerIdentity::direct(std::string socket_address) {
  return {PeerClassification::kDirect, std::move(socket_address), std::nullopt,
          DisplayNameOutcome::kDirectPeer, 0};
}

PeerIdentity PeerIdentity::proxy_forwarded(std::string forwarded_address,
                                           std::optional<std::string> display_name,
                                           const DisplayNameOutcome display_name_outcome,
                                           const std::size_t received_display_name_length) {
  return {PeerClassification::kProxyForwarded, std::move(forwarded_address),
          std::move(display_name), display_name_outcome, received_display_name_length};
}

std::string PeerIdentity::display_name_for(const std::uint64_t connection_ordinal) const {
  if (display_name_.has_value()) {
    return *display_name_;
  }
  return std::string{protocol::kFallbackDisplayNamePrefix} + std::to_string(connection_ordinal);
}

bool is_canonical_forwarded_client_address(const std::string_view value) noexcept {
  if (value.empty()) {
    return false;
  }
  // A zone identifier, a bracketed literal, and a port are all rejected before parsing, because
  // `make_address` accepts a scope id and would round-trip it, and because a rejected shape must
  // be rejected for a stated reason rather than by accident of the parser's tolerance.
  for (const char character : value) {
    if (character == '%' || character == '[' || character == ']' || character == ' ' ||
        character == '\t') {
      return false;
    }
  }

  boost::system::error_code parse_error;
  const boost::asio::ip::address address = boost::asio::ip::make_address(value, parse_error);
  if (parse_error) {
    return false;
  }
  // The round trip is the canonical-form test: `to_string` emits IPv4 dotted-quad and RFC 5952
  // IPv6, so a value that differs from its own re-serialization is a non-canonical spelling of a
  // real address -- `::0:1`, `0177.0.0.1`, `2001:0DB8::1` -- and is rejected rather than repaired.
  return address.to_string() == value;
}

PeerIdentityResolution derive_peer_identity(const ServerConfig& server_config,
                                            const std::string_view peer_address,
                                            const HttpRequest& request) {
  // Step 1 of `docs/protocol/v3.md` § "Principal derivation", and the only place proxy trust is
  // decided. It is computed from the socket alone, before any header is read, so no header can
  // move a connection into the trusted arm.
  if (!server_config.trusts_proxy_address(peer_address)) {
    return {.identity = PeerIdentity::direct(std::string{peer_address}),
            .forwarded_client_rejection = std::nullopt};
  }

  const std::size_t forwarded_field_count = header_count(request, kForwardedForHeader);
  if (forwarded_field_count == 0) {
    return {.identity = std::nullopt,
            .forwarded_client_rejection = protocol::ForwardedClientReason::kAbsent};
  }
  if (forwarded_field_count > 1) {
    return {.identity = std::nullopt,
            .forwarded_client_rejection = protocol::ForwardedClientReason::kMultipleValues};
  }
  const boost::beast::string_view forwarded_field = request.base().at(kForwardedForHeader);
  const std::string_view forwarded_value{forwarded_field.data(), forwarded_field.size()};
  if (forwarded_value.find(',') != std::string_view::npos) {
    return {.identity = std::nullopt,
            .forwarded_client_rejection = protocol::ForwardedClientReason::kMultipleValues};
  }
  if (!is_canonical_forwarded_client_address(forwarded_value)) {
    return {.identity = std::nullopt,
            .forwarded_client_rejection = protocol::ForwardedClientReason::kNotCanonical};
  }

  // Only `Tailscale-User-Name` is read. `Tailscale-User-Login` is an email address and every
  // display name is published to every other connected peer, so it and every other
  // `Tailscale-User-*` field are deliberately never touched.
  const std::size_t name_field_count = header_count(request, kTailscaleUserNameHeader);
  if (name_field_count == 0) {
    return {.identity = PeerIdentity::proxy_forwarded(std::string{forwarded_value}, std::nullopt,
                                                      DisplayNameOutcome::kAbsent, 0),
            .forwarded_client_rejection = std::nullopt};
  }
  if (name_field_count > 1) {
    return {.identity = PeerIdentity::proxy_forwarded(std::string{forwarded_value}, std::nullopt,
                                                      DisplayNameOutcome::kMultipleValues, 0),
            .forwarded_client_rejection = std::nullopt};
  }

  const boost::beast::string_view name_field = request.base().at(kTailscaleUserNameHeader);
  const std::string_view received_name{name_field.data(), name_field.size()};
  const std::string_view trimmed_name = trim_ascii_spaces(received_name);
  if (!protocol::is_accepted_display_name(trimmed_name)) {
    return {.identity = PeerIdentity::proxy_forwarded(std::string{forwarded_value}, std::nullopt,
                                                      DisplayNameOutcome::kNotAccepted,
                                                      received_name.size()),
            .forwarded_client_rejection = std::nullopt};
  }
  return {.identity = PeerIdentity::proxy_forwarded(
              std::string{forwarded_value}, std::string{trimmed_name},
              DisplayNameOutcome::kProxySupplied, received_name.size()),
          .forwarded_client_rejection = std::nullopt};
}

} // namespace blob_royale::server
