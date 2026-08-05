#include "server_config.hpp"
#include "server_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;

static_assert(!noexcept(server::normalize_host_authority({}, 80)));
static_assert(!noexcept(server::normalize_serialized_origin({})));

namespace {

template <typename Action>
void require_config_error(Action&& action, const server::ServerConfigValidationCode expected_code) {
  try {
    std::forward<Action>(action)();
  } catch (const server::ServerConfigValidationError& error) {
    REQUIRE(error.validation_code() == expected_code);
    return;
  }
  FAIL("expected ServerConfigValidationError");
}

} // namespace

TEST_CASE("ServerConfig canonicalizes exact host and Origin allowlists once",
          "[unit][server][config]") {
  const server::ServerConfig config =
      fixture::loopback_server_config({"LOCALHOST", "127.0.0.1:8000", "[::1]"},
                                      {"HTTPS://GAME.EXAMPLE.TEST", "http://localhost:5173"});

  REQUIRE(config.allowed_hosts().size() == 3);
  CHECK(config.allows_host("127.0.0.1:8000"));
  CHECK(config.allows_host("localhost:8000"));
  CHECK(config.allows_host("[::1]:8000"));
  CHECK(config.allows_origin("https://game.example.test:443"));
  CHECK(config.allows_origin("http://localhost:5173"));
  CHECK(config.public_configuration().snapshots_per_second() == 30);
}

TEST_CASE("Host normalization binds an omitted request port to the listener authority",
          "[unit][server][config]") {
  CHECK(server::normalize_host_authority("LOCALHOST", 8000) == "localhost:8000");
  CHECK(server::normalize_host_authority("localhost:443", 8000) == "localhost:443");
  CHECK(server::normalize_host_authority("[::1]", 8000) == "[::1]:8000");
}

TEST_CASE("Origin normalization supplies the scheme default port and rejects URL components",
          "[unit][server][config]") {
  CHECK(server::normalize_serialized_origin("HTTPS://GAME.EXAMPLE.TEST") ==
        "https://game.example.test:443");
  CHECK(server::normalize_serialized_origin("http://localhost") == "http://localhost:80");
  CHECK(server::normalize_serialized_origin("https://game.example.test/path").empty());
  CHECK(server::normalize_serialized_origin("https://user@game.example.test").empty());
  CHECK(server::normalize_serialized_origin("null").empty());
}

TEST_CASE("ServerConfig rejects public bind addresses", "[unit][server][config]") {
  require_config_error(
      [] {
        static_cast<void>(server::ServerConfig::create(
            "8.8.8.8", 8000, 30, fixture::kWorldWidth, fixture::kWorldHeight,
            fixture::kPlayerRadius, {"game.example.test:443"}, {"https://game.example.test"},
            {"10.0.0.2"}));
      },
      server::ServerConfigValidationCode::kBindAddressInvalid);
}

TEST_CASE("ServerConfig requires explicit proxy and Origin policy for private binding",
          "[unit][server][config]") {
  require_config_error(
      [] {
        static_cast<void>(server::ServerConfig::create(
            "0.0.0.0", 8000, 30, fixture::kWorldWidth, fixture::kWorldHeight,
            fixture::kPlayerRadius, {"game.example.test:443"}, {}, {}));
      },
      server::ServerConfigValidationCode::kNonLoopbackPolicyInvalid);
}

TEST_CASE("ServerConfig accepts a private listener only behind exact trusted proxy policy",
          "[unit][server][config]") {
  const server::ServerConfig config = server::ServerConfig::create(
      "0.0.0.0", 8000, 30, fixture::kWorldWidth, fixture::kWorldHeight, fixture::kPlayerRadius,
      {"game.example.test:443"}, {"https://game.example.test"}, {"10.0.0.2"});

  CHECK_FALSE(config.binds_loopback());
  CHECK(config.trusts_proxy_address("10.0.0.2"));
}

TEST_CASE("ServerConfig rejects duplicate canonical host entries", "[unit][server][config]") {
  require_config_error(
      [] { static_cast<void>(fixture::loopback_server_config({"LOCALHOST", "localhost:8000"})); },
      server::ServerConfigValidationCode::kHostAllowlistInvalid);
}

TEST_CASE("ServerConfig rejects wildcard and malformed allowlist entries",
          "[unit][server][config]") {
  require_config_error(
      [] { static_cast<void>(fixture::loopback_server_config({"*.example.test"})); },
      server::ServerConfigValidationCode::kHostAllowlistInvalid);
  require_config_error(
      [] {
        static_cast<void>(
            fixture::loopback_server_config({"localhost"}, {"https://game.example.test?query"}));
      },
      server::ServerConfigValidationCode::kOriginAllowlistInvalid);
}

TEST_CASE("ServerConfig enforces allowlist count and aggregate byte bounds",
          "[unit][server][config]") {
  std::vector<std::string> too_many_hosts;
  for (std::size_t index = 0;
       index < server::ServerLimits::kConfigurationAllowlistMaximumEntryCount + 1; ++index) {
    too_many_hosts.push_back("host" + std::to_string(index) + ".example.test");
  }
  require_config_error(
      [&too_many_hosts] { static_cast<void>(fixture::loopback_server_config(too_many_hosts)); },
      server::ServerConfigValidationCode::kHostAllowlistInvalid);
}

TEST_CASE("ServerConfig rejects protocol-invalid public world geometry", "[unit][server][config]") {
  require_config_error(
      [] {
        static_cast<void>(server::ServerConfig::create(
            "127.0.0.1", 8000, 30, 20.0, fixture::kWorldHeight, 10.0, {"localhost"}, {}, {}));
      },
      server::ServerConfigValidationCode::kPublicConfigurationInvalid);
}
