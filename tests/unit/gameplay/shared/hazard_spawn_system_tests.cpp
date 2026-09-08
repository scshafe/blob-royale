#include "shared/hazard_spawn_system.hpp"

#include "gameplay_test_fixture.hpp"

#include "components/controllable_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "entity_id.hpp"
#include "map_definition.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "royale/royale_configuration.hpp"
#include "royale/royale_mode.hpp"
#include "sandbox/sandbox_mode.hpp"
#include "shared/hazard_archetype.hpp"
#include "shared/hazard_crossing.hpp"
#include "simulation_limits.hpp"
#include "system_pipeline.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr double kArenaWidth = 960.0;
constexpr double kArenaHeight = 640.0;
constexpr double kHazardRadius = 26.0;
constexpr double kHazardSpeed = 200.0;
// 400 ticks per second, so this is exactly 20 ticks and the arithmetic below stays readable.
constexpr double kSpawnIntervalSeconds = 0.05;
constexpr std::uint64_t kSpawnIntervalTicks = 20;

[[nodiscard]] gameplay::HazardArchetype
archetype(const std::string_view kind, const bool lethal,
          const double interval_seconds = kSpawnIntervalSeconds) {
  return gameplay::HazardArchetype::create(gameplay::HazardArchetype::Section{
      std::string{kind}, kHazardRadius, 40.0, 0.2, kHazardSpeed, interval_seconds, lethal});
}

// A royale whose match reaches `running` almost immediately and then stays there, so a test spends
// its ticks on hazards rather than on the lobby.
//
// **Two players, not one.** Royale is last blob standing, so a one-player match satisfies its own
// outcome on the first running tick and transitions straight to `ended` -- at which point the
// spawner correctly does nothing and every hazard assertion below would fail for a reason that has
// nothing to do with hazards.
[[nodiscard]] gameplay::RoyaleConfiguration prompt_royale() {
  gameplay::RoyaleConfiguration::Section section = gameplay::RoyaleConfiguration::default_section();
  section.lobby_minimum_players = 2;
  section.countdown_seconds = 0.01;
  return gameplay::RoyaleConfiguration::create(section);
}

[[nodiscard]] testing::SteppedGame royale_driver(std::vector<gameplay::HazardArchetype> hazards,
                                                 const std::uint64_t seed = 0) {
  return testing::SteppedGame{testing::gameplay_simulation(
      gameplay::RoyaleMode::create(prompt_royale(), std::move(hazards)), testing::gameplay_map(4),
      seed)};
}

// The hazards one snapshot published: every entity carrying a body and no controller. A player has
// both, the zone has neither, and a map's static bodies would have a body alone -- the fixture map
// declares none, which is what makes this projection exact here.
[[nodiscard]] std::vector<simulation::PhysicsBody>
published_hazards(const simulation::WorldSnapshot& snapshot) {
  std::vector<simulation::PhysicsBody> hazards;
  for (const simulation::ComponentStore<simulation::PhysicsBody>::Entry& entry :
       snapshot.components<simulation::PhysicsBody>()) {
    bool driven = false;
    for (const simulation::ComponentStore<simulation::Controllable>::Entry& controllable :
         snapshot.components<simulation::Controllable>()) {
      driven = driven || controllable.entity == entry.entity;
    }
    if (!driven) {
      hazards.push_back(entry.value);
    }
  }
  return hazards;
}

[[nodiscard]] std::size_t lethal_count(const simulation::WorldSnapshot& snapshot) {
  return snapshot.components<simulation::LethalOnContact>().size();
}

// Steps until the match is running. The lobby and countdown spend ticks in which the spawner
// deliberately does nothing, and every test below starts from the first tick on which it does.
void start_match(testing::SteppedGame& driver) {
  simulation::WorldSnapshot snapshot =
      driver.step({testing::spawn_command(1), testing::spawn_command(2)});
  for (std::size_t tick = 0; tick < 40; ++tick) {
    if (snapshot.match().phase() == simulation::MatchPhase::kRunning) {
      return;
    }
    snapshot = driver.step();
  }
  FAIL("the fixture royale never reached the running phase");
}

} // namespace

