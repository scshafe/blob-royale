#include "application_input_error.hpp"
#include "match_configuration.hpp"
#include "match_startup_validation.hpp"

#include "map_definition.hpp"
#include "physics_body.hpp"
#include "protocol_v2_constants.hpp"
#include "server_limits.hpp"
#include "simulation_config.hpp"
#include "vector2.hpp"

#include "application_input_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace blob_royale::application {
namespace {

using test_fixture::require_application_input_error_code;

constexpr double kArenaWidth = 960.0;
constexpr double kArenaHeight = 640.0;

[[nodiscard]] simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(kArenaWidth, kArenaHeight, 10.0,
                                              simulation::SimulationConfig::kRequiredTicksPerSecond,
                                              16, 16);
}

[[nodiscard]] simulation::MapDefinition map_with_static_bodies(const std::size_t body_count) {
  std::vector<simulation::PhysicsBody> static_bodies;
  static_bodies.reserve(body_count);
  for (std::size_t index = 0; index < body_count; ++index) {
    static_bodies.push_back(simulation::PhysicsBody::create_static(
        simulation::Vector2::create(static_cast<double>(index % 900) + 1.0, 1.0)));
  }
  return simulation::MapDefinition::create(
      "budget-arena", simulation::ArenaBounds::create(kArenaWidth, kArenaHeight),
      std::move(static_bodies), {}, simulation::MapMetadata::none());
}

[[nodiscard]] MatchConfiguration match_with_bots(const std::string& roster) {
  return MatchConfiguration::create("royale", "budget-arena", "maps", 1,
                                    MatchConfiguration::parse_bot_roster(roster));
}

} // namespace

TEST_CASE("a match whose worst-case population fits the snapshot bound is accepted",
          "[unit][application][match][validation]") {
  CHECK_NOTHROW(require_match_fits_snapshot_bound(match_with_bots("wanderer:2, chaser:1"),
                                                  map_with_static_bodies(64)));
}

TEST_CASE("a match whose static bodies plus roster ceiling exceed the snapshot bound is rejected",
          "[unit][application][match][validation]") {
  // Every published entity counts: the map's static bodies, one entity a mode may create for
  // itself, every admissible session seat, and every configured bot. The encoder refuses a frame
  // above 1,024 rather than dropping an entity, so a configuration that could reach it would stop
  // publishing to every client at once partway through a match.
  const std::size_t admissible_seats = server::ServerLimits::kConcurrentWebSocketMaximumCount;
  const std::size_t static_body_count = protocol::kSnapshotEntityLimit - admissible_seats - 1;

  CHECK_NOTHROW(require_match_fits_snapshot_bound(match_with_bots(""),
                                                  map_with_static_bodies(static_body_count)));
  require_application_input_error_code(
      [&] {
        require_match_fits_snapshot_bound(match_with_bots("wanderer:1"),
                                          map_with_static_bodies(static_body_count));
      },
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded);
}

TEST_CASE("a map whose arena disagrees with the published world scalars is rejected",
          "[unit][application][match][validation]") {
  // The kernel folds against the map while protocol v1 serves the `[world]` scalars, so two
  // arenas that disagree would draw a client's canvas at one size and simulate at another.
  CHECK_NOTHROW(require_map_matches_published_world(configuration(), map_with_static_bodies(1)));

  const simulation::MapDefinition narrower = simulation::MapDefinition::create(
      "narrow-arena", simulation::ArenaBounds::create(kArenaWidth - 1.0, kArenaHeight), {}, {},
      simulation::MapMetadata::none());
  require_application_input_error_code(
      [&] { require_map_matches_published_world(configuration(), narrower); },
      ApplicationInputErrorCode::kMatchMapBoundsMismatch);
}

} // namespace blob_royale::application
