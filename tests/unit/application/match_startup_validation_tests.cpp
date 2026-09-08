#include "application_input_error.hpp"
#include "match_configuration.hpp"
#include "match_startup_validation.hpp"

#include "shared/hazard_archetype.hpp"
#include "shared/hazard_crossing.hpp"

#include "fixed_delta.hpp"
#include "map_definition.hpp"
#include "physics_body.hpp"
#include "protocol_v2_constants.hpp"
#include "server_limits.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include "application_input_test_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

// One `[hazard.<kind>]` section as a designer would author it. The kind names below appear in no
// `src/` file, which is the same property `application_config_loader_tests.cpp` relies on: a hazard
// kind is configuration, so a test may invent one.
[[nodiscard]] gameplay::HazardArchetype hazard(const std::string_view kind, const double radius,
                                               const double speed, const double interval_seconds) {
  return gameplay::HazardArchetype::create(gameplay::HazardArchetype::Section{
      std::string{kind}, radius, 40.0, 0.2, speed, interval_seconds, true});
}

// The standing-hazard term written out from the plan's own words rather than called out of the
// implementation: "its maximum lifetime in ticks divided by its spawn interval in ticks, rounded
// up, plus one for the one seated this tick", over the longest crossing this arena admits.
//
// It is deliberately a second, independent expression. Calling
// `gameplay::maximum_standing_hazard_count` here would assert that the function equals itself; the
// point is to pin the arithmetic to a formula a reviewer can check against the archetype's own
// documented units.
[[nodiscard]] std::uint64_t expected_standing_count(const double radius, const double speed,
                                                    const double interval_seconds) {
  const double longest_crossing =
      std::sqrt((kArenaWidth * kArenaWidth) + (kArenaHeight * kArenaHeight)) +
      (2.0 * (gameplay::kHazardEntryClearanceRadii * radius));
  const double seconds_per_tick = simulation::FixedDelta::canonical().seconds();
  const auto lifetime_ticks =
      static_cast<std::uint64_t>(std::ceil(longest_crossing / speed / seconds_per_tick));
  const auto interval_ticks = static_cast<std::uint64_t>(
      std::round(interval_seconds * static_cast<double>(simulation::kSimulationTicksPerSecond)));
  return ((lifetime_ticks + interval_ticks - 1) / interval_ticks) + 1;
}

// Everything the worst case counts other than hazards: the admissible session seats and the one
// entity a mode may create for itself, plus whatever the map and roster contribute.
constexpr std::uint64_t kNonHazardBase = server::ServerLimits::kConcurrentWebSocketMaximumCount +
                                         simulation::kSystemCreatedEntityHeadroom;

} // namespace

TEST_CASE("a match whose worst-case population fits the snapshot bound is accepted",
          "[unit][application][match][validation]") {
  // The empty span is the ordinary case and the one this bound had before hazards existed: a
  // configuration declaring no `[hazard.*]` section contributes no term at all.
  CHECK_NOTHROW(require_match_fits_snapshot_bound(match_with_bots("wanderer:2, chaser:1"),
                                                  map_with_static_bodies(64), {}));
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
                                                  map_with_static_bodies(static_body_count), {}));
  require_application_input_error_code(
      [&] {
        require_match_fits_snapshot_bound(match_with_bots("wanderer:1"),
                                          map_with_static_bodies(static_body_count), {});
      },
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded);
}

TEST_CASE("a modest hazard table still fits the snapshot bound",
          "[unit][application][match][validation][hazard]") {
  // Two kinds a designer would plausibly author: a small fast one every second and a bigger slower
  // one every four. Standing populations of a few each, which is what the mechanic is for.
  const std::vector<gameplay::HazardArchetype> table{hazard("plaid_meteorite", 10.0, 200.0, 1.0),
                                                     hazard("velvet_boulder", 26.0, 120.0, 4.0)};

  CHECK_NOTHROW(require_match_fits_snapshot_bound(match_with_bots("wanderer:2, chaser:1"),
                                                  map_with_static_bodies(64), table));
}