TEST_CASE("hazard_spawn seats nothing before the match is running",
          "[unit][gameplay][shared][hazard_spawn]") {
  testing::SteppedGame driver = royale_driver({archetype("plaid_meteorite", true)});
  const simulation::WorldSnapshot lobby =
      driver.step({testing::spawn_command(1), testing::spawn_command(2)});

  REQUIRE(lobby.match().phase() != simulation::MatchPhase::kRunning);
  CHECK(published_hazards(lobby).empty());
  // Nothing was drawn either, which is the stronger statement: a tick that seats nothing must not
  // advance the generator, or the arena's contents would depend on how long the lobby lasted.
  CHECK(lobby.random_draw_count() == 0);
}

TEST_CASE("hazard_spawn seats a crossing body carrying the archetype's own physics",
          "[unit][gameplay][shared][hazard_spawn]") {
  testing::SteppedGame driver = royale_driver({archetype("plaid_meteorite", true)});
  start_match(driver);
  const simulation::WorldSnapshot snapshot = driver.advance(kSpawnIntervalTicks * 2);

  const std::vector<simulation::PhysicsBody> hazards = published_hazards(snapshot);
  REQUIRE_FALSE(hazards.empty());
  for (const simulation::PhysicsBody& hazard : hazards) {
    CHECK(hazard.radius() == kHazardRadius);
    CHECK(hazard.mass() == 40.0);
    CHECK(hazard.restitution() == 0.2);
    CHECK_FALSE(hazard.is_static());
    // Without this it would fold off the wall it entered through and rattle around the arena
    // forever instead of leaving.
    CHECK(hazard.crosses_bounds());
    // And without *this* it would never reach the wall in the first place at any deployed drag:
    // phase 1's factor is geometric, so a dragged body's total travel is `speed / drag_per_second`
    // and a hazard would stall into a drifting obstacle a fraction of the way across. The same
    // constant is what makes `hazard_lifetime_ticks`' `distance / speed` a duration.
    CHECK(hazard.drag_scale() == simulation::PhysicsBody::kMinimumDragScale);
    // The speed is the archetype's; only the direction was drawn.
    const double speed = std::sqrt((hazard.velocity().x() * hazard.velocity().x()) +
                                   (hazard.velocity().y() * hazard.velocity().y()));
    CHECK(speed == Catch::Approx(kHazardSpeed));
  }
}

TEST_CASE("hazard_spawn seats the body outside the arena so it enters under its own velocity",
          "[unit][gameplay][shared][hazard_spawn]") {
  testing::SteppedGame driver = royale_driver({archetype("plaid_meteorite", true)});
  start_match(driver);

  // The tick a hazard first appears on is the tick to measure: after that the kernel has moved it
  // inward and "outside" stops being the claim.
  bool observed = false;
  for (std::size_t tick = 0; tick < kSpawnIntervalTicks + 1 && !observed; ++tick) {
    const simulation::WorldSnapshot snapshot = driver.step();
    for (const simulation::PhysicsBody& hazard : published_hazards(snapshot)) {
      const double x = hazard.position().x();
      const double y = hazard.position().y();
      const bool outside = x < 0.0 || x > kArenaWidth || y < 0.0 || y > kArenaHeight;
      CHECK(outside);
      observed = true;
    }
  }
  REQUIRE(observed);
}

TEST_CASE("a hazard carries the lethal marker only when its archetype declares it",
          "[unit][gameplay][shared][hazard_spawn]") {
  SECTION("a lethal kind attaches the marker") {
    testing::SteppedGame driver = royale_driver({archetype("plaid_meteorite", true)});
    start_match(driver);
    const simulation::WorldSnapshot snapshot = driver.advance(kSpawnIntervalTicks * 2);
    REQUIRE_FALSE(published_hazards(snapshot).empty());
    CHECK(lethal_count(snapshot) == published_hazards(snapshot).size());
  }

  SECTION("a merely heavy kind does not") {
    // The difference between a comet and a boulder is one configuration key and one component.
    testing::SteppedGame driver = royale_driver({archetype("velvet_boulder", false)});
    start_match(driver);
    const simulation::WorldSnapshot snapshot = driver.advance(kSpawnIntervalTicks * 2);
    REQUIRE_FALSE(published_hazards(snapshot).empty());
    CHECK(lethal_count(snapshot) == 0);
  }
}

