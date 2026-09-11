#include "peer_identity.hpp"
#include "server_test_fixture.hpp"

#include "v3_http_error.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace protocol = blob_royale::protocol;
namespace http = boost::beast::http;

namespace {

constexpr std::string_view kProxyAddress = "127.0.0.1";
constexpr std::string_view kForwardedForHeader = "X-Forwarded-For";
constexpr std::string_view kTailscaleUserNameHeader = "Tailscale-User-Name";

[[nodiscard]] server::ServerConfig config_trusting_loopback_proxy() {
  return fixture::loopback_server_config({"127.0.0.1", "localhost", "[::1]"},
                                         {"https://game.example.test"},
                                         {std::string{kProxyAddress}});
}

[[nodiscard]] server::GameApiHttpRequest forwarded_request(const std::string_view forwarded_for) {
  server::GameApiHttpRequest request =
      fixture::request(http::verb::get, "/api/v3/lobbies/1/session");
  request.set(kForwardedForHeader, forwarded_for);
  return request;
}

} // namespace

TEST_CASE("derive_peer_identity classifies a peer outside the trusted set as direct",
          "[unit][server][identity][trust-boundary]") {
  const server::ServerConfig config = fixture::loopback_server_config();
  server::GameApiHttpRequest request = forwarded_request("100.101.102.103");
  request.set(kTailscaleUserNameHeader, "Cole Shaffer");

  const server::PeerIdentityResolution resolution =
      server::derive_peer_identity(config, "127.0.0.1", request);

  REQUIRE(resolution.accepted());
  CHECK(resolution.identity->classification() == server::PeerClassification::kDirect);
  CHECK(resolution.identity->is_direct_peer());
  // Every caller-supplied identity field is ignored entirely rather than validated and preferred.
  CHECK(resolution.identity->accounting_principal() == "127.0.0.1");
  CHECK(resolution.identity->display_name_outcome() == server::DisplayNameOutcome::kDirectPeer);
  CHECK(resolution.identity->display_name_for(7) == "player-7");
}

TEST_CASE("derive_peer_identity classifies a trusted loopback proxy as proxy-forwarded",
          "[unit][server][identity][trust-boundary]") {
  // The precedence rule: the deployed proxy *is* loopback, so a classification that consulted
  // loopback first would put the deployed configuration in the direct arm and grant it the
  // direct-peer relaxations.
  const server::ServerConfig config = config_trusting_loopback_proxy();
  const server::PeerIdentityResolution resolution =
      server::derive_peer_identity(config, kProxyAddress, forwarded_request("100.101.102.103"));

  REQUIRE(resolution.accepted());
  CHECK(resolution.identity->classification() == server::PeerClassification::kProxyForwarded);
  CHECK_FALSE(resolution.identity->is_direct_peer());
  CHECK(resolution.identity->accounting_principal() == "100.101.102.103");
}

TEST_CASE("derive_peer_identity rejects an absent forwarded client address",
          "[unit][server][identity][trust-boundary]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  const server::PeerIdentityResolution resolution = server::derive_peer_identity(
      config, kProxyAddress, fixture::request(http::verb::get, "/api/v3/lobbies/1/session"));

  CHECK_FALSE(resolution.accepted());
  REQUIRE(resolution.forwarded_client_rejection.has_value());
  CHECK(*resolution.forwarded_client_rejection == protocol::ForwardedClientReason::kAbsent);
}

TEST_CASE("derive_peer_identity rejects a repeated forwarded client field",
          "[unit][server][identity][trust-boundary]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  server::GameApiHttpRequest request = forwarded_request("100.101.102.103");
  request.insert(kForwardedForHeader, "100.101.102.104");

  const server::PeerIdentityResolution resolution =
      server::derive_peer_identity(config, kProxyAddress, request);

  CHECK_FALSE(resolution.accepted());
  REQUIRE(resolution.forwarded_client_rejection.has_value());
  CHECK(*resolution.forwarded_client_rejection == protocol::ForwardedClientReason::kMultipleValues);
}

TEST_CASE("derive_peer_identity rejects a comma-bearing forwarded client list",
          "[unit][server][identity][trust-boundary]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  const server::PeerIdentityResolution resolution = server::derive_peer_identity(
      config, kProxyAddress, forwarded_request("100.101.102.103, 10.0.0.1"));

  CHECK_FALSE(resolution.accepted());
  REQUIRE(resolution.forwarded_client_rejection.has_value());
  CHECK(*resolution.forwarded_client_rejection == protocol::ForwardedClientReason::kMultipleValues);
}

