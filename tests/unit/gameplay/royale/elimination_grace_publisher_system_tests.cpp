#include "royale/elimination_grace_publisher_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/no_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics_body.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kPublishedTick = 17;
constexpr std::uint64_t kGraceTicks = 1'200;

[[nodiscard]] simulation::GameWorld empty_world() { return simulation::GameWorld::create({}); }

[[nodiscard]] simulation::RoyalePlacementsModeState
royale_block_of(const simulation::GameWorld& world) {
  const auto* held = std::get_if<simulation::RoyalePlacementsModeState>(&world.match().mode_state);
  REQUIRE(held != nullptr);
  return *held;
}

void publish(simulation::GameWorld& world, const std::uint64_t grace_ticks) {
  const testing::TickHarness harness{simulation::TickSequence::create(kPublishedTick)};
  gameplay::EliminationGracePublisherSystem::create(grace_ticks)->apply(world, harness.context());
}

} // namespace

TEST_CASE("the publisher stamps the configured grace into royale's mode-state block",
          "[unit][gameplay][royale][mode_state]") {
  simulation::GameWorld world = empty_world();
  publish(world, kGraceTicks);

  CHECK(simulation::mode_match_state_schema_id_of(world.match().mode_state) ==
        std::string_view{"royale_placements"});
  CHECK(royale_block_of(world).elimination_grace_ticks == kGraceTicks);
}

TEST_CASE("a grace of zero is published as zero rather than treated as absent",
          "[unit][gameplay][royale][mode_state]") {
  // Zero is a legal `[royale]` value and means elimination on the first outside tick, because
  // `zone_elimination` increments before it tests. Publishing it is therefore a statement, not a
  // missing value, and the wire has no other way to say it.
  simulation::GameWorld world = empty_world();
  publish(world, 0);

  CHECK(royale_block_of(world).elimination_grace_ticks == 0);
}

TEST_CASE("the publisher writes one member and leaves every other member as it found it",
          "[unit][gameplay][royale][mode_state]") {
  // This is the whole reason it is allowed to run beside `placement_recorder` rather than inside
  // it: a rule that stamps a constant must not be able to disturb the ranking or the observed
  // phase, and the way that is guaranteed is that it names exactly one member.
  simulation::GameWorld world = empty_world();
  world.mutable_match().mode_state = simulation::RoyalePlacementsModeState{
      {simulation::RoyalePlacement{simulation::EntityId::create(4),
                                   simulation::ControllerId::create(9), 2,
                                   simulation::TickSequence::create(11)}},
      simulation::MatchPhase::kEnded,
      7};

  publish(world, kGraceTicks);

  const simulation::RoyalePlacementsModeState published = royale_block_of(world);
  CHECK(published.elimination_grace_ticks == kGraceTicks);
  REQUIRE(published.placements.size() == 1);
  CHECK(published.placements.front().entity == simulation::EntityId::create(4));
  CHECK(published.placements.front().controller == simulation::ControllerId::create(9));
  CHECK(published.placements.front().placement == 2);
  CHECK(published.previous_phase == simulation::MatchPhase::kEnded);
}

TEST_CASE("a world holding another mode's arm is given royale's, carrying the grace",
          "[unit][gameplay][royale][mode_state]") {
  // The same answer `royale_mode_state_of` gives a reader: a world holding another arm reads as
  // the default block. A royale system running on such a world is a composition error either way,
  // and `placement_recorder` already replaces the arm rather than refusing, so the two writers
  // agree about what that world means.
  simulation::GameWorld world = empty_world();
  world.mutable_match().mode_state = simulation::NoModeState{};

  publish(world, kGraceTicks);

  const simulation::RoyalePlacementsModeState published = royale_block_of(world);
  CHECK(published.elimination_grace_ticks == kGraceTicks);
  CHECK(published.placements.empty());
  CHECK(published.previous_phase == simulation::MatchPhase::kLobby);
}

TEST_CASE("the publisher writes the same value on every tick it is applied",
          "[unit][gameplay][royale][mode_state]") {
  // It holds immutable configuration and reads no world state, so its output is a function of its
  // construction alone. Anything else would make a snapshot's grace depend on when it was taken.
  simulation::GameWorld world = empty_world();
  for (int repetition = 0; repetition < 3; ++repetition) {
    publish(world, kGraceTicks);
    CHECK(royale_block_of(world).elimination_grace_ticks == kGraceTicks);
  }
  CHECK(royale_block_of(world).placements.empty());
}
