#include "fixed_delta.hpp"
#include "simulation_config.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] simulation::SimulationConfig
configuration(const double drag_per_second = simulation::SimulationConfig::kDefaultDragPerSecond) {
  return simulation::SimulationConfig::create(
      500.0, 500.0, 10.0, simulation::SimulationConfig::kRequiredTicksPerSecond, 10, 10,
      drag_per_second);
}

template <typename Context, typename = void> struct MapDetector : std::false_type {};
template <typename Context>
struct MapDetector<Context, std::void_t<decltype(std::declval<const Context&>().map())>>
    : std::true_type {};

template <typename Context, typename = void> struct InputBatchDetector : std::false_type {};
template <typename Context>
struct InputBatchDetector<Context, std::void_t<decltype(std::declval<const Context&>().input_batch())>>
    : std::true_type {};

template <typename Context, typename = void> struct NowDetector : std::false_type {};
template <typename Context>
struct NowDetector<Context, std::void_t<decltype(std::declval<const Context&>().now())>>
    : std::true_type {};

template <typename Context> inline constexpr bool kHasMap = MapDetector<Context>::value;
template <typename Context>
inline constexpr bool kHasInputBatch = InputBatchDetector<Context>::value;
template <typename Context> inline constexpr bool kHasNow = NowDetector<Context>::value;

} // namespace

TEST_CASE("TickContext publishes the sequence the tick commits, its delta, and its configuration",
          "[unit][simulation][tick_context]") {
  const simulation::TickContext context = simulation::TickContext::create(
      simulation::TickSequence::create(41), simulation::FixedDelta::canonical(), configuration(2.5));

  CHECK(context.tick_sequence() == simulation::TickSequence::create(41));
  CHECK(context.fixed_delta() == simulation::FixedDelta::canonical());
  CHECK(context.simulation_config() == configuration(2.5));
  CHECK(context.simulation_config().drag_per_second() == 2.5);
}

TEST_CASE("TickContext is a copyable value that carries its configuration rather than a reference",
          "[unit][simulation][tick_context]") {
  STATIC_REQUIRE(std::is_copy_constructible_v<simulation::TickContext>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::TickContext>);

  const simulation::TickContext first = simulation::TickContext::create(
      simulation::TickSequence::create(3), simulation::FixedDelta::canonical(), configuration());
  const simulation::TickContext second = first;

  CHECK(first == second);
  CHECK_FALSE(first == simulation::TickContext::create(simulation::TickSequence::create(4),
                                                       simulation::FixedDelta::canonical(),
                                                       configuration()));
}

TEST_CASE("TickContext exposes no clock, no InputBatch, and no map",
          "[unit][simulation][tick_context]") {
  // Absence is the contract, so it is asserted rather than assumed. A clock or a batch accessor
  // would let a system read a wall time or observe a half-applied intake, and `map()` is the one
  // ADR 0004 member deliberately deferred: MapDefinition does not exist until Step 18 replaces the
  // world-size configuration with it, and Step 18 adds the accessor and the field it reads
  // together. This test is the reminder that turns red the day one of them is added silently.
  STATIC_REQUIRE_FALSE(kHasMap<simulation::TickContext>);
  STATIC_REQUIRE_FALSE(kHasInputBatch<simulation::TickContext>);
  STATIC_REQUIRE_FALSE(kHasNow<simulation::TickContext>);
  STATIC_REQUIRE(std::is_same_v<
                 decltype(std::declval<const simulation::TickContext&>().simulation_config()),
                 const simulation::SimulationConfig&>);
}