TEST_CASE("a hazard's Lifetime is derived from its own speed and the arena it must cross",
          "[unit][gameplay][shared][hazard_spawn]") {
  testing::SteppedGame driver = royale_driver({archetype("plaid_meteorite", true)});
  start_match(driver);
  const simulation::WorldSnapshot snapshot = driver.advance(kSpawnIntervalTicks + 1);

  REQUIRE_FALSE(snapshot.components<simulation::Lifetime>().empty());

  // The bound the derivation must satisfy, computed here from the arena and the archetype rather
  // than copied from the system: a crossing is at most the diagonal plus the clearance at each end,
  // and at least the shorter arena axis. A hardcoded expected number would pass just as well
  // against a hardcoded implementation, which is the failure this avoids.
  const double clearance = gameplay::kHazardEntryClearanceRadii * kHazardRadius;
  const double longest =
      std::sqrt((kArenaWidth * kArenaWidth) + (kArenaHeight * kArenaHeight)) + (2.0 * clearance);
  const double shortest = kArenaHeight + (2.0 * clearance);
  const double seconds_per_tick = 1.0 / static_cast<double>(simulation::kSimulationTicksPerSecond);
  const auto upper =
      static_cast<std::uint64_t>(std::ceil(longest / kHazardSpeed / seconds_per_tick));
  const auto lower =
      static_cast<std::uint64_t>(std::floor(shortest / kHazardSpeed / seconds_per_tick));

  for (const simulation::ComponentStore<simulation::Lifetime>::Entry& entry :
       snapshot.components<simulation::Lifetime>()) {
    CHECK(entry.value.ticks_remaining <= upper);
    CHECK(entry.value.ticks_remaining >= lower);
  }
}

TEST_CASE("a hazard despawns after crossing rather than accumulating",
          "[unit][gameplay][shared][hazard_spawn]") {
  // A fast hazard against a long interval: each one is gone well before the next is due, so the
  // standing population must return to zero rather than climb. This is the property that keeps a
  // spawner from filling the world's seats, and it needs `lifetime_expiry` to be declared -- the
  // component alone despawns nothing.
  // Deliberately not lethal: a kill would leave one blob standing, end the match, and stop the
  // spawner for a reason that has nothing to do with lifetimes.
  testing::SteppedGame driver = royale_driver({archetype("velvet_boulder", false, 2.0)});
  start_match(driver);

  std::size_t peak = 0;
  std::size_t seen_spawns = 0;
  for (std::size_t tick = 0; tick < 1'200; ++tick) {
    const simulation::WorldSnapshot snapshot = driver.step();
    REQUIRE(snapshot.match().phase() == simulation::MatchPhase::kRunning);
    const std::size_t standing = published_hazards(snapshot).size();
    peak = standing > peak ? standing : peak;
    seen_spawns += standing > 0 ? 1 : 0;
  }
  REQUIRE(seen_spawns > 0);
  // One at a time, never a growing crowd.
  CHECK(peak == 1);
}

TEST_CASE("the same seed and the same commands reproduce every crossing exactly",
          "[unit][gameplay][shared][hazard_spawn][determinism]") {
  // The assertion is a comparison of two runs, not an inspection of one: a hardcoded expected
  // position would prove the arithmetic is stable across builds and nothing about whether the
  // spawner is a pure function of (seed, command log).
  const std::vector<gameplay::HazardArchetype> table{archetype("plaid_meteorite", true),
                                                     archetype("velvet_boulder", false, 0.07)};

  testing::SteppedGame first = royale_driver(table, 20260907);
  testing::SteppedGame second = royale_driver(table, 20260907);
  start_match(first);
  start_match(second);

  for (std::size_t tick = 0; tick < 200; ++tick) {
    const simulation::WorldSnapshot left = first.step();
    const simulation::WorldSnapshot right = second.step();
    // Whole-snapshot equality, so a divergence in any component of any entity fails at the first
    // tick it happens rather than whenever someone thought to look.
    REQUIRE(left == right);
  }

  const simulation::WorldSnapshot left = first.advance(1);
  // Draws actually happened, so the equality above is evidence about a generator that ran rather
  // than about one that was never touched.
  CHECK(left.random_draw_count() > 0);
}

