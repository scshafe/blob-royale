#include "../fixtures/bounded_name_fixture.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/race_mode_state.hpp"
#include "race_road_name.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::bounded_name_fixture;

namespace {

template <typename State>
concept HasMirroredTrack = requires(const State state) { state.track; };

template <typename State>
concept HasMirroredTrackHalfWidth = requires(const State state) { state.track_half_width; };

static_assert(std::is_aggregate_v<simulation::RaceModeState>);
static_assert(!std::is_default_constructible_v<simulation::RaceModeState>);
static_assert(std::is_nothrow_move_constructible_v<simulation::RaceModeState>);
static_assert(std::is_nothrow_move_assignable_v<simulation::RaceModeState>);
static_assert(!HasMirroredTrack<simulation::RaceModeState>);
static_assert(!HasMirroredTrackHalfWidth<simulation::RaceModeState>);

} // namespace

TEST_CASE("race mode state requires a road identity while default match state remains undeclared",
          "[unit][simulation][race_mode_state]") {
  simulation::MatchState match;
  CHECK(std::holds_alternative<simulation::NoModeState>(match.mode_state));

  const auto& race = match.mode_state.emplace<simulation::RaceModeState>(
      simulation::RaceModeState{.road = simulation::RaceRoadName::create(fixture::kOwnedName)});
  CHECK(race.road.value() == fixture::kOwnedName);
  CHECK(race.checkpoint_radius == 0.0);
  CHECK(race.checkpoints.empty());
  CHECK(race.time_limit_ticks == 0);
  CHECK(race.finish_window_ticks == 0);
  CHECK(race.standings.empty());
}

TEST_CASE("race mode state copies and compares its required road by owned value",
          "[unit][simulation][race_mode_state]") {
  const simulation::RaceModeState original{
      .road = simulation::RaceRoadName::create(fixture::kOwnedName)};
  simulation::RaceModeState copied(original);
  CHECK(copied == original);
  CHECK(copied.road.value().data() != original.road.value().data());

  const simulation::RaceModeState moved(std::move(copied));
  CHECK(moved == original);
  CHECK(copied.road == original.road);
  CHECK(moved.road.value().data() != copied.road.value().data());

  copied.road = simulation::RaceRoadName::create(fixture::kDifferentName);
  CHECK_FALSE(copied == original);
  CHECK(original.road.value() == fixture::kOwnedName);
  CHECK(moved.road.value() == fixture::kOwnedName);
}
