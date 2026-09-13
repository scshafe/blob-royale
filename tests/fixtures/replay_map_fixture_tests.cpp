#include "hill_motion_replay_fixture.hpp"
#include "replay_fixture.hpp"

// Reuse the existing isolated filesystem fixture instead of another temporary-directory owner.
#include "../unit/application/application_input_test_fixture.hpp"
#include "gameplay_validation_error.hpp"
#include "map_loader.hpp"
#include "terrain_definition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace application = blob_royale::application;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

struct ReplayMapCase final {
  std::string_view replay;
  std::string_view map;
  double width;
  double height;
  std::size_t marker_count;
  bool corridor_ground;
  std::size_t static_body_count;
  std::size_t hole_count;
};

// Historical identities, bounds and marker counts stay frozen from before Step 6; newer
// ability/chronology maps declare their own counts. Every old empty terrain/body count stays zero.
constexpr std::array kReplayMaps{
    ReplayMapCase{"hill-contested", "hill_contested_arena", 960.0, 640.0, 3, false, 0, 0},
    ReplayMapCase{"hill-scripted-match", "hill_scripted_arena", 960.0, 640.0, 4, false, 0, 0},
    ReplayMapCase{"hill-threshold", "hill_threshold_arena", 960.0, 640.0, 3, false, 0, 0},
    ReplayMapCase{"hill-time-limit-draw", "hill_draw_arena", 960.0, 640.0, 3, false, 0, 0},
    ReplayMapCase{"race-charge-fall", "race_charge_fall", 960.0, 640.0, 5, true, 0, 1},
    ReplayMapCase{"race-charge-finish", "race_charge_finish", 960.0, 640.0, 4, true, 1, 0},
    ReplayMapCase{"race-finish-window", "race_finish_window", 960.0, 640.0, 5, true, 0, 0},
    ReplayMapCase{"race-off-track-return", "race_off_track_return", 960.0, 640.0, 4, true, 0, 0},
    ReplayMapCase{"race-scripted-course", "race_scripted_course", 960.0, 640.0, 4, true, 0, 0},
    ReplayMapCase{"race-shared-finish", "race_shared_finish", 960.0, 640.0, 5, true, 0, 0},
    ReplayMapCase{"race-time-limit", "race_time_limit", 960.0, 640.0, 5, true, 0, 0},
    ReplayMapCase{"royale-charge-burst", "royale_charge_arena", 960.0, 640.0, 2, false, 0, 0},
    ReplayMapCase{"royale-charge-contact", "royale_charge_contact", 960.0, 640.0, 2, false, 0, 0},
    ReplayMapCase{"royale-charge-hole", "royale_charge_hole", 960.0, 640.0, 2, false, 0, 1},
    ReplayMapCase{"royale-drag-decay", "royale_drag_corridor", 40000.0, 4000.0, 3, false, 0, 0},
    ReplayMapCase{"royale-elimination-timing", "royale_boundary_arena", 960.0, 640.0, 3, false, 0,
                  0},
    ReplayMapCase{"royale-scripted-match", "royale_scripted_arena", 960.0, 640.0, 4, false, 0, 0},
    ReplayMapCase{"royale-shield-boundaries", "royale_shield_boundaries", 40000.0, 4000.0, 6, false,
                  0, 0},
    ReplayMapCase{"royale-shield-parry", "royale_shield_arena", 960.0, 640.0, 2, false, 0, 0},
    ReplayMapCase{"royale-simultaneous-draw", "royale_draw_arena", 960.0, 640.0, 4, false, 0, 0},
    ReplayMapCase{"royale-spawn-order", "royale_spawn_arena", 960.0, 640.0, 4, false, 0, 0},
    ReplayMapCase{"royale-thrust-integration", "royale_thrust_arena", 960.0, 640.0, 3, false, 0, 0},
    ReplayMapCase{"royale-transition-per-tick", "royale_cycle_arena", 960.0, 640.0, 1, false, 0, 0},
};

} // namespace

