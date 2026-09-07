#include "match_startup_validation.hpp"

#include "application_input_error.hpp"
#include "protocol_v2_constants.hpp"
#include "runtime_limits.hpp"
#include "server_limits.hpp"

#include <cstdint>
#include <string>

namespace blob_royale::application {

void require_match_fits_snapshot_bound(const MatchConfiguration& match_configuration,
                                       const simulation::MapDefinition& map) {
  const std::uint64_t static_body_count = map.static_bodies().size();
  const std::uint64_t session_seat_count = server::ServerLimits::kConcurrentWebSocketMaximumCount;
  const std::uint64_t bot_count = match_configuration.total_bot_count();
  // The entities a mode's own systems create, which is one per tick by construction
  // (`simulation/simulation_limits.hpp`) and for royale is the zone entity.
  //
  // **This does not yet count standing hazards.** A hazard occupies a seat between the tick it is
  // seated and the tick its `Lifetime` runs out, so a configured `[hazard.*]` table raises the
  // worst case by roughly `crossing_ticks / spawn_interval_ticks` per kind. Both terms are known at
  // startup, but the hazard table is not reachable from here: this function is handed a
  // `MatchConfiguration` and a `MapDefinition`, and the archetypes live on
  // `GameModeConfiguration`. Until that is threaded through, a deployment that authors many or very
  // slow hazard kinds can exceed `kSnapshotEntityLimit` at run time rather than being refused at
  // startup, which is a worse failure than this check exists to prevent and is why it is named here
  // rather than left to be discovered.
  const std::uint64_t mode_created_count = simulation::kSystemCreatedEntityHeadroom;
  const std::uint64_t worst_case_entity_count =
      static_body_count + session_seat_count + bot_count + mode_created_count;

  if (worst_case_entity_count <= protocol::kSnapshotEntityLimit) {
    return;
  }
  throw ApplicationInputError{
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded, "match.entity_budget",
      "map " + std::string{map.name()} + " seats " + std::to_string(static_body_count) +
          " static bodies, the roster seats " + std::to_string(bot_count) + " bots, " +
          std::to_string(session_seat_count) + " session seats are admissible, and a mode may " +
          "create " + std::to_string(mode_created_count) +
          " entity of its own, for a worst case of " + std::to_string(worst_case_entity_count) +
          " published entities past the protocol v2 snapshot bound of " +
          std::to_string(protocol::kSnapshotEntityLimit)};
}

void require_map_matches_published_world(const simulation::SimulationConfig& simulation_config,
                                         const simulation::MapDefinition& map) {
  if (map.bounds().width() == simulation_config.world_width() &&
      map.bounds().height() == simulation_config.world_height()) {
    return;
  }
  throw ApplicationInputError{
      ApplicationInputErrorCode::kMatchMapBoundsMismatch, "match.map_bounds",
      "map " + std::string{map.name()} + " declares a " + std::to_string(map.bounds().width()) +
          " by " + std::to_string(map.bounds().height()) + " arena while [world] publishes a " +
          std::to_string(simulation_config.world_width()) + " by " +
          std::to_string(simulation_config.world_height()) +
          " one; the kernel folds against the map and every client draws the published scalars"};
}

} // namespace blob_royale::application
