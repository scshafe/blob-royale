#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "spatial_grid.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::SimulationConfig
configuration(const double drag_per_second = simulation::SimulationConfig::kDefaultDragPerSecond) {
  return simulation::SimulationConfig::create(500.0, 500.0, 10.0,
                                              simulation::SimulationConfig::kRequiredTicksPerSecond,
                                              10, 10, drag_per_second);
}

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::MapDefinition map() {
  return simulation::MapDefinition::create(
      "arena-500x500", simulation::ArenaBounds::create(500.0, 500.0), {},
      {simulation::MapDefinition::Marker::spawn(point(100.0, 100.0))},
      simulation::MapMetadata::none());
}

[[nodiscard]] simulation::GameWorld world() {
  return simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(1),
      simulation::PhysicsBody::create(point(100.0, 100.0), point(0.0, 0.0), point(0.0, 0.0)))});
}

template <typename Context, typename = void> struct InputBatchDetector : std::false_type {};
template <typename Context>
struct InputBatchDetector<Context,
                          std::void_t<decltype(std::declval<const Context&>().input_batch())>>
    : std::true_type {};

template <typename Context, typename = void> struct NowDetector : std::false_type {};
template <typename Context>
struct NowDetector<Context, std::void_t<decltype(std::declval<const Context&>().now())>>
    : std::true_type {};

template <typename Context>
inline constexpr bool kHasInputBatch = InputBatchDetector<Context>::value;
template <typename Context> inline constexpr bool kHasNow = NowDetector<Context>::value;

// Owns everything a context references, so a test can hold a context without the map or the index
// it names outliving the assertion.
class ContextFixture final {
public:
  ContextFixture(const simulation::TickSequence tick_sequence,
                 simulation::SimulationConfig simulation_config)
      : world_(world()), map_(map()),
        grid_(simulation::SpatialGrid::create(simulation_config, map_.bounds(), world_)),
        context_(simulation::TickContext::create(tick_sequence, simulation::FixedDelta::canonical(),
                                                 std::move(simulation_config), map_, grid_)) {}

  ContextFixture(const ContextFixture&) = delete;
  ContextFixture(ContextFixture&&) = delete;
  ContextFixture& operator=(const ContextFixture&) = delete;
  ContextFixture& operator=(ContextFixture&&) = delete;
  ~ContextFixture() = default;

  [[nodiscard]] const simulation::TickContext& context() const noexcept { return context_; }
  [[nodiscard]] const simulation::MapDefinition& declared_map() const noexcept { return map_; }
  [[nodiscard]] const simulation::SpatialGrid& index() const noexcept { return grid_; }

private:
  simulation::GameWorld world_;
  simulation::MapDefinition map_;
  simulation::SpatialGrid grid_;
  simulation::TickContext context_;
};

} // namespace

TEST_CASE("TickContext publishes the sequence the tick commits, its delta, and its configuration",
          "[unit][simulation][tick_context]") {
  const ContextFixture fixture(simulation::TickSequence::create(41), configuration(2.5));

  CHECK(fixture.context().tick_sequence() == simulation::TickSequence::create(41));
  CHECK(fixture.context().fixed_delta() == simulation::FixedDelta::canonical());
  CHECK(fixture.context().simulation_config() == configuration(2.5));
  CHECK(fixture.context().simulation_config().drag_per_second() == 2.5);
}

TEST_CASE("TickContext publishes the tick's map and its read-only spatial index",
          "[unit][simulation][tick_context][map_definition][spatial_grid]") {
  // ADR 0004 promises a system the map and the rebuilt index. `spatial_index()` exists so an area
  // query is a grid lookup rather than an O(n) scan every mechanic would otherwise have to write.
  const ContextFixture fixture(simulation::TickSequence::create(3), configuration());

  CHECK(fixture.context().map() == fixture.declared_map());
  CHECK(fixture.context().map().spawn_points().size() == 1);
  CHECK(fixture.context().spatial_index() == fixture.index());
  CHECK(fixture.context().spatial_index().candidate_pairs().empty());
}

TEST_CASE("TickContext exposes the map and index as read-only references",
          "[unit][simulation][tick_context]") {
  // Read-only is the contract: the index is a derived value the kernel owns, and a system that
  // could write it could make the broad phase disagree with the positions it indexes.
  STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const simulation::TickContext&>().map()),
                                const simulation::MapDefinition&>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<const simulation::TickContext&>().spatial_index()),
                     const simulation::SpatialGrid&>);
  STATIC_REQUIRE(
      std::is_same_v<decltype(std::declval<const simulation::TickContext&>().simulation_config()),
                     const simulation::SimulationConfig&>);
}

TEST_CASE("TickContext is a copyable value that carries its configuration rather than a reference",
          "[unit][simulation][tick_context]") {
  STATIC_REQUIRE(std::is_copy_constructible_v<simulation::TickContext>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::TickContext>);

  const ContextFixture first(simulation::TickSequence::create(3), configuration());
  const simulation::TickContext copied = first.context();
  const ContextFixture other_sequence(simulation::TickSequence::create(4), configuration());

  CHECK(first.context() == copied);
  CHECK_FALSE(first.context() == other_sequence.context());
}

TEST_CASE("TickContext equality compares the referenced map and index by value",
          "[unit][simulation][tick_context]") {
  // Two contexts built over separately owned but equal maps and indexes are equal, which is what
  // keeps the context a value rather than a pair of addresses.
  const ContextFixture first(simulation::TickSequence::create(9), configuration());
  const ContextFixture second(simulation::TickSequence::create(9), configuration());

  CHECK(first.context() == second.context());
}

TEST_CASE("TickContext exposes no clock and no InputBatch", "[unit][simulation][tick_context]") {
  // Absence is the contract, so it is asserted rather than assumed. A clock or a batch accessor
  // would let a system read a wall time or observe a half-applied intake. `map()` was the third
  // member of this list until Step 18 introduced MapDefinition and added the accessor and the
  // field it reads together; the two assertions above are its replacement.
  STATIC_REQUIRE_FALSE(kHasInputBatch<simulation::TickContext>);
  STATIC_REQUIRE_FALSE(kHasNow<simulation::TickContext>);
}
