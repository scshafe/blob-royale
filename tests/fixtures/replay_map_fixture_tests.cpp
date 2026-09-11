#include "replay_fixture.hpp"

// Reuse the existing isolated filesystem fixture instead of another temporary-directory owner.
#include "../unit/application/application_input_test_fixture.hpp"
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
};

// Frozen map identities, bounds and retained marker counts from ba75500, before Step 6.
// Existing replay tests independently pin committed motion, lifecycle and race mirrors.
constexpr std::array kReplayMaps{
    ReplayMapCase{"hill-contested", "hill_contested_arena", 960.0, 640.0, 3, false},
    ReplayMapCase{"hill-scripted-match", "hill_scripted_arena", 960.0, 640.0, 4, false},
    ReplayMapCase{"hill-threshold", "hill_threshold_arena", 960.0, 640.0, 3, false},
    ReplayMapCase{"hill-time-limit-draw", "hill_draw_arena", 960.0, 640.0, 3, false},
    ReplayMapCase{"race-finish-window", "race_finish_window", 960.0, 640.0, 5, true},
    ReplayMapCase{"race-off-track-return", "race_off_track_return", 960.0, 640.0, 4, true},
    ReplayMapCase{"race-scripted-course", "race_scripted_course", 960.0, 640.0, 4, true},
    ReplayMapCase{"race-shared-finish", "race_shared_finish", 960.0, 640.0, 5, true},
    ReplayMapCase{"race-time-limit", "race_time_limit", 960.0, 640.0, 5, true},
    ReplayMapCase{"royale-drag-decay", "royale_drag_corridor", 40000.0, 4000.0, 3, false},
    ReplayMapCase{"royale-elimination-timing", "royale_boundary_arena", 960.0, 640.0, 3, false},
    ReplayMapCase{"royale-scripted-match", "royale_scripted_arena", 960.0, 640.0, 4, false},
    ReplayMapCase{"royale-simultaneous-draw", "royale_draw_arena", 960.0, 640.0, 4, false},
    ReplayMapCase{"royale-spawn-order", "royale_spawn_arena", 960.0, 640.0, 4, false},
    ReplayMapCase{"royale-thrust-integration", "royale_thrust_arena", 960.0, 640.0, 3, false},
    ReplayMapCase{"royale-transition-per-tick", "royale_cycle_arena", 960.0, 640.0, 1, false},
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
    CHECK(loaded.static_bodies().empty());
    CHECK(loaded.terrain().holes().empty());
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
