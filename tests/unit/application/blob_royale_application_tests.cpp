#include "application_config.hpp"
#include "application_input_error.hpp"
#include "application_lifecycle_error.hpp"
#include "blob_royale_application.hpp"
#include "entity_id.hpp"
#include "game_mode_configuration.hpp"
#include "game_server_error.hpp"
#include "game_world.hpp"
#include "lobbies_configuration.hpp"
#include "map_definition.hpp"
#include "match_configuration.hpp"
#include "physics_body.hpp"
#include "server_config.hpp"
#include "simulation_config.hpp"
#include "simulation_validation_error.hpp"
#include "structured_log_capture.hpp"
#include "structured_logger.hpp"
#include "vector2.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

using namespace std::chrono_literals;

constexpr double kWorldWidth = 100.0;
constexpr double kWorldHeight = 80.0;
constexpr double kPlayerRadius = 1.0;
constexpr std::uint64_t kSnapshotsPerSecond = 30;
constexpr std::uint64_t kGridColumns = 10;
constexpr std::uint64_t kGridRows = 8;
constexpr std::uint16_t kUnboundConstructionPort = 8'000;
constexpr std::string_view kMapName = "fixture-arena";
constexpr std::uint64_t kMatchSeed = 7;
constexpr auto kTestDeadline = 5s;

using LogCapture = test_support::StructuredLogCapture;

class OccupiedLoopbackPort final {
public:
  OccupiedLoopbackPort() : acceptor_(io_context_) {
    acceptor_.open(boost::asio::ip::tcp::v4());
    acceptor_.bind(boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0});
    acceptor_.listen();
  }

  OccupiedLoopbackPort(const OccupiedLoopbackPort&) = delete;
  OccupiedLoopbackPort(OccupiedLoopbackPort&&) = delete;
  OccupiedLoopbackPort& operator=(const OccupiedLoopbackPort&) = delete;
  OccupiedLoopbackPort& operator=(OccupiedLoopbackPort&&) = delete;
  ~OccupiedLoopbackPort() = default;

  [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

private:
  boost::asio::io_context io_context_{1};
  boost::asio::ip::tcp::acceptor acceptor_;
};

[[nodiscard]] simulation::SimulationConfig simulation_config_fixture() {
  return simulation::SimulationConfig::create(kWorldWidth, kWorldHeight, kPlayerRadius,
                                              simulation::SimulationConfig::kRequiredTicksPerSecond,
                                              kGridColumns, kGridRows);
}

[[nodiscard]] server::ServerConfig server_config_fixture(const std::uint16_t port) {
  std::string allowed_host{"127.0.0.1:"};
  allowed_host.append(std::to_string(port));
  return server::ServerConfig::create("127.0.0.1", port, kSnapshotsPerSecond, kWorldWidth,
                                      kWorldHeight, kPlayerRadius, {std::move(allowed_host)}, {},
                                      {});
}

// The `[match]` section every fixture runs: `sandbox` on a bare arena of the fixture's own bounds,
// with the roster the individual test wants. Sandbox rather than royale because these tests are
// about process lifecycle and a shrinking zone would change the world under them.
[[nodiscard]] MatchConfiguration
match_configuration_fixture(std::vector<MatchConfiguration::BotRosterEntry> bot_roster = {}) {
  return MatchConfiguration::create("sandbox", std::string{kMapName}, "maps", kMatchSeed, 2,
                                    std::move(bot_roster));
}

// A map that satisfies sandbox's `validate_map` -- at least one spawn point -- on exactly the
// arena the fixture's world scalars publish, so `require_map_matches_published_world` passes.
[[nodiscard]] simulation::MapDefinition map_fixture() {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.push_back(
      simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(30.0, 40.0)));
  markers.push_back(
      simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(70.0, 40.0)));
  return simulation::MapDefinition::create(
      std::string{kMapName}, simulation::ArenaBounds::create(kWorldWidth, kWorldHeight), {},
      std::move(markers), simulation::MapMetadata::none());
}

[[nodiscard]] ApplicationConfig
application_config_fixture(const std::uint16_t port,
                           std::vector<MatchConfiguration::BotRosterEntry> bot_roster = {}) {
  return ApplicationConfig::create(server_config_fixture(port), simulation_config_fixture(),
                                   match_configuration_fixture(std::move(bot_roster)),
                                   gameplay::GameModeConfiguration::defaults(),
                                   LobbiesConfiguration::create(1));
}

