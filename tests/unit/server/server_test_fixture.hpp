#ifndef BLOB_ROYALE_TESTS_UNIT_SERVER_SERVER_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_SERVER_SERVER_TEST_FIXTURE_HPP

#include "command_kind_mask.hpp"
#include "entity_id.hpp"
#include "game_api_router.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "lobby_directory.hpp"
#include "match_session_context.hpp"
#include "physics_body.hpp"
#include "server_config.hpp"
#include "simulation_config.hpp"
#include "simulation_runtime.hpp"
#include "snapshot_publication.hpp"
#include "structured_log_capture.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::server::test_fixture {

inline constexpr std::uint16_t kServerPort = 8'000;
inline constexpr double kWorldWidth = 960.0;
inline constexpr double kWorldHeight = 640.0;
inline constexpr double kPlayerRadius = 10.0;

using LogCapture = test_support::StructuredLogCapture;

[[nodiscard]] inline ServerConfig
loopback_server_config(std::vector<std::string> allowed_hosts = {"127.0.0.1", "localhost", "[::1]"},
                       std::vector<std::string> allowed_origins = {"http://localhost:5173",
                                                                   "https://game.example.test"},
                       std::vector<std::string> trusted_proxy_addresses = {}) {
  return ServerConfig::create("127.0.0.1", kServerPort, 30, kWorldWidth, kWorldHeight,
                              kPlayerRadius, std::move(allowed_hosts), std::move(allowed_origins),
                              std::move(trusted_proxy_addresses));
}

[[nodiscard]] inline simulation::SimulationConfig simulation_config() {
  return simulation::SimulationConfig::create(kWorldWidth, kWorldHeight, kPlayerRadius, 400, 16,
                                              16);
}

[[nodiscard]] inline simulation::GameSimulation game_simulation() {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  const simulation::GameWorld::EntitySeed player = simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(1),
      simulation::PhysicsBody::create(simulation::Vector2::create(200.0, 200.0), zero, zero));
  return simulation::GameSimulation::create(simulation_config(),
                                            simulation::GameWorld::create({player}));
}

[[nodiscard]] inline simulation::WorldSnapshot snapshot_after_steps(const std::size_t step_count) {
  simulation::GameSimulation simulation = game_simulation();
  for (std::size_t step = 0; step < step_count; ++step) {
    simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return simulation.snapshot();
}

[[nodiscard]] inline runtime::SnapshotPublication initial_publication() {
  return runtime::SnapshotPublication(game_simulation().snapshot());
}

inline constexpr std::string_view kFixtureMapName = "arena-960x640";
// The seat ceiling the fixture's welcome publishes; the harness world has no map markers to
// count, so it is the shipped arena's 32.
inline constexpr std::uint64_t kFixtureSeatCountMaximum = 32;

// Everything `/api/v2/session` requires, owned for the life of one test.
//
// The runtime is constructed and never started, which is enough: `CommandSink` and
// `ControllerDirectory` are fully usable before the first tick, and a test that started the worker
// would be asserting against a moving world. The capability is required rather than optional
// because the server serves both protocol versions unconditionally.
class MatchSessionFixture final {
public:
  MatchSessionFixture() : simulation_runtime_(game_simulation()) {}

  MatchSessionFixture(const MatchSessionFixture&) = delete;
  MatchSessionFixture(MatchSessionFixture&&) = delete;
  MatchSessionFixture& operator=(const MatchSessionFixture&) = delete;
  MatchSessionFixture& operator=(MatchSessionFixture&&) = delete;
  ~MatchSessionFixture() = default;

  [[nodiscard]] MatchSessionContext context(const std::uint64_t lobby_id = 1) const {
    return MatchSessionContext::create(
        lobby_id, simulation_runtime_.command_sink(), simulation_runtime_.controller_directory(),
        std::string{kFixtureMapName}, kFixtureSeatCountMaximum, simulation::CommandKindMask::all(),
        std::vector<std::string>{"wanderer", "chaser"});
  }

  [[nodiscard]] runtime::CommandSink& command_sink() const noexcept {
    return simulation_runtime_.command_sink();
  }
  [[nodiscard]] const runtime::ControllerDirectory& controller_directory() const noexcept {
    return simulation_runtime_.controller_directory();
  }

private:
  mutable runtime::SimulationRuntime simulation_runtime_;
};

// The one-room directory every single-match harness serves: what the server received before rooms
// existed, in the shape it receives now.
[[nodiscard]] inline LobbyDirectory single_lobby(const runtime::SnapshotPublication& publication,
                                                 MatchSessionContext context) {
  std::vector<LobbyDirectory::Room> rooms;
  rooms.push_back(
      {.lobby_id = 1, .snapshot_publication = publication, .match_session = std::move(context)});
  return LobbyDirectory::create(std::move(rooms));
}

[[nodiscard]] inline GameApiHttpRequest request(const boost::beast::http::verb method,
                                                const std::string_view target,
                                                const std::string_view host = "localhost",
                                                const unsigned version = 11) {
  GameApiHttpRequest result{method, target, version};
  result.set(boost::beast::http::field::host, host);
  result.set("X-Request-ID", "server-test-request-1");
  return result;
}

[[nodiscard]] inline bool response_contains(const GameApiHttpResponse& response,
                                            const std::string_view text) {
  return response.body().find(text) != std::string::npos;
}

} // namespace blob_royale::server::test_fixture

#endif
