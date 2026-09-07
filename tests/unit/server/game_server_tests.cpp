#include "game_server.hpp"
#include "game_server_error.hpp"
#include "server_test_fixture.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <exception>
#include <future>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace runtime = blob_royale::runtime;

namespace {

[[nodiscard]] std::uint16_t reserve_available_loopback_port() {
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::acceptor acceptor(io_context,
                                          {boost::asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

[[nodiscard]] server::ServerConfig server_config(const std::uint16_t port) {
  return server::ServerConfig::create("127.0.0.1", port, 30, fixture::kWorldWidth,
                                      fixture::kWorldHeight, fixture::kPlayerRadius,
                                      {"localhost", "127.0.0.1"}, {}, {});
}

} // namespace

TEST_CASE("GameServer treats stop-before-run as one graceful single-use lifecycle",
          "[unit][server][lifecycle]") {
  const runtime::SnapshotPublication publication = fixture::initial_publication();
  fixture::LogCapture log_capture;
  fixture::MatchSessionFixture match_session;
  server::GameServer game_server(server_config(reserve_available_loopback_port()), publication,
                                 match_session.context(), log_capture.logger);

  game_server.stop();
  game_server.stop();
  game_server.run();

  CHECK(game_server.state() == server::GameServerState::kStopped);
  CHECK_THROWS_AS(game_server.run(), server::GameServerError);
  CHECK(log_capture.lifecycle_sequence() ==
        std::vector<std::pair<std::string, std::string>>{{"server.ready", "ready"},
                                                         {"server.stopped", "stopped"}});
  CHECK_FALSE(log_capture.contains_event("server.starting"));
  CHECK_FALSE(log_capture.contains_event("server.running"));
}

TEST_CASE("GameServer stop wins the race after running is visible and before accept settles",
          "[unit][server][lifecycle]") {
  for (int repetition = 0; repetition < 8; ++repetition) {
    const runtime::SnapshotPublication publication = fixture::initial_publication();
    fixture::LogCapture log_capture;
    fixture::MatchSessionFixture match_session;
    server::GameServer game_server(server_config(reserve_available_loopback_port()), publication,
                                   match_session.context(), log_capture.logger);
    std::promise<void> completed;
    std::future<void> completion = completed.get_future();
    std::exception_ptr run_failure;
    std::thread server_thread([&] {
      try {
        game_server.run();
      } catch (...) {
        run_failure = std::current_exception();
      }
      completed.set_value();
    });

    const bool reached_running =
        game_server.wait_for_state(server::GameServerState::kRunning, std::chrono::seconds{5});
    game_server.stop();
    const bool completed_in_time =
        completion.wait_for(std::chrono::seconds{6}) == std::future_status::ready;
    server_thread.join();

    REQUIRE(reached_running);
    REQUIRE(completed_in_time);
    CHECK(run_failure == nullptr);
    CHECK(game_server.state() == server::GameServerState::kStopped);
    CHECK(log_capture.lifecycle_sequence() == std::vector<std::pair<std::string, std::string>>{
                                                  {"server.ready", "ready"},
                                                  {"server.starting", "starting"},
                                                  {"server.running", "running"},
                                                  {"server.stopping", "stopping"},
                                                  {"server.stopped", "stopped"},
                                              });
  }
}

TEST_CASE("GameServer retains and rethrows an exact listener bind failure",
          "[unit][server][lifecycle][failure]") {
  boost::asio::io_context occupied_io_context;
  boost::asio::ip::tcp::acceptor occupied_acceptor(occupied_io_context,
                                                   {boost::asio::ip::make_address("127.0.0.1"), 0});
  const std::uint16_t occupied_port = occupied_acceptor.local_endpoint().port();
  const runtime::SnapshotPublication publication = fixture::initial_publication();
  fixture::LogCapture log_capture;
  fixture::MatchSessionFixture match_session;
  server::GameServer game_server(server_config(occupied_port), publication, match_session.context(),
                                 log_capture.logger);

  CHECK_THROWS_AS(game_server.run(), server::GameServerError);
  CHECK(game_server.state() == server::GameServerState::kFailed);
  CHECK(log_capture.lifecycle_sequence() == std::vector<std::pair<std::string, std::string>>{
                                                {"server.ready", "ready"},
                                                {"server.starting", "starting"},
                                                {"server.failed", "failed"},
                                            });
  CHECK_FALSE(log_capture.contains_event("server.running"));
  try {
    game_server.rethrow_if_failed();
    FAIL("expected retained GameServerError");
  } catch (const server::GameServerError& error) {
    CHECK(error.error_code() == server::GameServerErrorCode::kListenerBindFailed);
  }
}
