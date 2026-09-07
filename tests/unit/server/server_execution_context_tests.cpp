#include "server_execution_context.hpp"
#include "server_test_fixture.hpp"

#include <boost/asio/io_context.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace runtime = blob_royale::runtime;

namespace {

struct ContextFixture final {
  fixture::LogCapture log_capture;
  boost::asio::io_context io_context;
  runtime::SnapshotPublication publication{fixture::game_simulation().snapshot()};
  fixture::MatchSessionFixture match_session;
  std::shared_ptr<server::ServerExecutionContext> context =
      std::make_shared<server::ServerExecutionContext>(
          io_context, fixture::loopback_server_config(), publication, match_session.context(),
          log_capture.logger);
};

} // namespace

TEST_CASE("ServerExecutionContext safely drains callbacks that unregister during graceful stop",
          "[unit][server][shutdown][ownership]") {
  ContextFixture fixture_state;
  std::array<std::shared_ptr<server::ServerExecutionContext::SessionId>, 3> session_ids;
  std::size_t invoked_count = 0;
  for (auto& session_id : session_ids) {
    session_id = std::make_shared<server::ServerExecutionContext::SessionId>(0);
    *session_id = fixture_state.context->register_session(
        [context = fixture_state.context, session_id,
         &invoked_count](const server::SessionStopMode mode) {
          CHECK(mode == server::SessionStopMode::kGraceful);
          ++invoked_count;
          context->unregister_session(*session_id);
        });
  }

  fixture_state.context->request_stop();
  fixture_state.io_context.run();

  CHECK(invoked_count == session_ids.size());
  CHECK(fixture_state.context->stopping());
}

TEST_CASE("ServerExecutionContext safely closes multiple self-removing sessions on hard failure",
          "[unit][server][shutdown][failure][ownership]") {
  ContextFixture fixture_state;
  std::array<std::shared_ptr<server::ServerExecutionContext::SessionId>, 4> session_ids;
  std::size_t invoked_count = 0;
  for (auto& session_id : session_ids) {
    session_id = std::make_shared<server::ServerExecutionContext::SessionId>(0);
    *session_id = fixture_state.context->register_session(
        [context = fixture_state.context, session_id,
         &invoked_count](const server::SessionStopMode mode) {
          CHECK(mode == server::SessionStopMode::kImmediate);
          ++invoked_count;
          context->unregister_session(*session_id);
        });
  }
  const std::exception_ptr expected_failure =
      std::make_exception_ptr(std::runtime_error{"expected test failure"});

  fixture_state.context->fail(expected_failure);
  fixture_state.io_context.run();

  CHECK(invoked_count == session_ids.size());
  CHECK(fixture_state.context->failure() == expected_failure);
}
