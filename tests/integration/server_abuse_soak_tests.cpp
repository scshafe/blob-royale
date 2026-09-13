#include "integration_test_error.hpp"
#include "loopback_http_client.hpp"
#include "protocol_contract_validation.hpp"
#include "server_fixture_state.hpp"
#include "snapshot_websocket_client.hpp"
#include "structured_server_log_observer.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

#include <chrono>
#include <csignal>
#include <cstddef>
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
constexpr std::size_t kConnectionChurnCount = 128;
// Pin the migrated fixture's complete payload independently. Keep the real slow-consumer close,
// healthy-stream checks, receive buffer and observation deadline unchanged.
constexpr std::size_t kExpectedPlayerCount = 256;
constexpr auto kBackpressureObservationDuration = std::chrono::seconds{12};
constexpr auto kReadinessObservationInterval = std::chrono::seconds{3};
constexpr std::size_t kFullSnapshotValidationInterval = 60;
constexpr std::size_t kConstrainedReceiveBufferByteCount = 1'024;

[[nodiscard]] std::filesystem::path parse_fixture_directory(const int argument_count,
                                                            const char* const arguments[]) {
  if (argument_count != 3 || std::string_view{arguments[1]} != "--fixture-directory" ||
      std::string_view{arguments[2]}.empty()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "server_abuse_soak.parse_arguments",
                               "usage: blob_server_abuse_soak --fixture-directory <path>"};
  }
  return std::filesystem::path{arguments[2]};
}

void require_running_server_process(const pid_t server_process_id) {
  if (::kill(server_process_id, 0) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "server_abuse_soak.require_server",
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

void require_readiness(LoopbackHttpClient& http_client, const std::string_view request_id) {
  const IntegrationHttpResponse response =
      http_client.request(http::verb::get, "/api/v1/health/ready", request_id);
  validate_readiness_response(response, request_id);
}

int run_abuse_soak(const int argument_count, const char* const arguments[]) {
  const std::filesystem::path fixture_directory =
      parse_fixture_directory(argument_count, arguments);
  const ServerFixtureState fixture = ServerFixtureState::load(fixture_directory);
  require_running_server_process(fixture.server_process_id());

  LoopbackHttpClient http_client{fixture.port(), kTransportOperationTimeout};
  http_client.exercise_closed_connection_churn(kConnectionChurnCount);
  http_client.require_obsolete_line_fold_rejection("integration.abuse.obs-fold");
  require_readiness(http_client, "integration.abuse.after-churn");

  const std::string allowed_origin = std::string{"http://"}.append(http_client.host_authority());
  constexpr std::string_view kSlowClientRequestId = "integration.abuse.slow-client";
  SnapshotWebSocketClient slow_client{fixture.port(), allowed_origin,
                                      std::string{kSlowClientRequestId},
                                      kTransportOperationTimeout};
  validate_snapshot_handshake(slow_client.handshake_response(), kSlowClientRequestId);
  slow_client.constrain_receive_buffer_for_nonreading_test(kConstrainedReceiveBufferByteCount);

  constexpr std::string_view kHealthyClientRequestId = "integration.abuse.healthy-client";
  SnapshotWebSocketClient healthy_client{fixture.port(), allowed_origin,
                                         std::string{kHealthyClientRequestId},
                                         kTransportOperationTimeout};
  validate_snapshot_handshake(healthy_client.handshake_response(), kHealthyClientRequestId);

  std::uint64_t previous_tick_sequence = 0;
  std::size_t healthy_snapshot_message_count = 0;
  std::size_t fully_validated_snapshot_message_count = 0;
  std::size_t readiness_observation_count = 0;
  const auto observation_deadline =
      std::chrono::steady_clock::now() + kBackpressureObservationDuration;
  auto next_readiness_observation =
      std::chrono::steady_clock::now() + kReadinessObservationInterval;
  while (std::chrono::steady_clock::now() < observation_deadline) {
    ++healthy_snapshot_message_count;
    const bool validate_complete_snapshot =
        healthy_snapshot_message_count == 1 ||
        healthy_snapshot_message_count % kFullSnapshotValidationInterval == 0;
    if (validate_complete_snapshot) {
      const SnapshotContractObservation observation =
          validate_snapshot_message(healthy_client.read_snapshot_message(), kHealthyClientRequestId,
                                    healthy_snapshot_message_count, kExpectedPlayerCount);
      ++fully_validated_snapshot_message_count;
      if (observation.tick_sequence() <= previous_tick_sequence) {
        throw IntegrationTestError{
            IntegrationTestErrorCode::kContractViolation, "server_abuse_soak.healthy_tick_order",
            "healthy snapshots stopped advancing under slow-client pressure"};
      }
      previous_tick_sequence = observation.tick_sequence();
    } else {
      healthy_client.read_and_discard_snapshot_message();
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_readiness_observation) {
      ++readiness_observation_count;
      const std::string request_id = std::string{"integration.abuse.readiness."}.append(
          std::to_string(readiness_observation_count));
      require_readiness(http_client, request_id);
      do {
        next_readiness_observation += kReadinessObservationInterval;
      } while (next_readiness_observation <= now);
    }
  }

  ++healthy_snapshot_message_count;
  const SnapshotContractObservation recovery_observation =
      validate_snapshot_message(healthy_client.read_snapshot_message(), kHealthyClientRequestId,
                                healthy_snapshot_message_count, kExpectedPlayerCount);
  ++fully_validated_snapshot_message_count;
  if (recovery_observation.tick_sequence() <= previous_tick_sequence) {
    throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation,
                               "server_abuse_soak.healthy_recovery",
                               "healthy stream did not advance after the slow peer closed"};
  }
  StructuredServerLogObserver{fixture.server_log_path()}.require_websocket_close(
      kSlowClientRequestId, 1'013, kTransportOperationTimeout);
  require_readiness(http_client, "integration.abuse.final-readiness");
  require_running_server_process(fixture.server_process_id());

  boost::json::object success;
  success.emplace("status", "passed");
  success.emplace("server_process_id", fixture.server_process_id());
  success.emplace("port", fixture.port());
  success.emplace("connection_churn_count", kConnectionChurnCount);
  success.emplace("raw_obsolete_fold_rejections", 1);
  success.emplace("healthy_snapshot_messages", healthy_snapshot_message_count);
  success.emplace("fully_validated_snapshot_messages", fully_validated_snapshot_message_count);
  success.emplace("readiness_observations", readiness_observation_count + 2);
  success.emplace("slow_consumer_close_observations", 1);
  success.emplace("final_tick_sequence", recovery_observation.tick_sequence());
  std::cout << boost::json::serialize(success) << '\n';
  return 0;
}

} // namespace
} // namespace blob_royale::integration_test

int main(const int argument_count, const char* const arguments[]) {
  try {
    return blob_royale::integration_test::run_abuse_soak(argument_count, arguments);
  } catch (const blob_royale::integration_test::IntegrationTestError& error) {
    std::cerr << boost::json::serialize(blob_royale::integration_test::failure_document(error))
              << '\n';
  } catch (const std::exception& error) {
    boost::json::object failure;
    failure.emplace("status", "failed");
    failure.emplace("code", "INTEGRATION.UNEXPECTED_FAILURE");
    failure.emplace("operation", "server_abuse_soak.run");
    failure.emplace("context", error.what());
    std::cerr << boost::json::serialize(failure) << '\n';
  }
  return 1;
}
