#include "integration_test_error.hpp"
#include "loopback_http_client.hpp"
#include "protocol_contract_validation.hpp"
#include "server_fixture_state.hpp"
#include "snapshot_websocket_client.hpp"
#include "structured_server_log_observer.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace blob_royale::integration_test {
namespace {

namespace http = boost::beast::http;

constexpr auto kTransportOperationTimeout = std::chrono::seconds{3};

[[nodiscard]] std::filesystem::path parse_fixture_directory(const int argument_count,
                                                            const char* const arguments[]) {
  if (argument_count != 3 || std::string_view{arguments[1]} != "--fixture-directory" ||
      std::string_view{arguments[2]}.empty()) {
    throw IntegrationTestError{
        IntegrationTestErrorCode::kArgumentInvalid, "server_contracts.parse_arguments",
        "usage: blob_server_protocol_integration --fixture-directory <path>"};
  }
  return std::filesystem::path{arguments[2]};
}

void require_running_server_process(const pid_t server_process_id) {
  if (::kill(server_process_id, 0) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_contracts.require_server",
                               "fixture server process is not running"};
  }
}

[[nodiscard]] boost::json::object failure_document(const IntegrationTestError& error) {
  boost::json::object failure;
  failure.emplace("status", "failed");
  failure.emplace("code", error.code());
  failure.emplace("operation", error.operation());
  failure.emplace("context", error.context());
  return failure;
}

int run_contracts(const int argument_count, const char* const arguments[]) {
  const std::filesystem::path fixture_directory =
      parse_fixture_directory(argument_count, arguments);
  const ServerFixtureState fixture = ServerFixtureState::load(fixture_directory);
  require_running_server_process(fixture.server_process_id());

  LoopbackHttpClient http_client{fixture.port(), kTransportOperationTimeout};
  const std::string allowed_origin = std::string{"http://"}.append(http_client.host_authority());

  constexpr std::string_view kConfigurationRequestId = "integration.config";
  const IntegrationHttpResponse configuration_response = http_client.request(
      http::verb::get, "/api/v1/config", kConfigurationRequestId, allowed_origin);
  validate_configuration_response(configuration_response, kConfigurationRequestId, allowed_origin);

  constexpr std::string_view kReadinessRequestId = "integration.readiness";
  const IntegrationHttpResponse readiness_response =
      http_client.request(http::verb::get, "/api/v1/health/ready", kReadinessRequestId);
  validate_readiness_response(readiness_response, kReadinessRequestId);

  constexpr std::string_view kMethodRequestId = "integration.wrong-method";
  const IntegrationHttpResponse method_response =
      http_client.request(http::verb::post, "/api/v1/config", kMethodRequestId);
  validate_error_response(method_response, http::status::method_not_allowed,
                          "PROTOCOL.METHOD_NOT_ALLOWED", kMethodRequestId);
  validate_method_not_allowed_details(method_response);

  constexpr std::string_view kPathRequestId = "integration.wrong-path";
  const IntegrationHttpResponse path_response =
      http_client.request(http::verb::get, "/api/v1/config?alias=true", kPathRequestId);
  validate_error_response(path_response, http::status::not_found, "PROTOCOL.ROUTE_NOT_FOUND",
                          kPathRequestId);

  constexpr std::string_view kOriginRequestId = "integration.wrong-origin";
  const IntegrationHttpResponse origin_response = http_client.request(
      http::verb::get, "/api/v1/config", kOriginRequestId, "https://rejected.example");
  validate_error_response(origin_response, http::status::forbidden, "PROTOCOL.ORIGIN_REJECTED",
                          kOriginRequestId);

  constexpr std::string_view kUpgradeRequestId = "integration.upgrade-required";
  const IntegrationHttpResponse upgrade_response =
      http_client.request(http::verb::get, "/api/v1/snapshots", kUpgradeRequestId);
  validate_error_response(upgrade_response, http::status::upgrade_required,
                          "PROTOCOL.UPGRADE_REQUIRED", kUpgradeRequestId);
  validate_upgrade_required_header(upgrade_response);

  constexpr std::string_view kSnapshotRequestId = "integration.snapshots";
  SnapshotWebSocketClient websocket_client{
      fixture.port(), allowed_origin, std::string{kSnapshotRequestId}, kTransportOperationTimeout};
  validate_snapshot_handshake(websocket_client.handshake_response(), kSnapshotRequestId);
  const SnapshotContractObservation first_snapshot =
      validate_snapshot_message(websocket_client.read_snapshot_message(), kSnapshotRequestId, 1);
  const SnapshotContractObservation second_snapshot =
      validate_snapshot_message(websocket_client.read_snapshot_message(), kSnapshotRequestId, 2);
  if (second_snapshot.tick_sequence() <= first_snapshot.tick_sequence()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                               "server_contracts.snapshot_monotonicity",
                               "successive complete snapshots did not advance the committed tick"};
  }

  constexpr std::string_view kPeerCloseRequestId = "integration.peer-close";
  constexpr std::uint16_t kPeerApplicationCloseCode = 4'001;
  SnapshotWebSocketClient peer_close_client{
      fixture.port(), allowed_origin, std::string{kPeerCloseRequestId}, kTransportOperationTimeout};
  validate_snapshot_handshake(peer_close_client.handshake_response(), kPeerCloseRequestId);
  static_cast<void>(peer_close_client.read_snapshot_message());
  peer_close_client.close_with_application_code(kPeerApplicationCloseCode);
  StructuredServerLogObserver{fixture.server_log_path()}.require_websocket_close(
      kPeerCloseRequestId, kPeerApplicationCloseCode, kTransportOperationTimeout);

  websocket_client.send_forbidden_client_message_and_require_policy_close(
      R"({"acceleration":{"x":1,"y":0}})");
  require_running_server_process(fixture.server_process_id());

  boost::json::object success;
  success.emplace("status", "passed");
  success.emplace("server_process_id", fixture.server_process_id());
  success.emplace("port", fixture.port());
  success.emplace("first_tick_sequence", first_snapshot.tick_sequence());
  success.emplace("second_tick_sequence", second_snapshot.tick_sequence());
  success.emplace("validated_http_contracts", 6);
  success.emplace("validated_snapshot_messages", 2);
  success.emplace("validated_peer_close_observations", 1);
  std::cout << boost::json::serialize(success) << '\n';
  return 0;
}

} // namespace
} // namespace blob_royale::integration_test

int main(const int argument_count, const char* const arguments[]) {
  try {
    return blob_royale::integration_test::run_contracts(argument_count, arguments);
  } catch (const blob_royale::integration_test::IntegrationTestError& error) {
    std::cerr << boost::json::serialize(blob_royale::integration_test::failure_document(error))
              << '\n';
  } catch (const std::exception& error) {
    boost::json::object failure;
    failure.emplace("status", "failed");
    failure.emplace("code", "INTEGRATION.UNEXPECTED_FAILURE");
    failure.emplace("operation", "server_contracts.run");
    failure.emplace("context", error.what());
    std::cerr << boost::json::serialize(failure) << '\n';
  }
  return 1;
}