[[nodiscard]] simulation::GameWorld empty_world_fixture() {
  return simulation::GameWorld::create(simulation_config_fixture(), map_fixture(), kMatchSeed);
}

void require_bind_failure(BlobRoyaleApplication& application) {
  try {
    application.run();
  } catch (const server::GameServerError& error) {
    REQUIRE(error.error_code() == server::GameServerErrorCode::kListenerBindFailed);
    return;
  }
  FAIL("expected the occupied listener port to produce GameServerError");
}

} // namespace

TEST_CASE("BlobRoyaleApplication is a non-transferable RAII composition root",
          "[unit][application][lifecycle]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<BlobRoyaleApplication>);
  STATIC_CHECK_FALSE(std::is_move_constructible_v<BlobRoyaleApplication>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<BlobRoyaleApplication>);
  STATIC_CHECK_FALSE(std::is_move_assignable_v<BlobRoyaleApplication>);
  STATIC_CHECK(std::is_nothrow_destructible_v<BlobRoyaleApplication>);

  LogCapture log_capture;
  BlobRoyaleApplication application =
      BlobRoyaleApplication::create(application_config_fixture(kUnboundConstructionPort),
                                    map_fixture(), empty_world_fixture(), log_capture.logger);
  static_cast<void>(application);
}

TEST_CASE("BlobRoyaleApplication factory builds and validates the owned GameSimulation",
          "[unit][application][lifecycle]") {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  const simulation::GameWorld::EntitySeed outside_player =
      simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(1),
          simulation::PhysicsBody::create(simulation::Vector2::create(0.0, 20.0), zero, zero));
  simulation::GameWorld invalid_world = simulation::GameWorld::create({outside_player});
  LogCapture log_capture;

  try {
    static_cast<void>(
        BlobRoyaleApplication::create(application_config_fixture(kUnboundConstructionPort),
                                      map_fixture(), std::move(invalid_world), log_capture.logger));
  } catch (const simulation::SimulationValidationError& error) {
    REQUIRE(error.validation_code() ==
            simulation::SimulationValidationCode::kSpatialGridPlayerCenterOutOfBounds);
    return;
  }
  FAIL("expected BlobRoyaleApplication construction to validate its initial world");
}

TEST_CASE("BlobRoyaleApplication seats one hosted controller per configured bot",
          "[unit][application][lifecycle][controllers]") {
  // A bot opens a session through the same `CommandSink` a browser will, so the roster is seated as
  // part of construction rather than as a step a caller could forget.
  LogCapture log_capture;
  BlobRoyaleApplication application = BlobRoyaleApplication::create(
      application_config_fixture(kUnboundConstructionPort,
                                 {MatchConfiguration::BotRosterEntry{"wanderer", 2},
                                  MatchConfiguration::BotRosterEntry{"chaser", 1}}),
      map_fixture(), empty_world_fixture(), log_capture.logger);
  static_cast<void>(application);

  const std::optional<test_support::CapturedStructuredLogEvent> seated =
      log_capture.find_event("controllers.roster_seated");
  REQUIRE(seated.has_value());
  REQUIRE(seated->detail.has_value());
  CHECK(seated->detail->find("hosted_controller_count=3") != std::string::npos);
  CHECK(seated->detail->find("match_seed=" + std::to_string(kMatchSeed)) != std::string::npos);
}

TEST_CASE("BlobRoyaleApplication seats nothing and logs nothing for an empty roster",
          "[unit][application][lifecycle][controllers]") {
  LogCapture log_capture;
  BlobRoyaleApplication application =
      BlobRoyaleApplication::create(application_config_fixture(kUnboundConstructionPort),
                                    map_fixture(), empty_world_fixture(), log_capture.logger);
  static_cast<void>(application);

  CHECK_FALSE(log_capture.contains_event("controllers.roster_seated"));
}

