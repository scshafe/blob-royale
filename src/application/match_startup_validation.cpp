#include "match_startup_validation.hpp"

#include "application_input_error.hpp"
#include "protocol_v2_constants.hpp"
#include "runtime_limits.hpp"
#include "server_limits.hpp"
#include "shared/hazard_crossing.hpp"

#include "command_registry.hpp"
#include "fixed_delta.hpp"
#include "seat_roster.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace blob_royale::application {
namespace {

// The hazard table's contribution to the worst case, and the operator-facing account of where it
// came from. The two are built in one pass because a rejection that says "hazards add 900" without
// saying which kind is responsible names no knob to turn, and a second pass over the table to
// build the text could disagree with the first about the arithmetic.
struct HazardBudget final {
  std::uint64_t standing_count{};
  std::string description;
};

[[nodiscard]] HazardBudget
hazard_budget(const std::span<const gameplay::HazardArchetype> hazard_archetypes,
              const simulation::MapDefinition& map) {
  HazardBudget budget;
  for (const gameplay::HazardArchetype& archetype : hazard_archetypes) {
    // The **only** implementation of how long a hazard lives and how far one can travel is
    // `gameplay/shared/hazard_crossing.hpp`, and `hazard_spawn_system` calls the same two functions
    // at the distance it actually drew. A copy of that arithmetic here would be a bound that agreed
    // with the spawner until one of the two was edited, which is the failure this check exists to
    // prevent rather than to reproduce.
    //
    // The tick rate is a compile-time constant of the deterministic core and `TickContext` hands
    // the spawner exactly this value, so a startup bound and a running tick cannot disagree about
    // how long a second is.
    const std::uint64_t standing = gameplay::maximum_standing_hazard_count(
        archetype, map.bounds(), simulation::FixedDelta::canonical().seconds());
    budget.standing_count += standing;
    budget.description += (budget.description.empty() ? "" : ", ") + archetype.kind_name() + " " +
                          std::to_string(standing);
  }
  return budget;
}

} // namespace

void require_match_fits_snapshot_bound(
    const MatchConfiguration& match_configuration, const simulation::MapDefinition& map,
    const std::span<const gameplay::HazardArchetype> hazard_archetypes) {
  const std::uint64_t static_body_count = map.static_bodies().size();
  const std::uint64_t session_seat_count = server::ServerLimits::kConcurrentWebSocketMaximumCount;
  const std::uint64_t bot_count = match_configuration.total_bot_count();
  // The entities a mode's own systems create, which is one per tick by construction
  // (`simulation/simulation_limits.hpp`) and for royale is the zone entity.
  const std::uint64_t mode_created_count = simulation::kSystemCreatedEntityHeadroom;
  // Every hazard that can be standing at the same moment. A hazard occupies a seat between the tick
  // it is seated and the tick its `Lifetime` runs out, so a configured `[hazard.*]` table raises
  // the worst case by `ceil(longest_lifetime_ticks / spawn_interval_ticks) + 1` per kind -- both
  // terms known before the first tick, which is what makes an authored table a startup rejection
  // rather than a match that quietly stops publishing once enough comets are in the air at once.
  const HazardBudget hazards = hazard_budget(hazard_archetypes, map);
  const std::uint64_t worst_case_entity_count = static_body_count + session_seat_count + bot_count +
                                                mode_created_count + hazards.standing_count;

  if (worst_case_entity_count <= protocol::kSnapshotEntityLimit) {
    return;
  }
  // The hazard clause is omitted entirely when no kind is declared, rather than reported as zero:
  // a deployment with no `[hazard.*]` section reads the rejection it read before hazards existed,
  // byte for byte, and nothing sends its author looking for a table that is not there. It is
  // spliced in ahead of the mode's own term and carries the list's `and` with it, so the
  // conjunction stays on the last item in both shapes rather than stranding itself mid-sentence.
  const std::string hazard_clause =
      hazard_archetypes.empty()
          ? " and"
          : (" the hazard table can stand " + std::to_string(hazards.standing_count) +
             " hazards at once (" + hazards.description + "), and");
  throw ApplicationInputError{
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded, "match.entity_budget",
      "map " + std::string{map.name()} + " seats " + std::to_string(static_body_count) +
          " static bodies, the roster seats " + std::to_string(bot_count) + " bots, " +
          std::to_string(session_seat_count) + " session seats are admissible," + hazard_clause +
          " a mode may create " + std::to_string(mode_created_count) +
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

simulation::SeatRoster
initial_seat_roster_for(const simulation::GameMode& mode, const std::uint64_t lobby_seat_count,
                        const std::span<const MatchConfiguration::BotRosterEntry> bot_roster) {
  if (!mode.accepted_command_kinds().contains(simulation::CommandKind::kStartMatch)) {
    return simulation::SeatRoster{};
  }
  simulation::SeatRoster roster =
      simulation::SeatRoster::of_size(static_cast<std::size_t>(lobby_seat_count));
  std::size_t next_seat = 0;
  for (const MatchConfiguration::BotRosterEntry& entry : bot_roster) {
    for (std::uint64_t ordinal = 0; ordinal < entry.count; ++ordinal) {
      if (next_seat >= roster.seat_count()) {
        throw ApplicationInputError{
            ApplicationInputErrorCode::kMatchBotsExceedSeats, "match.bots",
            "[match] bots declares more bots than the " + std::to_string(roster.seat_count()) +
                " seats of [royale] lobby_seat_count, so the field could never be seated in full"};
      }
      // The kind was checked against the controller registry when the roster was parsed, and the
      // registry's names satisfy the published kind-name grammar by construction
      // (`match_session_context.cpp`), so this cannot throw for a loaded configuration.
      roster.assign_seat(
          next_seat, simulation::Seat{simulation::NpcSeat{
                         simulation::SeatKindName::create(entry.controller_kind), std::nullopt}});
      ++next_seat;
    }
  }
  return roster;
}

} // namespace blob_royale::application