TEST_CASE("derive_peer_identity rejects every non-canonical forwarded client spelling",
          "[unit][server][identity][trust-boundary]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  // Surrounding whitespace is absent here because HTTP field parsing strips optional whitespace
  // before a field value is ever read; the accept/reject rule for it is asserted directly against
  // `is_canonical_forwarded_client_address` below.
  for (const std::string_view value :
       {"100.101.102.103:443", "[2001:db8::1]", "2001:0DB8::1", "2001:db8:0:0:0:0:0:1",
        "fe80::1%eth0", "0177.0.0.1", "not-an-address", "::0:1", "100.101.102.103."}) {
    const server::PeerIdentityResolution resolution =
        server::derive_peer_identity(config, kProxyAddress, forwarded_request(value));
    INFO("forwarded value " << value);
    CHECK_FALSE(resolution.accepted());
    REQUIRE(resolution.forwarded_client_rejection.has_value());
    CHECK(*resolution.forwarded_client_rejection == protocol::ForwardedClientReason::kNotCanonical);
  }
}

TEST_CASE("is_canonical_forwarded_client_address accepts exactly the canonical forms",
          "[unit][server][identity]") {
  CHECK(server::is_canonical_forwarded_client_address("100.101.102.103"));
  CHECK(server::is_canonical_forwarded_client_address("::1"));
  CHECK(server::is_canonical_forwarded_client_address("2001:db8::1"));
  CHECK_FALSE(server::is_canonical_forwarded_client_address(""));
  CHECK_FALSE(server::is_canonical_forwarded_client_address("2001:DB8::1"));
  // The value is accepted or rejected; it is never normalized, trimmed, or repaired.
  CHECK_FALSE(server::is_canonical_forwarded_client_address(" 100.101.102.103"));
  CHECK_FALSE(server::is_canonical_forwarded_client_address("100.101.102.103 "));
  CHECK_FALSE(server::is_canonical_forwarded_client_address("100.101.102.103\t"));
}

TEST_CASE("derive_peer_identity publishes a byte-exact accepted proxy display name",
          "[unit][server][identity]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  server::GameApiHttpRequest request = forwarded_request("100.101.102.103");
  request.set(kTailscaleUserNameHeader, "  Cole Shaffer  ");

  const server::PeerIdentityResolution resolution =
      server::derive_peer_identity(config, kProxyAddress, request);

  REQUIRE(resolution.accepted());
  CHECK(resolution.identity->display_name_outcome() == server::DisplayNameOutcome::kProxySupplied);
  // Only surrounding ASCII spaces are trimmed. No character inside the value is substituted.
  CHECK(resolution.identity->display_name_for(9) == "Cole Shaffer");
}

TEST_CASE("derive_peer_identity falls back for every unaccepted proxy display name",
          "[unit][server][identity]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  const std::vector<std::string> unaccepted{"",
                                            "-leading",
                                            "trailing-",
                                            "sn\x7f"
                                            "ake",
                                            std::string(65, 'a'),
                                            "caf\xc3\xa9",
                                            "new\nline"};
  for (const std::string& name : unaccepted) {
    server::GameApiHttpRequest request = forwarded_request("100.101.102.103");
    request.set(kTailscaleUserNameHeader, name);
    const server::PeerIdentityResolution resolution =
        server::derive_peer_identity(config, kProxyAddress, request);
    INFO("display name length " << name.size());
    REQUIRE(resolution.accepted());
    CHECK(resolution.identity->display_name_outcome() == server::DisplayNameOutcome::kNotAccepted);
    CHECK(resolution.identity->received_display_name_length() == name.size());
    CHECK(resolution.identity->display_name_for(3) == "player-3");
  }
}

TEST_CASE("derive_peer_identity falls back for an absent or repeated display name field",
          "[unit][server][identity]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  const server::PeerIdentityResolution absent =
      server::derive_peer_identity(config, kProxyAddress, forwarded_request("100.101.102.103"));
  REQUIRE(absent.accepted());
  CHECK(absent.identity->display_name_outcome() == server::DisplayNameOutcome::kAbsent);

  server::GameApiHttpRequest repeated = forwarded_request("100.101.102.103");
  repeated.set(kTailscaleUserNameHeader, "Cole Shaffer");
  repeated.insert(kTailscaleUserNameHeader, "Someone Else");
  const server::PeerIdentityResolution resolution =
      server::derive_peer_identity(config, kProxyAddress, repeated);
  REQUIRE(resolution.accepted());
  CHECK(resolution.identity->display_name_outcome() == server::DisplayNameOutcome::kMultipleValues);
  CHECK(resolution.identity->display_name_for(4) == "player-4");
}

TEST_CASE("derive_peer_identity never reads a Tailscale identity field other than the name",
          "[unit][server][identity][trust-boundary]") {
  const server::ServerConfig config = config_trusting_loopback_proxy();
  server::GameApiHttpRequest request = forwarded_request("100.101.102.103");
  request.set("Tailscale-User-Login", "cole@example.test");
  request.set("Tailscale-User-ProfilePic", "https://example.test/pic.png");

  const server::PeerIdentityResolution resolution =
      server::derive_peer_identity(config, kProxyAddress, request);

  REQUIRE(resolution.accepted());
  // The login is an email address and every display name is published to every peer, so the
  // fallback must be used rather than any other Tailscale-User-* field.
  CHECK(resolution.identity->display_name_outcome() == server::DisplayNameOutcome::kAbsent);
  CHECK(resolution.identity->display_name_for(11) == "player-11");
}