TEST_CASE("BlobRoyaleApplication rejects a map whose arena disagrees with the published world",
          "[unit][application][lifecycle][match][validation]") {
  LogCapture log_capture;
  const simulation::MapDefinition narrower = simulation::MapDefinition::create(
      std::string{kMapName}, simulation::ArenaBounds::create(kWorldWidth - 1.0, kWorldHeight), {},
      {simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(30.0, 40.0))},
      simulation::MapMetadata::none());

  try {
    static_cast<void>(
        BlobRoyaleApplication::create(application_config_fixture(kUnboundConstructionPort),
                                      narrower, empty_world_fixture(), log_capture.logger));
  } catch (const ApplicationInputError& error) {
    CHECK(error.error_code() == ApplicationInputErrorCode::kMatchMapBoundsMismatch);
    return;
  }
  FAIL("expected the composition root to reject a map that disagrees with [world]");
}

TEST_CASE("BlobRoyaleApplication propagates an immediate retained server bind failure",
          "[unit][application][lifecycle][failure]") {
  OccupiedLoopbackPort occupied_port;
  LogCapture log_capture;
  BlobRoyaleApplication application =
      BlobRoyaleApplication::create(application_config_fixture(occupied_port.port()), map_fixture(),
                                    empty_world_fixture(), log_capture.logger);

  require_bind_failure(application);

  CHECK(log_capture.lifecycle_sequence() == std::vector<std::pair<std::string, std::string>>{
                                                {"server.ready", "ready"},
                                                {"application.starting", "starting"},
                                                {"server.starting", "starting"},
                                                {"server.failed", "failed"},
                                                {"application.failed", "failed"},
                                            });
  CHECK_FALSE(log_capture.contains_event("application.running"));
}

TEST_CASE("BlobRoyaleApplication rejects a second run with one typed lifecycle error",
          "[unit][application][lifecycle][failure]") {
  OccupiedLoopbackPort occupied_port;
  LogCapture log_capture;
  BlobRoyaleApplication application =
      BlobRoyaleApplication::create(application_config_fixture(occupied_port.port()), map_fixture(),
                                    empty_world_fixture(), log_capture.logger);
  require_bind_failure(application);

  try {
    application.run();
  } catch (const ApplicationLifecycleError& error) {
    CHECK(error.error_code() == ApplicationLifecycleErrorCode::kRunAlreadyInvoked);
    CHECK(error.code() == "APPLICATION.LIFECYCLE.RUN_ALREADY_INVOKED");
    return;
  }
  FAIL("expected a second application run to produce ApplicationLifecycleError");
}

TEST_CASE("BlobRoyaleApplication destruction joins its ready runtime worker",
          "[unit][application][lifecycle][concurrency]") {
  std::binary_semaphore destruction_completed{0};
  std::atomic<bool> construction_completed{false};
  std::jthread owner([&] {
    try {
      LogCapture log_capture;
      {
        BlobRoyaleApplication application =
            BlobRoyaleApplication::create(application_config_fixture(kUnboundConstructionPort),
                                          map_fixture(), empty_world_fixture(), log_capture.logger);
        static_cast<void>(application);
      }
      construction_completed.store(true, std::memory_order_release);
    } catch (...) {
      // The assertion below reports any unexpected construction or destruction failure.
    }
    destruction_completed.release();
  });

  REQUIRE(destruction_completed.try_acquire_for(kTestDeadline));
  owner.join();
  CHECK(construction_completed.load(std::memory_order_acquire));
}

TEST_CASE("BlobRoyaleApplication leaves no server thread after a run failure",
          "[unit][application][lifecycle][failure][concurrency]") {
  OccupiedLoopbackPort occupied_port;
  std::binary_semaphore destruction_completed{0};
  std::atomic<bool> retained_bind_failure_observed{false};
  std::jthread owner([&] {
    try {
      LogCapture log_capture;
      BlobRoyaleApplication application =
          BlobRoyaleApplication::create(application_config_fixture(occupied_port.port()),
                                        map_fixture(), empty_world_fixture(), log_capture.logger);
      application.run();
    } catch (const server::GameServerError& error) {
      retained_bind_failure_observed.store(error.error_code() ==
                                               server::GameServerErrorCode::kListenerBindFailed,
                                           std::memory_order_release);
    } catch (...) {
      // The assertion below reports an unexpected failure type without escaping the test thread.
    }
    destruction_completed.release();
  });

  REQUIRE(destruction_completed.try_acquire_for(kTestDeadline));
  owner.join();
  CHECK(retained_bind_failure_observed.load(std::memory_order_acquire));
}

} // namespace blob_royale::application