TEST_CASE("a different seed produces different crossings",
          "[unit][gameplay][shared][hazard_spawn][determinism]") {
  const std::vector<gameplay::HazardArchetype> table{archetype("plaid_meteorite", true)};
  testing::SteppedGame first = royale_driver(table, 1);
  testing::SteppedGame second = royale_driver(table, 2);
  start_match(first);
  start_match(second);

  const simulation::WorldSnapshot left = first.advance(kSpawnIntervalTicks * 3);
  const simulation::WorldSnapshot right = second.advance(kSpawnIntervalTicks * 3);

  REQUIRE_FALSE(published_hazards(left).empty());
  REQUIRE_FALSE(published_hazards(right).empty());
  // The seed is the only difference between these two runs, so this is what says the geometry is
  // genuinely drawn rather than a fixed pattern a player could memorize.
  CHECK_FALSE(left == right);
}

TEST_CASE("the startup bound is never violated by the crossings the spawner actually draws",
          "[unit][gameplay][shared][hazard_spawn][hazard_crossing]") {
  // The agreement test between the two callers of `shared/hazard_crossing.hpp`.
  // `application/match_startup_validation.cpp` refuses a configuration whose *worst case* standing
  // population would exceed the published snapshot bound, and it computes that worst case from the
  // longest crossing this arena admits. That is only a bound if no crossing the spawner draws is
  // longer and no population it produces is larger, which is what this observes over many drawn
  // crossings rather than asserting once from the same expression the implementation uses.
  //
  // Deliberately not lethal: a kill would leave one blob standing, end the match, and stop the
  // spawner for a reason that has nothing to do with the bound.
  const simulation::ArenaBounds bounds = simulation::ArenaBounds::create(kArenaWidth, kArenaHeight);
  const double seconds_per_tick = 1.0 / static_cast<double>(simulation::kSimulationTicksPerSecond);
  const gameplay::HazardArchetype archetype_under_test =
      archetype("velvet_boulder", false, kSpawnIntervalSeconds);
  const std::uint64_t longest_lifetime = gameplay::hazard_lifetime_ticks(
      gameplay::longest_hazard_travel_distance(bounds, kHazardRadius), kHazardSpeed,
      seconds_per_tick);
  const std::uint64_t standing_bound =
      gameplay::maximum_standing_hazard_count(archetype_under_test, bounds, seconds_per_tick);

  std::size_t observed_spawns = 0;
  std::size_t peak_standing = 0;
  // Several seeds, because a single one exercises one sequence of crossings and the claim is about
  // every crossing the geometry admits.
  for (const std::uint64_t seed : {0ULL, 1ULL, 20260907ULL, 999983ULL}) {
    testing::SteppedGame driver = royale_driver({archetype_under_test}, seed);
    start_match(driver);
    for (std::size_t tick = 0; tick < kSpawnIntervalTicks * 30; ++tick) {
      const simulation::WorldSnapshot snapshot = driver.step();
      REQUIRE(snapshot.match().phase() == simulation::MatchPhase::kRunning);
      const std::size_t standing = published_hazards(snapshot).size();
      peak_standing = standing > peak_standing ? standing : peak_standing;
      observed_spawns += standing;
      for (const simulation::ComponentStore<simulation::Lifetime>::Entry& entry :
           snapshot.components<simulation::Lifetime>()) {
        // A drawn crossing runs edge to opposite edge, so it can be at most the arena's diagonal
        // and the lifetime derived from it can be at most the one derived from that diagonal.
        CHECK(entry.value.ticks_remaining <= longest_lifetime);
      }
      CHECK(standing <= standing_bound);
    }
  }
  // Evidence about a spawner that ran rather than one that never seated anything.
  REQUIRE(observed_spawns > 0);
  REQUIRE(peak_standing > 0);
}

TEST_CASE("sandbox declares neither the spawner nor the lethal row",
          "[unit][gameplay][shared][hazard_spawn]") {
  // The test that the mechanic is optional rather than ambient. Sandbox stays free play, and a
  // hazard table in its configuration would simply never be read.
  const simulation::SystemPipeline systems = gameplay::SandboxMode::create()->systems();
  for (const simulation::SystemStage stage :
       {simulation::SystemStage::kPreKernel, simulation::SystemStage::kPostKernel,
        simulation::SystemStage::kLifecycle}) {
    for (const simulation::SystemPipeline::StagedSystem& declared : systems.systems_at(stage)) {
      CHECK(declared.system->name() != gameplay::HazardSpawnSystem::kSystemName);
    }
  }
}