TEST_CASE("every replay uses its canonical bundled map without legacy track markers",
          "[fixtures][replay][map][terrain]") {
  for (const auto& expected : kReplayMaps) {
    INFO(expected.replay);
    const auto fixture = testing::ReplayFixture::named(std::string(expected.replay));
    const auto directory = std::filesystem::path{BLOB_ROYALE_REPLAY_FIXTURE_DIRECTORY} /
                           expected.replay / "maps" / expected.map;
    const auto loaded = application::MapLoader::load(directory);
    CHECK(fixture.map() == loaded);
    CHECK(loaded.name() == expected.map);
    CHECK(loaded.bounds().width() == expected.width);
    CHECK(loaded.bounds().height() == expected.height);
    CHECK(loaded.markers().size() == expected.marker_count);
    CHECK(loaded.static_bodies().size() == expected.static_body_count);
    CHECK(loaded.terrain().holes().size() == expected.hole_count);
    CHECK(loaded.terrain().ground() == (expected.corridor_ground
                                            ? simulation::TerrainGround::kCorridors
                                            : simulation::TerrainGround::kSolid));
    CHECK(loaded.terrain().corridors().size() ==
          (expected.corridor_ground ? std::size_t{1} : std::size_t{0}));
    for (const auto& marker : loaded.markers()) {
      CHECK(marker.kind != "track");
      CHECK_FALSE(marker.team.has_value());
    }
    if (expected.corridor_ground) {
      REQUIRE(loaded.terrain().find_corridor(fixture.race().road()) != nullptr);
    }
  }
}

TEST_CASE("replay map references reject absolute and nonleaf paths before map loading",
          "[fixtures][replay][map][validation]") {
  application::test_fixture::TemporaryApplicationInputWorkspace workspace;
  constexpr std::array invalid_references{".",          "..",          "/tmp/road", "../road",
                                          "child/road", "child\\road", "road/",     "C:/road"};
  for (const std::string_view reference : invalid_references) {
    INFO(reference);
    std::string match{"[match]\nmode=royale\nseed=1\ntick_count=1\nlobby_seat_count=1\nmap="};
    match.append(reference);
    match.push_back('\n');
    const auto path = workspace.write_file("match.ini", match);
    CHECK_THROWS_AS(testing::ReplayFixture::load(path.parent_path()), testing::ReplayFixtureError);
    CHECK_THROWS_WITH(testing::ReplayFixture::load(path.parent_path()),
                      Catch::Matchers::ContainsSubstring("map must be one map-name leaf"));
  }
}

TEST_CASE("every accepted hill replay explicitly retains marker tour and its roam configuration",
          "[fixtures][replay][hill_motion][authoring]") {
  for (const auto name : testing::hill_motion_fixture::kLegacyReplays) {
    CAPTURE(name);
    const auto fixture = testing::ReplayFixture::named(name);
    CHECK(fixture.king_of_the_hill().motion_policy() ==
          blob_royale::gameplay::KingOfTheHillConfiguration::HillMotionPolicy::kMarkerTour);
    CHECK(fixture.king_of_the_hill().hill_speed_minimum() == 20.0);
    CHECK(fixture.king_of_the_hill().hill_speed_maximum() == 70.0);
    CHECK(fixture.king_of_the_hill().hill_retarget_minimum_ticks() == 140);
    CHECK(fixture.king_of_the_hill().hill_retarget_maximum_ticks() == 480);
  }
}

TEST_CASE("hill replay reader requires explicit motion keys and refuses an unknown policy",
          "[fixtures][replay][hill_motion][validation]") {
  application::test_fixture::TemporaryApplicationInputWorkspace workspace;
  const auto copy = testing::hill_motion_fixture::copy_scripted_replay(workspace);
  for (const std::string_view field : application::hill_motion_fixture::kRequiredFields) {
    CAPTURE(field);
    static_cast<void>(
        workspace.write_file(testing::hill_motion_fixture::kCopiedMatch,
                             application::test_fixture::replace_once(copy.match, field, "")));
    CHECK_THROWS_AS(testing::ReplayFixture::load(copy.directory), testing::ReplayFixtureError);
    CHECK_THROWS_WITH(testing::ReplayFixture::load(copy.directory),
                      Catch::Matchers::ContainsSubstring("is missing"));
  }
  const auto& invalid = application::hill_motion_fixture::kUnknownPolicy;
  static_cast<void>(workspace.write_file(
      testing::hill_motion_fixture::kCopiedMatch,
      application::test_fixture::replace_once(copy.match, invalid.field, invalid.replacement)));
  application::test_fixture::require_domain_validation_error_code<
      blob_royale::gameplay::GameplayValidationError>(
      [&] { static_cast<void>(testing::ReplayFixture::load(copy.directory)); },
      blob_royale::gameplay::GameplayValidationCode::kKingOfTheHillMotionPolicyInvalid);

  const auto& random = application::hill_motion_fixture::kRandomRoam;
  static_cast<void>(workspace.write_file(
      testing::hill_motion_fixture::kCopiedMatch,
      application::test_fixture::replace_once(copy.match, random.field, random.replacement)));
  CHECK(testing::ReplayFixture::load(copy.directory).king_of_the_hill().motion_policy() ==
        blob_royale::gameplay::KingOfTheHillConfiguration::HillMotionPolicy::kRandomRoam);
}