TEST_CASE(
    "the standing-hazard term is exactly the lifetime over the interval, rounded up, plus one",
    "[unit][application][match][validation][hazard]") {
  // The boundary, from both sides, through the public function. A term computed one entity too
  // small would accept the second case and a term one too large would reject the first, so this
  // pins the arithmetic rather than merely observing that hazards raise the count.
  constexpr double kRadius = 10.0;
  constexpr double kSpeed = 200.0;
  constexpr double kIntervalSeconds = 1.0;
  const std::vector<gameplay::HazardArchetype> table{
      hazard("plaid_meteorite", kRadius, kSpeed, kIntervalSeconds)};

  const std::uint64_t standing = expected_standing_count(kRadius, kSpeed, kIntervalSeconds);
  REQUIRE(standing > 1);
  REQUIRE(kNonHazardBase + standing < protocol::kSnapshotEntityLimit);
  // `maximum_standing_hazard_count` is what the validator sums, so this is where the independently
  // written expression above meets the shared implementation the spawner also uses.
  CHECK(gameplay::maximum_standing_hazard_count(
            table.front(), simulation::ArenaBounds::create(kArenaWidth, kArenaHeight),
            simulation::FixedDelta::canonical().seconds()) == standing);

  const std::size_t exactly_at_bound = protocol::kSnapshotEntityLimit - kNonHazardBase - standing;
  CHECK_NOTHROW(require_match_fits_snapshot_bound(match_with_bots(""),
                                                  map_with_static_bodies(exactly_at_bound), table));
  require_application_input_error_code(
      [&] {
        require_match_fits_snapshot_bound(match_with_bots(""),
                                          map_with_static_bodies(exactly_at_bound + 1), table);
      },
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded);
}

TEST_CASE("a hazard kind slow enough to fill the arena is refused at startup",
          "[unit][application][match][validation][hazard]") {
  // The failure this check exists to prevent, and the reason it had to grow a third parameter: a
  // hazard crossing at half a world unit a second takes some forty minutes to leave, so one seated
  // every second stacks up thousands deep and the encoder starts refusing frames -- to every client
  // at once, mid-match, with nothing having gone wrong at startup.
  const std::vector<gameplay::HazardArchetype> table{hazard("plaid_meteorite", 10.0, 0.5, 1.0)};
  REQUIRE(expected_standing_count(10.0, 0.5, 1.0) > protocol::kSnapshotEntityLimit);

  require_application_input_error_code(
      [&] {
        require_match_fits_snapshot_bound(match_with_bots(""), map_with_static_bodies(0), table);
      },
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded);
}

TEST_CASE("a hazard kind seated every tick is refused at startup",
          "[unit][application][match][validation][hazard]") {
  // The other half of the product. An ordinary hazard on an interval of one tick seats a new body
  // faster than the old ones leave, so the standing population is the crossing length itself.
  const std::vector<gameplay::HazardArchetype> table{
      hazard("plaid_meteorite", 10.0, 200.0, simulation::FixedDelta::canonical().seconds())};
  REQUIRE(table.front().spawn_interval_ticks() == 1);

  require_application_input_error_code(
      [&] {
        require_match_fits_snapshot_bound(match_with_bots(""), map_with_static_bodies(0), table);
      },
      ApplicationInputErrorCode::kMatchEntityBudgetExceeded);
}

TEST_CASE("the rejection names each hazard kind and how many of it it expects",
          "[unit][application][match][validation][hazard]") {
  // An operator reading the rejection has to be able to tell which `[hazard.<kind>]` knob to turn,
  // which a total alone does not say. The kind names are the section instance names as authored, so
  // the diagnostic can be read straight back onto a line of the configuration file.
  const std::vector<gameplay::HazardArchetype> table{hazard("plaid_meteorite", 10.0, 0.5, 1.0),
                                                     hazard("velvet_boulder", 26.0, 120.0, 4.0)};
  try {
    require_match_fits_snapshot_bound(match_with_bots(""), map_with_static_bodies(0), table);
    FAIL("expected ApplicationInputError");
  } catch (const ApplicationInputError& error) {
    REQUIRE(error.error_code() == ApplicationInputErrorCode::kMatchEntityBudgetExceeded);
    const std::string detail = error.detail();
    CHECK_THAT(detail,
               Catch::Matchers::ContainsSubstring(
                   "plaid_meteorite " + std::to_string(expected_standing_count(10.0, 0.5, 1.0))));
    CHECK_THAT(detail,
               Catch::Matchers::ContainsSubstring(
                   "velvet_boulder " + std::to_string(expected_standing_count(26.0, 120.0, 4.0))));
  }
}

TEST_CASE("a rejection with no hazard table says nothing about hazards",
          "[unit][application][match][validation][hazard]") {
  // A deployment that declares no `[hazard.*]` section reads the same rejection it read before
  // hazards existed, rather than being sent to look for a table that is not there.
  const std::size_t static_body_count = protocol::kSnapshotEntityLimit - kNonHazardBase;
  try {
    require_match_fits_snapshot_bound(match_with_bots(""),
                                      map_with_static_bodies(static_body_count + 1), {});
    FAIL("expected ApplicationInputError");
  } catch (const ApplicationInputError& error) {
    CHECK_THAT(error.detail(), !Catch::Matchers::ContainsSubstring("hazard"));
  }
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
